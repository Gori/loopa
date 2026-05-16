#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace loopa::app {

class EngineBridge;

class SettingsComponent : public juce::Component,
                          public juce::ChangeListener,
                          private juce::Timer {
public:
    SettingsComponent(EngineBridge& bridge,
                      juce::AudioDeviceManager& deviceManager);
    ~SettingsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    void timerCallback() override;
    void refreshFromSettings();
    void refreshLatency();

    EngineBridge& m_bridge;
    juce::AudioDeviceManager& m_deviceManager;

    juce::Label m_title;

    juce::Label m_audioHeader;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> m_deviceSelector;

    juce::Label m_latencyHeader;
    juce::Label m_latencyInputLabel;
    juce::Label m_latencyInputValue;
    juce::Label m_latencyOutputLabel;
    juce::Label m_latencyOutputValue;
    juce::Label m_latencyBufferLabel;
    juce::Label m_latencyBufferValue;
    juce::Label m_latencyTotalLabel;
    juce::Label m_latencyTotalValue;

    juce::Label m_mixHeader;
    juce::Label m_masterLabel;
    juce::Slider m_masterSlider;
    juce::Label m_masterReadout;

    juce::Label m_metroHeader;
    juce::Label m_metroVolumeLabel;
    juce::Slider m_metroVolumeSlider;
    juce::Label m_metroVolumeReadout;
    juce::ToggleButton m_metroAutoToggle;

    juce::Label m_defaultsHeader;
    juce::Label m_defaultBarsLabel;
    juce::ComboBox m_defaultBarsCombo;

    juce::Label m_aiPreHeader;
    juce::ToggleButton m_aiPreDenoiseToggle;
    juce::ToggleButton m_aiPreVocalToggle;
    juce::ToggleButton m_aiPreLoudnessToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

}  // namespace loopa::app
