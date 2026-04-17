#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace loopa::app {

class EngineBridge;

class SettingsWindow : public juce::DocumentWindow {
public:
    SettingsWindow(EngineBridge& bridge, juce::AudioDeviceManager& deviceManager);
    ~SettingsWindow() override;

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsWindow)
};

}  // namespace loopa::app
