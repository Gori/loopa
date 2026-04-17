#pragma once

#include "core/LooperEngine.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

namespace loopa {
class LooperEngine;
}

namespace loopa::app {

// NSView-backed JUCE component that renders one track's waveform through a
// CAMetalLayer. Has NO internal timer — the owner (MainComponent) calls
// renderNow(snapshot) once per frame so every track presents on the same vsync.
class WaveformView : public juce::NSViewComponent {
public:
    WaveformView(loopa::LooperEngine& engine, int trackId);
    ~WaveformView() override;

    void resized() override;

    void renderNow(const loopa::LooperEngine::Snapshot& snapshot);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};

}  // namespace loopa::app
