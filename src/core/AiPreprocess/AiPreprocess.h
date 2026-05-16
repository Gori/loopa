#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace loopa {

// Optional pre-processing applied to a loop's audio before it's handed to
// the timbre-transfer model. Three independent stages, all default off:
//   - denoise           : DeepFilterNet-style TorchScript denoiser
//   - vocalIsolate      : Demucs-style source separation, keep vocals stem
//   - loudnessNormalize : ITU-R BS.1770 integrated LUFS normalize to target
//
// All processing happens IN-PLACE on a mono float32 buffer. Stages run on
// the caller's thread (the TimbreTransferService background worker), so
// there is no real-time constraint here. Models are loaded lazily on first
// use of each stage; missing model files log a one-time WARN and the stage
// becomes a no-op for the lifetime of this AiPreprocessor (graceful
// degradation rather than failing the whole conversion).
class AiPreprocessor {
public:
    struct Options {
        bool  denoise           = false;
        bool  vocalIsolate      = false;
        bool  loudnessNormalize = false;
        float targetLufs        = -18.0f;  // BS.1770 integrated LUFS
    };

    // modelDir: directory containing optional model files
    //   denoise.preprocess.ts  (DeepFilterNet-style)
    //   vocal_isolate.ts       (Demucs-style)
    explicit AiPreprocessor(std::string modelDir);
    ~AiPreprocessor();

    AiPreprocessor(const AiPreprocessor&)            = delete;
    AiPreprocessor& operator=(const AiPreprocessor&) = delete;
    AiPreprocessor(AiPreprocessor&&)                 = delete;
    AiPreprocessor& operator=(AiPreprocessor&&)      = delete;

    // Process IN-PLACE. If every option is false, returns immediately with
    // the buffer untouched. Returns false only on unrecoverable error
    // (currently never — missing models / inference failures are logged
    // and treated as no-ops so conversion can still proceed).
    bool process(float* samples, std::size_t numSamples,
                 double sampleRate, const Options& opts);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace loopa
