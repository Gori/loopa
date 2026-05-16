#include "core/AiPreprocess/AiPreprocess.h"

#include "core/AudioWavDump.h"
#include "core/Logger.h"
#include "core/Timbre/AudioResample.h"

#include <torch/script.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace loopa {

namespace {

constexpr const char* kDenoiseFile      = "denoise.preprocess.ts";
constexpr const char* kVocalIsolateFile = "vocal_isolate.ts";

// Demucs's htdemucs operates at 44.1 kHz stereo. Resample to and from this
// rate around the model call when sr != 44100.
constexpr double kVocalModelSr = 44100.0;

// Set LOOPA_PREPROC_DUMP=/some/dir before launching to save WAV snapshots
// of each pre-processing stage (00_input, 01_after_denoise,
// 02_after_vocal_isolate, 03_after_loudness_normalize). Each conversion
// gets its own NN_ index so successive runs don't clobber each other.
constexpr const char* kDumpEnvVar = "LOOPA_PREPROC_DUMP";

const char* dumpDir() {
    static const char* d = std::getenv(kDumpEnvVar);
    return d;
}

void dumpStage(int conversionIx, int stageIx, const char* tag,
               const float* samples, std::size_t n, double sr) {
    const char* dir = dumpDir();
    if (!dir) return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d_%02d_%s.wav", conversionIx, stageIx, tag);
    writeWavMonoF32(std::string(dir) + "/" + buf, samples, n, sr);
}

// ITU-R BS.1770 gating thresholds.
constexpr double kAbsGateLufs   = -70.0;
constexpr double kRelGateOffset = -10.0;

// Direct-form-II-transposed biquad. Used for K-weighting in the LUFS
// integrator. Coefficients are normalized so a0 == 1.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
    double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// K-weighting filter coefficients per ITU-R BS.1770-4 Annex 1, derived from
// the analog reference via the bilinear transform pre-warped at `sr`. Two
// stages: a +4 dB high-frequency shelf at ~1.68 kHz and a 2nd-order
// high-pass at ~38 Hz (RLB).
void kWeightingCoeffs(double sr, Biquad& shelf, Biquad& hp) {
    {
        const double f0 = 1681.974450955533;
        const double G  = 3.999843853973347;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan(M_PI * f0 / sr);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f1 = 38.13547087602444;
        const double Q1 = 0.5003270373238773;
        const double K1 = std::tan(M_PI * f1 / sr);
        const double a0 = 1.0 + K1 / Q1 + K1 * K1;
        hp.b0 = 1.0 / a0;
        hp.b1 = -2.0 / a0;
        hp.b2 = 1.0 / a0;
        hp.a1 = 2.0 * (K1 * K1 - 1.0) / a0;
        hp.a2 = (1.0 - K1 / Q1 + K1 * K1) / a0;
    }
}

double meanSqToLufs(double meanSq) {
    if (meanSq <= 0.0) return -std::numeric_limits<double>::infinity();
    return -0.691 + 10.0 * std::log10(meanSq);
}

// ITU-R BS.1770-4 integrated loudness (mono). Returns -INFINITY when the
// signal is below the absolute gate or shorter than one 400 ms block.
double computeIntegratedLufs(const float* samples, std::size_t n, double sr) {
    if (n == 0 || sr <= 0.0) return -std::numeric_limits<double>::infinity();
    Biquad shelf{}, hp{};
    kWeightingCoeffs(sr, shelf, hp);

    std::vector<double> sq;
    sq.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        double y = static_cast<double>(samples[i]);
        y = shelf.process(y);
        y = hp.process(y);
        sq.push_back(y * y);
    }

    const std::size_t blockN = static_cast<std::size_t>(0.4 * sr);
    const std::size_t hopN   = static_cast<std::size_t>(0.1 * sr);
    if (blockN == 0 || hopN == 0) return -std::numeric_limits<double>::infinity();
    if (n < blockN) return -std::numeric_limits<double>::infinity();

    std::vector<double> blockMs;
    blockMs.reserve((n - blockN) / hopN + 1);
    for (std::size_t s = 0; s + blockN <= n; s += hopN) {
        double sum = 0.0;
        for (std::size_t i = s; i < s + blockN; ++i) sum += sq[i];
        blockMs.push_back(sum / static_cast<double>(blockN));
    }

