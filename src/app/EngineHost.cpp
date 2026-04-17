#include "EngineHost.h"

#include "Settings.h"

#include "core/Logger.h"

#include <string>

namespace loopa::app {

EngineHost::EngineHost() = default;

EngineHost::~EngineHost() {
    stop();
}

void EngineHost::start() {
    if (m_started) {
        return;
    }

    // Try to restore a previous device state.
    std::unique_ptr<juce::XmlElement> savedXml;
    const auto& saved = Settings::instance().data().audioDeviceStateXml;
    if (saved.isNotEmpty()) {
        savedXml = juce::parseXML(saved);
    }

    const juce::String initError = m_deviceManager.initialise(2, 2, savedXml.get(), true);
    if (initError.isNotEmpty()) {
        LOG_ERROR(std::string("AudioDeviceManager init failed: ") + initError.toStdString());
        return;
    }

    // If no saved state, nudge toward 48 kHz / 128 samples as the default.
    if (savedXml == nullptr) {
        auto setup = m_deviceManager.getAudioDeviceSetup();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 128;
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;
        const juce::String setupError = m_deviceManager.setAudioDeviceSetup(setup, true);
        if (setupError.isNotEmpty()) {
            LOG_WARN(std::string("Audio setup adjusted: ") + setupError.toStdString());
        }
    }

    const auto applied = m_deviceManager.getAudioDeviceSetup();
    LOG_INFO("Audio device: " + applied.outputDeviceName.toStdString()
             + " @ " + std::to_string(applied.sampleRate) + " Hz, "
             + std::to_string(applied.bufferSize) + " samples");

    m_deviceManager.addAudioCallback(this);
    m_deviceManager.addChangeListener(this);

    // Persist the current state so the first launch writes defaults too.
    if (auto xml = m_deviceManager.createStateXml()) {
        Settings::instance().setAudioDeviceStateXml(xml->toString());
    }

    m_started = true;
}

void EngineHost::stop() {
    if (!m_started) {
        return;
    }
    m_deviceManager.removeChangeListener(this);
    m_deviceManager.removeAudioCallback(this);
    m_deviceManager.closeAudioDevice();
    m_started = false;
}

void EngineHost::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    const double sr = device->getCurrentSampleRate();
    const int    bs = device->getCurrentBufferSizeSamples();
    const int    inLat  = device->getInputLatencyInSamples();
    const int    outLat = device->getOutputLatencyInSamples();
    const double inMs   = (sr > 0.0) ? (1000.0 * inLat  / sr) : 0.0;
    const double outMs  = (sr > 0.0) ? (1000.0 * outLat / sr) : 0.0;
    const double bufMs  = (sr > 0.0) ? (1000.0 * bs     / sr) : 0.0;
    const double rtMs   = inMs + outMs + bufMs;

    m_engine.prepareToPlay(sr, bs);
    LOG_INFO("audioDeviceAboutToStart @ " + std::to_string(sr)
             + " Hz, " + std::to_string(bs) + " samples ("
             + std::to_string(bufMs) + " ms)"
             + "; input latency " + std::to_string(inLat) + " smp ("
             + std::to_string(inMs) + " ms)"
             + "; output latency " + std::to_string(outLat) + " smp ("
             + std::to_string(outMs) + " ms)"
             + "; total round-trip ~" + std::to_string(rtMs) + " ms");

    // Publish the compensation to the engine so subsequent recordings align to
    // the user's perceived beat rather than the raw capture timestamps.
    const int comp = std::max(0, inLat + outLat);
    loopa::EngineCommand c{};
    c.kind   = loopa::CommandKind::SetLatencyCompensation;
    c.intArg = comp;
    m_engine.postCommand(c);
    LOG_INFO("SetLatencyCompensation = " + std::to_string(comp) + " samples");
}

void EngineHost::audioDeviceStopped() {
    m_engine.releaseResources();
    LOG_INFO("audioDeviceStopped");
}

void EngineHost::audioDeviceError(const juce::String& errorMessage) {
    LOG_ERROR("audioDeviceError: " + errorMessage.toStdString());
}

void EngineHost::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                  int numInputChannels,
                                                  float* const* outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&) {
    m_engine.processBlock(inputChannelData, numInputChannels,
                          outputChannelData, numOutputChannels,
                          numSamples);
}

void EngineHost::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &m_deviceManager) {
        if (auto xml = m_deviceManager.createStateXml()) {
            Settings::instance().setAudioDeviceStateXml(xml->toString());
        }
    }
}

}  // namespace loopa::app
