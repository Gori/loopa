#pragma once

#include "EngineBridge.h"
#include "TopBar.h"
#include "TrackRow.h"

#include <array>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace loopa {
class LooperEngine;
}

namespace loopa::app {

class SettingsWindow;

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent(loopa::LooperEngine& engine, juce::AudioDeviceManager& deviceManager);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void openSettings();

    loopa::LooperEngine& m_engine;
    juce::AudioDeviceManager& m_deviceManager;
    EngineBridge m_bridge;

    std::unique_ptr<TopBar> m_topBar;
    std::array<std::unique_ptr<TrackRow>, loopa::LooperEngine::kNumTracks> m_rows;
    std::unique_ptr<SettingsWindow> m_settingsWindow;

    int m_lastLoggedBar = -1;
    bool m_metronomeAutoTriggered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

}  // namespace loopa::app
