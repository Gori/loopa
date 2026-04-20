#pragma once

#include "core/Loop.h"

#include <memory>
#include <string>
#include <vector>

namespace loopa {

// Named timbre preset (used to label the UI menu). `name` is displayed to
// the user; `vectorPath` is an absolute path to a .pt file containing the
// 32-dim timbre tensor produced by the ISMIR model's encoder.
struct TimbrePreset {
    std::string name;
    std::string vectorPath;
};

// Runs the ISMIR 2024 "Combining audio control and style transfer using
// latent diffusion" model for audio-to-audio timbre transfer. Five .ts
// primitives are called from C++, and the Heun diffusion sampling loop
// runs natively in this class (not baked into the .ts) so we can tune
// step count and guidance without re-exporting.
class TimbreTransferEngine {
public:
    // modelDir: directory containing ae.ts, enc_t.ts, enc_s.ts,
    //           time_xform.ts, denoise.ts — the five model primitives.
    // presets:  list of instrument presets. Each preset's `vectorPath`
    //           points to a saved [32]-dim torch tensor.
    TimbreTransferEngine(const std::string& modelDir,
                         const std::vector<TimbrePreset>& presets);
    ~TimbreTransferEngine();

    TimbreTransferEngine(const TimbreTransferEngine&) = delete;
    TimbreTransferEngine& operator=(const TimbreTransferEngine&) = delete;
    TimbreTransferEngine(TimbreTransferEngine&&) = delete;
    TimbreTransferEngine& operator=(TimbreTransferEngine&&) = delete;

    int numTimbres() const noexcept;
    const std::string& timbreName(int index) const noexcept;

    // Run the full pipeline. Returns a new Loop with the source's
    // bars/bpm/sampleRate/length. Throws on inference failure.
    std::shared_ptr<Loop> transfer(std::shared_ptr<const Loop> source,
                                    int timbreIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace loopa
