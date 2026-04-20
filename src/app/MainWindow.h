#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace loopa {
class LooperEngine;
}

namespace loopa::app {

class TimbreTransferService;

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(const juce::String& name,
               juce::Colour backgroundColour,
               int buttonsNeeded,
               loopa::LooperEngine& engine,
               juce::AudioDeviceManager& deviceManager,
               TimbreTransferService& timbre);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

}  // namespace loopa::app
