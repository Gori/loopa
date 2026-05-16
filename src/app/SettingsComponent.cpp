#include "SettingsComponent.h"

#include "EngineBridge.h"
#include "Settings.h"
#include "Theme.h"

namespace loopa::app {

namespace {

void styleHeader(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(theme::fontLabel());
    l.setColour(juce::Label::textColourId, theme::col(theme::kTextDim));
    l.setJustificationType(juce::Justification::centredLeft);
}

void styleLabel(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(theme::fontBody());
    l.setColour(juce::Label::textColourId, theme::col(theme::kText));
    l.setJustificationType(juce::Justification::centredLeft);
}

void styleReadout(juce::Label& l) {
    l.setFont(theme::fontNumeric());
    l.setColour(juce::Label::textColourId, theme::col(theme::kText));
    l.setJustificationType(juce::Justification::centredRight);
}

}  // namespace

SettingsComponent::SettingsComponent(EngineBridge& bridge,
                                     juce::AudioDeviceManager& deviceManager)
    : m_bridge(bridge), m_deviceManager(deviceManager) {
    // Title
    m_title.setText("SETTINGS", juce::dontSendNotification);
    m_title.setFont(theme::fontLarge().withHeight(18.0f));
    m_title.setColour(juce::Label::textColourId, theme::col(theme::kText));
    addAndMakeVisible(m_title);

    // Audio
    styleHeader(m_audioHeader, "AUDIO");
    addAndMakeVisible(m_audioHeader);

    m_deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager,
        0, 2,      // min / max input channels
        0, 2,      // min / max output channels
        false,     // showMidiInputOptions
        false,     // showMidiOutputSelector
        true,      // showChannelsAsStereoPairs
        false);    // hideAdvancedOptionsWithButton
    addAndMakeVisible(*m_deviceSelector);

    // Latency readout
    styleHeader(m_latencyHeader, "LATENCY");
    addAndMakeVisible(m_latencyHeader);
    styleLabel(m_latencyInputLabel,  "Input");
    styleLabel(m_latencyOutputLabel, "Output");
    styleLabel(m_latencyBufferLabel, "Buffer");
    styleLabel(m_latencyTotalLabel,  "Round-trip");
    styleReadout(m_latencyInputValue);
    styleReadout(m_latencyOutputValue);
    styleReadout(m_latencyBufferValue);
    styleReadout(m_latencyTotalValue);
    addAndMakeVisible(m_latencyInputLabel);
    addAndMakeVisible(m_latencyInputValue);
    addAndMakeVisible(m_latencyOutputLabel);
    addAndMakeVisible(m_latencyOutputValue);
    addAndMakeVisible(m_latencyBufferLabel);
    addAndMakeVisible(m_latencyBufferValue);
    addAndMakeVisible(m_latencyTotalLabel);
    addAndMakeVisible(m_latencyTotalValue);

    m_deviceManager.addChangeListener(this);
    refreshLatency();
    startTimerHz(4);  // keep readout live in case the device updates mid-play

    // Mix
    styleHeader(m_mixHeader, "MIX");
    addAndMakeVisible(m_mixHeader);

    styleLabel(m_masterLabel, "Master output");
    addAndMakeVisible(m_masterLabel);

    m_masterSlider.setRange(0.0, 1.0, 0.001);
    m_masterSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_masterSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    m_masterSlider.setColour(juce::Slider::trackColourId, theme::col(theme::kAccent));
    m_masterSlider.setColour(juce::Slider::backgroundColourId, theme::col(theme::kBg2));
    m_masterSlider.onValueChange = [this] {
        const auto v = static_cast<float>(m_masterSlider.getValue());
        Settings::instance().setMasterGain(v);
        m_bridge.setMasterGain(v);
        m_masterReadout.setText(juce::String(juce::roundToInt(v * 100.0f)) + " %",
                                juce::dontSendNotification);
    };
    addAndMakeVisible(m_masterSlider);

    styleReadout(m_masterReadout);
    addAndMakeVisible(m_masterReadout);

    // Metronome
    styleHeader(m_metroHeader, "METRONOME");
    addAndMakeVisible(m_metroHeader);

    styleLabel(m_metroVolumeLabel, "Volume");
    addAndMakeVisible(m_metroVolumeLabel);

    m_metroVolumeSlider.setRange(0.0, 1.0, 0.001);
    m_metroVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_metroVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    m_metroVolumeSlider.setColour(juce::Slider::trackColourId, theme::col(theme::kAccent));
    m_metroVolumeSlider.setColour(juce::Slider::backgroundColourId, theme::col(theme::kBg2));
    m_metroVolumeSlider.onValueChange = [this] {
        const auto v = static_cast<float>(m_metroVolumeSlider.getValue());
        Settings::instance().setMetronomeVolume(v);
        m_bridge.setMetronomeVolume(v);
        m_metroVolumeReadout.setText(juce::String(juce::roundToInt(v * 100.0f)) + " %",
                                     juce::dontSendNotification);
    };
    addAndMakeVisible(m_metroVolumeSlider);