    std::vector<double> gated;
    gated.reserve(blockMs.size());
    for (double ms : blockMs) {
        if (meanSqToLufs(ms) >= kAbsGateLufs) gated.push_back(ms);
    }
    if (gated.empty()) return -std::numeric_limits<double>::infinity();

    double meanGated = 0.0;
    for (double ms : gated) meanGated += ms;
    meanGated /= static_cast<double>(gated.size());
    const double relGate = meanSqToLufs(meanGated) + kRelGateOffset;

    double finalSum = 0.0;
    std::size_t finalN = 0;
    for (double ms : gated) {
        if (meanSqToLufs(ms) >= relGate) {
            finalSum += ms;
            ++finalN;
        }
    }
    if (finalN == 0) return -std::numeric_limits<double>::infinity();
    return meanSqToLufs(finalSum / static_cast<double>(finalN));
}

}  // namespace

struct AiPreprocessor::Impl {
    std::string modelDir;

    std::optional<torch::jit::script::Module> denoiseModel;
    std::optional<torch::jit::script::Module> vocalModel;
    bool denoiseTried    = false;
    bool denoiseDisabled = false;
    bool vocalTried      = false;
    bool vocalDisabled   = false;

    explicit Impl(std::string md) : modelDir(std::move(md)) {}

    bool ensureModel(const char* file,
                     std::optional<torch::jit::script::Module>& slot,
                     bool& tried, bool& disabled) {
        if (tried) return slot.has_value();
        tried = true;
        const auto path = modelDir + "/" + file;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            LOG_WARN(std::string("AiPreprocess: ") + file + " not found in "
                     + modelDir + " — stage will be skipped");
            disabled = true;
            return false;
        }
        try {
            auto m = torch::jit::load(path, torch::Device(torch::kCPU));
            m.eval();
            slot = std::move(m);
            LOG_INFO(std::string("AiPreprocess: loaded ") + file);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("AiPreprocess: failed to load ") + file
                      + ": " + e.what());
            disabled = true;
            return false;
        }
    }

    void runDenoise(float* samples, std::size_t n, double /*sr*/) {
        if (denoiseDisabled) return;
        if (!ensureModel(kDenoiseFile, denoiseModel, denoiseTried, denoiseDisabled)) return;
        torch::NoGradGuard noGrad;
        try {
            torch::Tensor inT = torch::from_blob(
                samples, {static_cast<long>(n)}, torch::kFloat32).clone();
            inT = inT.unsqueeze(0);  // [1, N]
            auto outIv = denoiseModel->forward({inT});
            auto outT = outIv.toTensor().to(torch::kCPU).to(torch::kFloat32).contiguous();
            if (outT.dim() == 2) outT = outT.squeeze(0);
            const long outN  = outT.size(0);
            const long copyN = std::min<long>(outN, static_cast<long>(n));
            std::memcpy(samples, outT.data_ptr<float>(),
                        static_cast<std::size_t>(copyN) * sizeof(float));
            if (static_cast<std::size_t>(copyN) < n) {
                std::memset(samples + copyN, 0, (n - static_cast<std::size_t>(copyN)) * sizeof(float));
            }
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("AiPreprocess: denoise inference failed: ") + e.what());
        }
    }

    void runVocalIsolate(float* samples, std::size_t n, double sr) {
        if (vocalDisabled) return;
        if (!ensureModel(kVocalIsolateFile, vocalModel, vocalTried, vocalDisabled)) return;
        torch::NoGradGuard noGrad;
        try {
            torch::Tensor mono48 = torch::from_blob(
                samples, {static_cast<long>(n)}, torch::kFloat32).clone();
            torch::Tensor mono44 = (std::abs(sr - kVocalModelSr) < 1e-6)
                ? mono48
                : loopa::resampleMono(mono48, sr, kVocalModelSr);

            // Stereo dup: [2, M], add batch -> [1, 2, M].
            torch::Tensor stereo = torch::stack({mono44, mono44}, 0).unsqueeze(0);

            auto outIv = vocalModel->forward({stereo});
            torch::Tensor outT = outIv.toTensor().to(torch::kCPU).to(torch::kFloat32).contiguous();

            // Demucs htdemucs returns [B, S, C, M] with sources ordered
            // [drums, bass, other, vocals] — vocals is the last stem.
            // Tolerate a few common shapes from custom exports.
            torch::Tensor vocals;
            if (outT.dim() == 4) {
                vocals = outT.select(0, 0).select(0, outT.size(1) - 1);
            } else if (outT.dim() == 3) {
                vocals = outT.select(0, outT.size(0) - 1);
            } else if (outT.dim() == 2) {
                vocals = outT;
            } else {
                LOG_ERROR("AiPreprocess: unexpected vocal isolate output dim "
                          + std::to_string(outT.dim()));
                return;
            }
            torch::Tensor vocMono44 = vocals.dim() == 2 ? vocals.mean(0) : vocals;
            torch::Tensor vocMono48 = (std::abs(sr - kVocalModelSr) < 1e-6)
                ? vocMono44
                : loopa::resampleMono(vocMono44, kVocalModelSr, sr);

            const long outN  = vocMono48.size(0);
            const long copyN = std::min<long>(outN, static_cast<long>(n));
            std::memcpy(samples, vocMono48.data_ptr<float>(),
                        static_cast<std::size_t>(copyN) * sizeof(float));
            if (static_cast<std::size_t>(copyN) < n) {
                std::memset(samples + copyN, 0, (n - static_cast<std::size_t>(copyN)) * sizeof(float));
            }
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("AiPreprocess: vocal isolate inference failed: ") + e.what());
        }
    }

    void runLoudnessNormalize(float* samples, std::size_t n, double sr, float targetLufs) {
        const double measured = computeIntegratedLufs(samples, n, sr);
        if (!std::isfinite(measured)) {
            LOG_INFO("AiPreprocess: LUFS gate empty (silence/too short) — skip normalize");
            return;
        }
        const double gainDb = static_cast<double>(targetLufs) - measured;
        const double gain   = std::pow(10.0, gainDb / 20.0);
        LOG_INFO("AiPreprocess: LUFS measured=" + std::to_string(measured)
                 + " target=" + std::to_string(targetLufs)
                 + " gain=" + std::to_string(gainDb) + " dB");
        for (std::size_t i = 0; i < n; ++i) {
            samples[i] = static_cast<float>(static_cast<double>(samples[i]) * gain);
        }
    }
};

