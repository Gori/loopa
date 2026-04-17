#pragma once

#include "ChipButton.h"

#include "core/LooperEngine.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace loopa::app {

class EngineBridge;

class TopBar : public juce::Component {
public:
    explicit TopBar(EngineBridge& bridge);
    ~TopBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void update(const loopa::LooperEngine::Snapshot& s);

    void setOnSettingsClicked(std::function<void()> cb) { m_onSettings = std::move(cb); }

private:
    EngineBridge& m_bridge;

    juce::Label m_brand;
    juce::Label m_bpmLabel;
    juce::Label m_bpmValue;
    juce::Label m_barLabel;
    juce::Label m_barValue;
    ChipButton  m_play;
    ChipButton  m_rec;
    ChipButton  m_metro;
    ChipButton  m_settings;

    std::function<void()> m_onSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

}  // namespace loopa::app
