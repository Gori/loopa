#include "core/Timbre/TimbreTransfer.h"

#include "core/Logger.h"

#include <torch/script.h>
#include <torch/nn/functional.h>
#include <ATen/Parallel.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace loopa {

namespace {

// ISMIR model constants (from config.gin).
constexpr double kModelSampleRate = 24000.0;
constexpr long   kXLength         = 131072;      // samples per chunk (~5.46s @ 24kHz)
constexpr long   kAeLatents       = 32;          // AE channels
constexpr long   kAeFrames        = 128;         // kXLength / ae_ratio
constexpr double kSigmaMin        = 0.002;
constexpr double kSigmaMax        = 80.0;
constexpr double kRho             = 7.0;
constexpr double kTimeCondDrop    = -4.0;  // matches drop_values[0] in config.gin
constexpr int    kNbSteps         = 40;          // paper default; fewer -> noisier
constexpr double kGuidance        = 2.0;
// Set LOOPA_TIMBRE_DUMP=/some/dir before launching to save WAV snapshots
// of each pipeline stage (source_48k, source_24k, decoded_24k, final_48k).
constexpr const char* kDumpEnvVar = "LOOPA_TIMBRE_DUMP";

struct Stats {
    float peak = 0.0f;
    double rms = 0.0;
    std::size_t nanCount = 0;
    std::size_t infCount = 0;
};

Stats computeStats(const float* data, std::size_t n) {
    Stats s;
    double sumSq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const float x = data[i];
        if (std::isnan(x)) { ++s.nanCount; continue; }
        if (std::isinf(x)) { ++s.infCount; continue; }
        const float a = std::fabs(x);
        if (a > s.peak) s.peak = a;
        sumSq += static_cast<double>(x) * static_cast<double>(x);
    }
    if (n > 0) s.rms = std::sqrt(sumSq / static_cast<double>(n));
    return s;
}

std::string fmtStats(const char* label, std::size_t n, const Stats& s) {
    std::ostringstream o;
    o << label << " n=" << n << " peak=" << s.peak << " rms=" << s.rms
      << " nan=" << s.nanCount << " inf=" << s.infCount;
    return o.str();
}

