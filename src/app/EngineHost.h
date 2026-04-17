#pragma once

#include "core/LooperEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace loopa::app {

// Owns a LooperEngine and drives it from the JUCE AudioDeviceManager.
// Requests 48 kHz @ 128 samples and asks for input+output channels.
class EngineHost : public juce::AudioIODeviceCallback,
                   public juce::ChangeListener {
public:
    EngineHost();
    ~EngineHost() override;

    void start();
    void stop();

    LooperEngine& engine() noexcept { return m_engine; }
    const LooperEngine& engine() const noexcept { return m_engine; }

    juce::AudioDeviceManager& deviceManager() noexcept { return m_deviceManager; }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    juce::AudioDeviceManager m_deviceManager;
    LooperEngine m_engine;
    bool m_started = false;
};

}  // namespace loopa::app
