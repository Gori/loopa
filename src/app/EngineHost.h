#pragma once

#include "core/LooperEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace loopa::app {

class TimbreTransferService;

// Owns a LooperEngine and drives it from the JUCE AudioDeviceManager.
// Requests 48 kHz @ 128 samples and asks for input+output channels. Also
// owns the TimbreTransferService (background worker for neural audio style
// transfer); the service is destroyed before the engine so in-flight jobs
// are joined before their target data goes away.
class EngineHost : public juce::AudioIODeviceCallback,
                   public juce::ChangeListener {
public:
    EngineHost();
    ~EngineHost() override;

    void start();
    void stop();

    LooperEngine& engine() noexcept { return m_engine; }
    const LooperEngine& engine() const noexcept { return m_engine; }

    TimbreTransferService& timbreService() noexcept { return *m_timbreService; }

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
    // Order matters: m_timbreService is destroyed before m_engine so the
    // worker is joined before the engine's loops go away.
    juce::AudioDeviceManager m_deviceManager;
    LooperEngine m_engine;
    std::unique_ptr<TimbreTransferService> m_timbreService;
    bool m_started = false;
};

}  // namespace loopa::app