// Simple polyphase sinc resampler. Applies a Kaiser-windowed lowpass at the
// minimum of src/dst Nyquist, then does rational-ratio resampling. Not as
// fast as libsoxr but avoids linear-interp aliasing/stair-stepping that
// produces audible distortion on anything above ~6 kHz.
torch::Tensor resample(torch::Tensor audio1d, double srcSr, double dstSr) {
    if (std::abs(srcSr - dstSr) < 1e-6) return audio1d;
    const int64_t srcLen = audio1d.size(-1);
    const int64_t dstLen = static_cast<int64_t>(
        std::llround(static_cast<double>(srcLen) * dstSr / srcSr));

    // Kaiser-windowed sinc with ~64 taps. Cutoff = 0.475 * min(srcSr, dstSr)
    // so we have a transition band up to Nyquist.
    constexpr int zeroCrossings = 32;
    const double fCut = 0.475 * std::min(srcSr, dstSr);
    const double fsHigher = std::max(srcSr, dstSr);
    const int tapsPerSide = static_cast<int>(
        std::ceil(zeroCrossings * fsHigher / fCut));

    // Use torchaudio-free implementation: precompute resample matrix
    // W[dst, src] where each row is the windowed-sinc centered at the
    // fractional src index (dst * srcSr / dstSr). Then y = W @ x.
    // This is equivalent to conv-resample but simple to write.
    auto fp32 = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto src = audio1d.to(torch::kCPU).to(torch::kFloat32).contiguous();
    auto out = torch::zeros({dstLen}, fp32);
    const float* sp = src.data_ptr<float>();
    float* op = out.data_ptr<float>();
    const double ratio = srcSr / dstSr;
    // Kaiser window beta for ~60 dB stopband attenuation.
    const double beta = 6.0;
    auto i0 = [](double x) {
        // Modified Bessel I0 approximation (Numerical Recipes-style).
        double ax = std::fabs(x), ans;
        if (ax < 3.75) {
            double y = x / 3.75; y *= y;
            ans = 1.0 + y*(3.5156229 + y*(3.0899424 + y*(1.2067492
                  + y*(0.2659732 + y*(0.0360768 + y*0.0045813)))));
        } else {
            double y = 3.75 / ax;
            ans = (std::exp(ax) / std::sqrt(ax)) * (0.39894228
                  + y*(0.01328592 + y*(0.00225319 + y*(-0.00157565
                  + y*(0.00916281 + y*(-0.02057706 + y*(0.02635537
                  + y*(-0.01647633 + y*0.00392377))))))));
        }
        return ans;
    };
    const double i0Beta = i0(beta);
    const double gain = std::min(1.0, dstSr / srcSr);  // prevent clipping on upsample
    for (int64_t n = 0; n < dstLen; ++n) {
        const double centre = static_cast<double>(n) * ratio;
        const int64_t iStart = static_cast<int64_t>(std::floor(centre)) - tapsPerSide + 1;
        const int64_t iEnd   = iStart + 2 * tapsPerSide;
        double acc = 0.0;
        for (int64_t i = iStart; i <= iEnd; ++i) {
            if (i < 0 || i >= srcLen) continue;
            const double t = (static_cast<double>(i) - centre) * gain;
            double sinc;
            if (std::fabs(t) < 1e-9) sinc = 1.0;
            else                      sinc = std::sin(M_PI * t) / (M_PI * t);
            const double u = static_cast<double>(i - iStart + 1) / (2 * tapsPerSide);
            const double win = i0(beta * std::sqrt(std::max(0.0, 1.0 - (2*u - 1)*(2*u - 1)))) / i0Beta;
            acc += static_cast<double>(sp[i]) * sinc * win * gain;
        }
        op[n] = static_cast<float>(acc);
    }
    return out;
}

