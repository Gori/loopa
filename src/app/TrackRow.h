#pragma once

#include "ChipButton.h"
#include "WaveformView.h"

#include "core/LooperEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace loopa::app {

class EngineBridge;
class TrackRow;

// Bare big-digit track number that selects the row on click. No chip border.
class TrackNumber : public juce::Component {
public:
    TrackNumber(int trackId, std::function<void()> onClick);
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void setSelected(bool selected);

private:
    int m_trackId;
    bool m_selected = false;
    std::function<void()> m_onClick;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackNumber)
};

class TrackRow : public juce::Component {
public:
    TrackRow(EngineBridge& bridge, int trackId);
    ~TrackRow() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void update(const loopa::LooperEngine::Snapshot& s);

    int trackId() const noexcept { return m_trackId; }

private:
    void cycleBars();
    void cycleMode();
    void cycleInput();
    void toggleMute();

    EngineBridge& m_bridge;
    const int m_trackId;

    std::unique_ptr<TrackNumber> m_number;
    ChipButton m_loopChip;
    ChipButton m_input;
    ChipButton m_bars;
    ChipButton m_mode;
    ChipButton m_mute;   // icon

    std::unique_ptr<WaveformView> m_waveform;

    bool m_selected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackRow)
};

}  // namespace loopa::app