    styleReadout(m_metroVolumeReadout);
    addAndMakeVisible(m_metroVolumeReadout);

    m_metroAutoToggle.setButtonText("Auto-enable after first recording");
    m_metroAutoToggle.setColour(juce::ToggleButton::textColourId, theme::col(theme::kText));
    m_metroAutoToggle.setColour(juce::ToggleButton::tickColourId, theme::col(theme::kAccent));
    m_metroAutoToggle.onClick = [this] {
        Settings::instance().setMetronomeAutoOnAfterFirstRecord(m_metroAutoToggle.getToggleState());
    };
    addAndMakeVisible(m_metroAutoToggle);

    // Defaults
    styleHeader(m_defaultsHeader, "DEFAULTS");
    addAndMakeVisible(m_defaultsHeader);

    styleLabel(m_defaultBarsLabel, "Default bars for new tracks");
    addAndMakeVisible(m_defaultBarsLabel);

    for (int b : {1, 2, 4, 8, 16}) {
        m_defaultBarsCombo.addItem(juce::String(b), b);
    }
    m_defaultBarsCombo.setColour(juce::ComboBox::backgroundColourId, theme::col(theme::kBg2));
    m_defaultBarsCombo.setColour(juce::ComboBox::textColourId, theme::col(theme::kText));
    m_defaultBarsCombo.setColour(juce::ComboBox::outlineColourId, theme::col(theme::kLine));
    m_defaultBarsCombo.onChange = [this] {
        const int id = m_defaultBarsCombo.getSelectedId();
        if (id > 0) {
            Settings::instance().setDefaultBarsForNewTracks(id);
        }
    };
    addAndMakeVisible(m_defaultBarsCombo);

    // AI Pre-Processing
    styleHeader(m_aiPreHeader, "AI PRE-PROCESSING");
    addAndMakeVisible(m_aiPreHeader);

    auto styleToggle = [](juce::ToggleButton& b, const juce::String& text) {
        b.setButtonText(text);
        b.setColour(juce::ToggleButton::textColourId, theme::col(theme::kText));
        b.setColour(juce::ToggleButton::tickColourId, theme::col(theme::kAccent));
    };
    styleToggle(m_aiPreDenoiseToggle, "Remove background noise");
    m_aiPreDenoiseToggle.onClick = [this] {
        Settings::instance().setAiPreDenoise(m_aiPreDenoiseToggle.getToggleState());
    };
    addAndMakeVisible(m_aiPreDenoiseToggle);

    styleToggle(m_aiPreVocalToggle, "Isolate vocals (remove accompaniment)");
    m_aiPreVocalToggle.onClick = [this] {
        Settings::instance().setAiPreVocalIsolate(m_aiPreVocalToggle.getToggleState());
    };
    addAndMakeVisible(m_aiPreVocalToggle);

    styleToggle(m_aiPreLoudnessToggle, "Normalize loudness (LUFS)");
    m_aiPreLoudnessToggle.onClick = [this] {
        Settings::instance().setAiPreLoudnessNormalize(m_aiPreLoudnessToggle.getToggleState());
    };
    addAndMakeVisible(m_aiPreLoudnessToggle);

    refreshFromSettings();

    // Fixed preferred size — the parent SettingsWindow wraps this in a
    // juce::Viewport that scrolls vertically when the window is shorter
    // than this height.
    setSize(560, 820);
}

SettingsComponent::~SettingsComponent() {
    stopTimer();
    m_deviceManager.removeChangeListener(this);
}

void SettingsComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &m_deviceManager) {
        refreshLatency();
    }
}

void SettingsComponent::timerCallback() {
    refreshLatency();
}

void SettingsComponent::refreshLatency() {
    auto* dev = m_deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) {
        m_latencyInputValue.setText("-- ms", juce::dontSendNotification);
        m_latencyOutputValue.setText("-- ms", juce::dontSendNotification);
        m_latencyBufferValue.setText("-- ms", juce::dontSendNotification);
        m_latencyTotalValue.setText("-- ms", juce::dontSendNotification);
        return;
    }
    const double sr = dev->getCurrentSampleRate();
    if (sr <= 0.0) return;
    const int    bs     = dev->getCurrentBufferSizeSamples();
    const int    inLat  = dev->getInputLatencyInSamples();
    const int    outLat = dev->getOutputLatencyInSamples();
    const double inMs   = 1000.0 * inLat  / sr;
    const double outMs  = 1000.0 * outLat / sr;
    const double bufMs  = 1000.0 * bs     / sr;
    const double totMs  = inMs + outMs + bufMs;

    auto fmt = [](double ms) {
        return juce::String::formatted("%.1f ms", ms);
    };
    m_latencyInputValue.setText(fmt(inMs),   juce::dontSendNotification);
    m_latencyOutputValue.setText(fmt(outMs), juce::dontSendNotification);
    m_latencyBufferValue.setText(fmt(bufMs), juce::dontSendNotification);
    m_latencyTotalValue.setText(fmt(totMs),  juce::dontSendNotification);
}