// Write a [N]-shaped float32 tensor as a 32-bit-float mono WAV. Tiny helper
// for the diagnostic LOOPA_TIMBRE_DUMP path. No external dep.
void writeWav(const std::string& path, torch::Tensor audio1d, double sr) {
    audio1d = audio1d.to(torch::kCPU).to(torch::kFloat32).contiguous();
    const int32_t n = static_cast<int32_t>(audio1d.size(0));
    // Ensure the parent dir exists; fstream doesn't create it.
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        LOG_WARN("TimbreTransfer: failed to open " + path + " for dump");
        return;
    }
    auto w32 = [&](int32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto w16 = [&](int16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    const int32_t byteRate = static_cast<int32_t>(sr) * 4;
    f.write("RIFF", 4);    w32(36 + n * 4);
    f.write("WAVE", 4);    f.write("fmt ", 4);  w32(16);
    w16(3); w16(1);                             // fmt=IEEE float, 1 ch
    w32(static_cast<int32_t>(sr));
    w32(byteRate); w16(4); w16(32);             // block align, bits per sample
    f.write("data", 4);    w32(n * 4);
    f.write(reinterpret_cast<const char*>(audio1d.data_ptr<float>()), n * 4);
    LOG_INFO("TimbreTransfer: wrote dump " + path
             + " (" + std::to_string(n) + " samples @ "
             + std::to_string(static_cast<int>(sr)) + " Hz)");
}

bool shouldDump() { return std::getenv(kDumpEnvVar) != nullptr; }
std::string dumpDir() { const char* d = std::getenv(kDumpEnvVar); return d ? d : ""; }

torch::Tensor loadTimbreVector(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("TimbreTransfer: can't open timbre vector " + path);
    }
    std::vector<char> buf((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    auto iv = torch::jit::pickle_load(buf);
    auto t = iv.toTensor().to(torch::kFloat32).contiguous().cpu();
    if (t.dim() != 1 || t.size(0) != kAeLatents) {
        throw std::runtime_error("TimbreTransfer: bad timbre vector shape " + path);
    }
    return t.reshape({1, kAeLatents});  // [1, 32]
}

}  // namespace

struct TimbreTransferEngine::Impl {
    torch::jit::script::Module ae;
    torch::jit::script::Module encoder_t;       // timbre encoder
    torch::jit::script::Module encoder_s;       // structure / time-cond encoder
    torch::jit::script::Module time_xform;      // CQT
    torch::jit::script::Module denoise;         // EDM-preconditioned denoiser
    torch::Device device{torch::kCPU};          // all modules on the same device
    std::vector<TimbrePreset> presets;
    std::vector<torch::Tensor> timbreVectors;   // each [1, 32]

    Impl(const std::string& modelDir,
         const std::vector<TimbrePreset>& p) : presets(p) {
        const auto t0 = std::chrono::steady_clock::now();

        // Cap torch's thread pool — same reasoning as before: don't starve
        // the audio callback when running inference on the worker thread.
        torch::set_num_threads(4);
        try { at::set_num_interop_threads(1); } catch (...) {}

        // ISMIR model files contain some fp64 tensors that MPS rejects
        // on direct load. We run everything on CPU — 4-thread CPU on M1
        // Pro does a 10-second loop in ~3 s, which is acceptable offline.
        device = torch::Device(torch::kCPU);
        LOG_INFO("TimbreTransfer: device = CPU");
        if (shouldDump()) {
            LOG_INFO(std::string("TimbreTransfer: ") + kDumpEnvVar
                     + "=" + dumpDir() + " — will dump pipeline WAVs here");
        } else {
            LOG_INFO(std::string("TimbreTransfer: ") + kDumpEnvVar
                     + " not set; pipeline WAV dumps disabled");
        }

        auto loadMod = [&](const std::string& name) {
            const auto path = modelDir + "/" + name;
            LOG_INFO(std::string("TimbreTransfer: loading ") + path);
            return torch::jit::load(path, device);
        };

        ae          = loadMod("ae.ts");          ae.eval();
        encoder_t   = loadMod("enc_t.ts");       encoder_t.eval();
        encoder_s   = loadMod("enc_s.ts");       encoder_s.eval();
        time_xform  = loadMod("time_xform.ts");  time_xform.eval();
        denoise     = loadMod("denoise.ts");     denoise.eval();

        torch::NoGradGuard noGrad;

        auto floatDev = torch::TensorOptions().dtype(torch::kFloat32).device(device);

        // Load + warm each preset timbre vector.
        for (const auto& pr : presets) {
            auto v = loadTimbreVector(pr.vectorPath).to(device).to(torch::kFloat32);
            timbreVectors.push_back(v);
            LOG_INFO("TimbreTransfer: loaded preset '" + pr.name
                     + "' norm=" + std::to_string(v.norm().item<float>()));
        }
        if (timbreVectors.empty()) {
            timbreVectors.push_back(torch::zeros({1, kAeLatents}, floatDev));
            LOG_WARN("TimbreTransfer: no presets — using zero vector fallback");
        }

        // Warm each module so first real call doesn't pay JIT cost.
        auto warmAudio = torch::zeros({1, 1, kXLength}, floatDev);
        auto warmZ     = ae.run_method("encode", warmAudio).toTensor();
        (void)encoder_t.forward({warmZ});
        auto warmCqt   = time_xform.forward({warmAudio}).toTensor();
        auto cqtOpts   = torch::nn::functional::InterpolateFuncOptions()
                            .size(std::vector<int64_t>{kAeFrames})
                            .mode(torch::kNearest);
        auto warmCqtI  = torch::nn::functional::interpolate(warmCqt, cqtOpts);
        auto cMin = warmCqtI.min(), cMax = warmCqtI.max();
        auto warmCqtN  = (warmCqtI - cMin) / (cMax - cMin + 1e-4);
        (void)encoder_s.forward({warmCqtN});
        auto sigma = torch::full({1, 1, 1}, 10.0f, floatDev);
        (void)denoise.forward({warmZ, sigma, timbreVectors[0],
                                torch::zeros({1, 16, kAeFrames}, floatDev)});
        (void)ae.run_method("decode", warmZ);

        const auto t1 = std::chrono::steady_clock::now();
        LOG_INFO("TimbreTransfer: setup + warmup in " + std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count())
            + " ms");
    }

    // One chunk through the full ISMIR pipeline. Member so it has access
    // to the private module handles without friend declarations.
    torch::Tensor runChunk(torch::Tensor audio1d,   // [N], on device
                           torch::Tensor zsem) {    // [1, 32], on device
    auto audio = audio1d.reshape({1, 1, audio1d.size(0)});

    // AE encode.
    auto z = ae.run_method("encode", audio).toTensor();

    // CQT + normalise + encoder_s -> time_cond.
    auto cqt = time_xform.forward({audio}).toTensor();
    auto cqtOpts = torch::nn::functional::InterpolateFuncOptions()
        .size(std::vector<int64_t>{z.size(-1)})
        .mode(torch::kNearest);
    cqt = torch::nn::functional::interpolate(cqt, cqtOpts);
    auto cMin = cqt.min(), cMax = cqt.max();
    cqt = (cqt - cMin) / (cMax - cMin + 1e-4);
    auto time_cond = encoder_s.forward({cqt}).toTensor();

    // Heun sampling loop (EDM). Native here so we can tune steps without
    // re-exporting.
    auto fp32 = torch::TensorOptions().dtype(torch::kFloat32).device(device);
    auto step_idx = torch::arange(kNbSteps, fp32);
    auto tmax1r = static_cast<float>(std::pow(kSigmaMax, 1.0 / kRho));
    auto tmin1r = static_cast<float>(std::pow(kSigmaMin, 1.0 / kRho));
    auto t_steps = torch::pow(tmax1r + step_idx / (kNbSteps - 1) * (tmin1r - tmax1r),
                               static_cast<float>(kRho));
    t_steps = torch::cat({t_steps, torch::zeros({1}, fp32)});

    auto x = torch::randn_like(z) * t_steps[0];

    // CFG setup — matches Python demo's `guidance_type="time_cond"`: only
    // time_cond is replaced with the drop value; zsem passes through in BOTH
    // forwards. Dropping zsem as well ("both" mode) produces unstable outputs
    // (intermittent silence / gaps) because the model isn't well-conditioned
    // when both controls are absent at inference time.
    auto drop_tc = torch::full_like(time_cond, static_cast<float>(kTimeCondDrop));

    for (int i = 0; i < kNbSteps; ++i) {
        auto t_cur  = t_steps[i];
        auto t_next = t_steps[i + 1];
        auto sigma  = t_cur.reshape({1, 1, 1}).expand({1, 1, 1}).contiguous();

        auto cond_d   = denoise.forward({x, sigma, zsem, time_cond}).toTensor();
        auto uncond_d = denoise.forward({x, sigma, zsem, drop_tc}).toTensor();
        auto denoised = uncond_d + kGuidance * (cond_d - uncond_d);

        auto d_cur = (x - denoised) / t_cur;
        auto x1 = x + (t_next - t_cur) * d_cur;

        if (i < kNbSteps - 1) {
            auto sigma2 = t_next.reshape({1, 1, 1}).expand({1, 1, 1}).contiguous();
            auto cond_d2   = denoise.forward({x1, sigma2, zsem, time_cond}).toTensor();
            auto uncond_d2 = denoise.forward({x1, sigma2, zsem, drop_tc}).toTensor();
            auto den2 = uncond_d2 + kGuidance * (cond_d2 - uncond_d2);
            auto d_prime = (x1 - den2) / t_next;
            x1 = x + (t_next - t_cur) * 0.5 * (d_cur + d_prime);
        }
        x = x1;
    }

    auto decoded = ae.run_method("decode", x).toTensor();
    return decoded.reshape({decoded.size(-1)});
    }  // end of runChunk
};     // end of struct Impl