AiPreprocessor::AiPreprocessor(std::string modelDir)
    : m_impl(std::make_unique<Impl>(std::move(modelDir))) {}

AiPreprocessor::~AiPreprocessor() = default;

bool AiPreprocessor::process(float* samples, std::size_t numSamples,
                              double sampleRate, const Options& opts) {
    if (!samples || numSamples == 0) return true;
    if (!opts.denoise && !opts.vocalIsolate && !opts.loudnessNormalize) return true;

    auto stamp = [](const auto& a, const auto& b) {
        return std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
    };

    static std::atomic<int> sConversionCounter{0};
    const int conversionIx = ++sConversionCounter;
    if (dumpDir()) {
        LOG_INFO(std::string("AiPreprocess: ") + kDumpEnvVar + "="
                 + dumpDir() + " — dumping per-stage WAVs");
        dumpStage(conversionIx, 0, "input", samples, numSamples, sampleRate);
    }

    if (opts.denoise) {
        const auto t0 = std::chrono::steady_clock::now();
        m_impl->runDenoise(samples, numSamples, sampleRate);
        const auto t1 = std::chrono::steady_clock::now();
        LOG_INFO("AiPreprocess: denoise " + stamp(t0, t1) + " ms");
        dumpStage(conversionIx, 1, "after_denoise", samples, numSamples, sampleRate);
    }
    if (opts.vocalIsolate) {
        const auto t0 = std::chrono::steady_clock::now();
        m_impl->runVocalIsolate(samples, numSamples, sampleRate);
        const auto t1 = std::chrono::steady_clock::now();
        LOG_INFO("AiPreprocess: vocal isolate " + stamp(t0, t1) + " ms");
        dumpStage(conversionIx, 2, "after_vocal_isolate", samples, numSamples, sampleRate);
    }
    if (opts.loudnessNormalize) {
        const auto t0 = std::chrono::steady_clock::now();
        m_impl->runLoudnessNormalize(samples, numSamples, sampleRate, opts.targetLufs);
        const auto t1 = std::chrono::steady_clock::now();
        LOG_INFO("AiPreprocess: LUFS normalize " + stamp(t0, t1) + " ms");
        dumpStage(conversionIx, 3, "after_loudness_normalize", samples, numSamples, sampleRate);
    }
    return true;
}

}  // namespace loopa