void SettingsComponent::refreshFromSettings() {
    const auto& d = Settings::instance().data();
    m_masterSlider.setValue(d.masterGain, juce::dontSendNotification);
    m_masterReadout.setText(juce::String(juce::roundToInt(d.masterGain * 100.0f)) + " %",
                            juce::dontSendNotification);
    m_metroVolumeSlider.setValue(d.metronomeVolume, juce::dontSendNotification);
    m_metroVolumeReadout.setText(juce::String(juce::roundToInt(d.metronomeVolume * 100.0f)) + " %",
                                 juce::dontSendNotification);
    m_metroAutoToggle.setToggleState(d.metronomeAutoOnAfterFirstRecord,
                                     juce::dontSendNotification);
    m_defaultBarsCombo.setSelectedId(d.defaultBarsForNewTracks, juce::dontSendNotification);
    m_aiPreDenoiseToggle.setToggleState(d.aiPreDenoise, juce::dontSendNotification);
    m_aiPreVocalToggle.setToggleState(d.aiPreVocalIsolate, juce::dontSendNotification);
    m_aiPreLoudnessToggle.setToggleState(d.aiPreLoudnessNormalize, juce::dontSendNotification);
}

void SettingsComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::col(theme::kBg1));

    g.setColour(theme::col(theme::kLine));
    auto drawSep = [&](int y) { g.fillRect(theme::kPadX, y, getWidth() - 2 * theme::kPadX, 1); };
    drawSep(m_audioHeader.getY() - 6);
    drawSep(m_latencyHeader.getY() - 6);
    drawSep(m_mixHeader.getY() - 6);
    drawSep(m_metroHeader.getY() - 6);
    drawSep(m_defaultsHeader.getY() - 6);
    drawSep(m_aiPreHeader.getY() - 6);
}

void SettingsComponent::resized() {
    auto area = getLocalBounds().reduced(theme::kPadX, theme::kPadY);

    m_title.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    // Audio
    m_audioHeader.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);
    m_deviceSelector->setBounds(area.removeFromTop(280));
    area.removeFromTop(10);

    // Latency
    m_latencyHeader.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);
    auto latencyRow = [&](juce::Label& l, juce::Label& v) {
        auto row = area.removeFromTop(20);
        l.setBounds(row.removeFromLeft(140));
        v.setBounds(row.removeFromLeft(100));
    };
    latencyRow(m_latencyInputLabel,  m_latencyInputValue);
    latencyRow(m_latencyOutputLabel, m_latencyOutputValue);
    latencyRow(m_latencyBufferLabel, m_latencyBufferValue);
    latencyRow(m_latencyTotalLabel,  m_latencyTotalValue);
    area.removeFromTop(10);

    // Mix
    m_mixHeader.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);
    {
        auto row = area.removeFromTop(24);
        m_masterLabel.setBounds(row.removeFromLeft(140));
        m_masterReadout.setBounds(row.removeFromRight(56));
        row.removeFromLeft(8);
        m_masterSlider.setBounds(row);
    }
    area.removeFromTop(10);

    // Metronome
    m_metroHeader.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);
    {
        auto row = area.removeFromTop(24);
        m_metroVolumeLabel.setBounds(row.removeFromLeft(140));
        m_metroVolumeReadout.setBounds(row.removeFromRight(56));
        row.removeFromLeft(8);
        m_metroVolumeSlider.setBounds(row);
    }
    area.removeFromTop(6);
    m_metroAutoToggle.setBounds(area.removeFromTop(24));
    area.removeFromTop(10);

    // Defaults
    m_defaultsHeader.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);
    {
        auto row = area.removeFromTop(28);
        m_defaultBarsLabel.setBounds(row.removeFromLeft(220));
        m_defaultBarsCombo.setBounds(row.removeFromLeft(96));
    }
    area.removeFromTop(10);

    // AI Pre-Processing
    m_aiPreHeader.setBounds(area.removeFromTop(16));
    area.removeFromTop(6);
    m_aiPreDenoiseToggle.setBounds(area.removeFromTop(24));
    area.removeFromTop(2);
    m_aiPreVocalToggle.setBounds(area.removeFromTop(24));
    area.removeFromTop(2);
    m_aiPreLoudnessToggle.setBounds(area.removeFromTop(24));
}

}  // namespace loopa::app