TimbreTransferEngine::TimbreTransferEngine(const std::string& modelDir,
                                            const std::vector<TimbrePreset>& presets)
    : m_impl(std::make_unique<Impl>(modelDir, presets)) {}

TimbreTransferEngine::~TimbreTransferEngine() = default;

int TimbreTransferEngine::numTimbres() const noexcept {
    return static_cast<int>(m_impl->presets.size());
}

const std::string& TimbreTransferEngine::timbreName(int index) const noexcept {
    static const std::string unknown = "?";
    if (index < 0 || index >= numTimbres()) return unknown;
    return m_impl->presets[static_cast<std::size_t>(index)].name;
}

std::shared_ptr<Loop> TimbreTransferEngine::transfer(std::shared_ptr<const Loop> source,
                                                     int timbreIndex) {
    if (!source || source->empty()) {
        throw std::runtime_error("TimbreTransferEngine: empty source loop");
    }
    const std::size_t n = source->length();
    const double engineSr = source->sampleRate();
    const float* srcData = source->data();

    const Stats srcStats = computeStats(srcData, n);
    LOG_INFO(fmtStats("TimbreTransfer: input", n, srcStats));
    LOG_INFO("TimbreTransfer: source bars=" + std::to_string(source->bars())
             + " bpm=" + std::to_string(source->bpm())
             + " sr=" + std::to_string(engineSr)
             + " dur_s=" + std::to_string(static_cast<double>(n) / engineSr));

    torch::NoGradGuard noGrad;

    const int nP = static_cast<int>(m_impl->presets.size());
    if (nP == 0) throw std::runtime_error("TimbreTransfer: no timbre presets loaded");
    if (timbreIndex < 0 || timbreIndex >= nP) {
        LOG_WARN("TimbreTransfer: timbreIndex " + std::to_string(timbreIndex)
                 + " out of range — using 0");
        timbreIndex = 0;
    }
    const auto& zsem = m_impl->timbreVectors[static_cast<std::size_t>(timbreIndex)];
    LOG_INFO("TimbreTransfer: timbre preset='"
             + m_impl->presets[static_cast<std::size_t>(timbreIndex)].name + "'");

    // ISMIR pipeline, clean (matches notebooks/audio_to_audio_demo.ipynb):
    //   1. normalise whole source to peak 1.0
    //   2. resample 48k -> 24k
    //   3. chunk into kXLength pieces (zero-pad last chunk's tail)
    //   4. per chunk: AE.encode -> CQT+encoder_s -> Heun sample -> AE.decode
    //   5. concat chunks
    //   6. resample 24k -> 48k
    //   7. copy to output Loop, trim to source length, hard-limit to [-1,1]
    // No peak-matching, no wrap-padding, no input-conditional scaling.

    // (1) normalise to peak 1.0 (matches the Python reference's `audio / audio.max()`).
    const auto tRs0 = std::chrono::steady_clock::now();
    auto srcTensor = torch::from_blob(const_cast<float*>(srcData),
                                       {static_cast<long>(n)},
                                       torch::kFloat32).clone();
    if (srcStats.peak > 0.0f) {
        srcTensor = srcTensor / srcStats.peak;
    }
    LOG_INFO("TimbreTransfer: normalised input by 1/" + std::to_string(srcStats.peak)
             + " (now peak=1.0)");

    // (2) resample to model SR.
    auto src24 = resample(srcTensor, engineSr, kModelSampleRate).to(m_impl->device);
    const auto tRs1 = std::chrono::steady_clock::now();
    if (shouldDump()) {
        writeWav(dumpDir() + "/01_source_48k.wav", srcTensor, engineSr);
        writeWav(dumpDir() + "/02_source_24k.wav", src24,     kModelSampleRate);
    }

    // (3) chunk. kXLength-sized pieces; last chunk's tail gets zero-padded.
    const long n24  = src24.size(0);
    const long numChunks = (n24 + kXLength - 1) / kXLength;
    const long paddedN   = numChunks * kXLength;
    if (paddedN != n24) {
        auto padded = torch::zeros({paddedN}, src24.options());
        padded.slice(0, 0, n24).copy_(src24);
        src24 = padded;
    }
    LOG_INFO("TimbreTransfer: chunking — n24=" + std::to_string(n24)
             + " -> " + std::to_string(numChunks) + " chunk(s) of "
             + std::to_string(kXLength)
             + " (zero-padded " + std::to_string(paddedN - n24) + " tail samples)");

    // (4) process each chunk. Log per-chunk input/output stats so chunking
    //     health is visible in the logs: we want to see every chunk's output
    //     RMS be non-trivial and roughly within an order of magnitude of the
    //     others. A silent/near-silent chunk is a chunking failure.
    const auto tInf0 = std::chrono::steady_clock::now();
    std::vector<torch::Tensor> outChunks;
    outChunks.reserve(static_cast<std::size_t>(numChunks));
    for (long c = 0; c < numChunks; ++c) {
        auto chunk = src24.slice(0, c * kXLength, (c + 1) * kXLength);
        const float inPeak = chunk.abs().max().item<float>();
        const float inRms  = chunk.pow(2).mean().sqrt().item<float>();

        const auto tC0 = std::chrono::steady_clock::now();
        auto decoded = m_impl->runChunk(chunk, zsem);
        const auto tC1 = std::chrono::steady_clock::now();

        const float outPeak = decoded.abs().max().item<float>();
        const float outRms  = decoded.pow(2).mean().sqrt().item<float>();
        const auto chunkMs = std::chrono::duration_cast<std::chrono::milliseconds>(tC1 - tC0).count();
        LOG_INFO("TimbreTransfer: chunk " + std::to_string(c + 1) + "/"
                 + std::to_string(numChunks)
                 + " in[peak=" + std::to_string(inPeak)  + " rms=" + std::to_string(inRms)  + "]"
                 + " out[peak=" + std::to_string(outPeak) + " rms=" + std::to_string(outRms) + "]"
                 + " in " + std::to_string(chunkMs) + " ms");

        outChunks.push_back(decoded);
    }
    auto full24 = torch::cat(outChunks, 0).to(torch::kCPU);
    const auto tInf1 = std::chrono::steady_clock::now();
    if (shouldDump()) {
        writeWav(dumpDir() + "/03_decoded_24k.wav", full24, kModelSampleRate);
    }

    // (5,6) resample 24k -> 48k.
    auto full48 = resample(full24, kModelSampleRate, engineSr);
    const auto tRs2 = std::chrono::steady_clock::now();
    if (shouldDump()) {
        writeWav(dumpDir() + "/04_final_48k.wav", full48, engineSr);
    }

    LOG_INFO("TimbreTransfer: resample_in="
             + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tRs1 - tRs0).count())
             + " ms, inference="
             + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tInf1 - tInf0).count())
             + " ms, resample_out="
             + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tRs2 - tInf1).count())
             + " ms");

    // (7) copy to destination Loop, trim to source length, hard-limit [-1,1].
    //     The model output can slightly exceed ±1.0 — we clamp rather than
    //     rescale so the output amplitude is whatever the model produced,
    //     with only extreme samples clipped. No peak-match, no scaling.
    auto dst = std::make_shared<Loop>(source->bars(), source->bpm(),
                                       source->sampleRate(), n);
    float* dstData = dst->data();
    full48 = full48.contiguous();
    const auto outLen = static_cast<std::size_t>(full48.size(0));
    const std::size_t copyN = (outLen < n) ? outLen : n;
    const float* outPtr = full48.data_ptr<float>();
    std::size_t clipCount = 0;
    for (std::size_t i = 0; i < copyN; ++i) {
        float x = outPtr[i];
        if (x >  1.0f) { x =  1.0f; ++clipCount; }
        if (x < -1.0f) { x = -1.0f; ++clipCount; }
        dstData[i] = x;
    }
    for (std::size_t i = copyN; i < n; ++i) dstData[i] = 0.0f;

    const Stats finalStats = computeStats(dstData, n);
    LOG_INFO(fmtStats("TimbreTransfer: final output", n, finalStats)
             + " clipped=" + std::to_string(clipCount));
    return dst;
}

}  // namespace loopa
