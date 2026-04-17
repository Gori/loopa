#pragma once

#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace loopa::app {

enum class ChipIcon : std::uint8_t { None, SpeakerMute };

// Small segmented-control-style button: a rounded chip with a label and optional
// value. Switches its visual state among (inactive, active, hot). "Active" draws
// the chip filled with its accent colour; "hot" adds an extra fill (used while
// recording). A "selected" outline is drawn around the chip when `selected` is true.
class ChipButton : public juce::Component {
public:
    ChipButton();
    ~ChipButton() override = default;

    void setLabel(juce::String label);
    void setValue(juce::String value);
    void setAccentColour(juce::Colour c);     // applied when active or hot
    void setActive(bool active);
    void setHot(bool hot);
    void setSelected(bool selected);
    void setEnabled2(bool enabled);           // visual enabled (not juce::Component::setEnabled)
    void setOnClick(std::function<void()> onClick);
    void setIcon(ChipIcon i);                 // non-None icons replace the text render path
    void setDrawBackground(bool draw);        // turn off the rounded-rect fill (handy for icons)

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::String m_label;
    juce::String m_value;
    juce::Colour m_accent;
    bool m_active = false;
    bool m_hot = false;
    bool m_selected = false;
    bool m_enabled = true;
    bool m_drawBackground = true;
    ChipIcon m_icon = ChipIcon::None;
    std::function<void()> m_onClick;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipButton)
};

}  // namespace loopa::app
