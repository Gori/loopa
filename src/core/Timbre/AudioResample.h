#pragma once

#include <torch/torch.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace loopa {

// Kaiser-windowed sinc polyphase resampler for a 1D mono float tensor.
// Cutoff is set to 0.475 * min(srcSr, dstSr) so we have a transition band
// up to Nyquist. Same implementation that LoopaTimbre uses for 48k <-> 24k
// — factored here so LoopaAiPreprocess can reuse it for 48k <-> 44.1k
// without duplication.
inline torch::Tensor resampleMono(torch::Tensor audio1d, double srcSr, double dstSr) {
    if (std::abs(srcSr - dstSr) < 1e-6) return audio1d;
    const int64_t srcLen = audio1d.size(-1);
    const int64_t dstLen = static_cast<int64_t>(
        std::llround(static_cast<double>(srcLen) * dstSr / srcSr));

    constexpr int zeroCrossings = 32;
    const double fCut = 0.475 * std::min(srcSr, dstSr);
    const double fsHigher = std::max(srcSr, dstSr);
    const int tapsPerSide = static_cast<int>(
        std::ceil(zeroCrossings * fsHigher / fCut));

    auto fp32 = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto src = audio1d.to(torch::kCPU).to(torch::kFloat32).contiguous();
    auto out = torch::zeros({dstLen}, fp32);
    const float* sp = src.data_ptr<float>();
    float* op = out.data_ptr<float>();
    const double ratio = srcSr / dstSr;
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
    const double gain = std::min(1.0, dstSr / srcSr);
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

}  // namespace loopa
