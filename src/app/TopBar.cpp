#include "TopBar.h"

#include "EngineBridge.h"
#include "Theme.h"

#include <cmath>

namespace loopa::app {

TopBar::TopBar(EngineBridge& bridge) : m_bridge(bridge) {
    m_brand.setText("LOOPA", juce::dontSendNotification);
    m_brand.setFont(theme::fontBody().withHeight(13.0f));
    m_brand.setColour(juce::Label::textColourId, theme::col(theme::kText));
    m_brand.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_brand);

    m_bpmLabel.setText("BPM", juce::dontSendNotification);
    m_bpmLabel.setFont(theme::fontLabel());
    m_bpmLabel.setColour(juce::Label::textColourId, theme::col(theme::kTextDim));
    m_bpmLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_bpmLabel);

    m_bpmValue.setText("--.--", juce::dontSendNotification);
    m_bpmValue.setFont(theme::fontLarge());
    m_bpmValue.setColour(juce::Label::textColourId, theme::col(theme::kText));
    m_bpmValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_bpmValue);

    m_barLabel.setText("BAR", juce::dontSendNotification);
    m_barLabel.setFont(theme::fontLabel());
    m_barLabel.setColour(juce::Label::textColourId, theme::col(theme::kTextDim));
    m_barLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_barLabel);

    m_barValue.setText("-- / --", juce::dontSendNotification);
    m_barValue.setFont(theme::fontNumeric());
    m_barValue.setColour(juce::Label::textColourId, theme::col(theme::kText));
    m_barValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_barValue);

    m_play.setLabel("PLAY");
    m_play.setAccentColour(theme::col(theme::kPlay));
    m_play.setOnClick([this] { m_bridge.togglePlay(); });
    addAndMakeVisible(m_play);

    m_rec.setLabel("REC");
    m_rec.setAccentColour(theme::col(theme::kRecord));
    m_rec.setOnClick([this] {
        const auto s = m_bridge.snapshot();
        bool anyRecording = false;
        for (int i = 0; i < loopa::LooperEngine::kNumTracks; ++i) {
            if (s.trackRecordState[static_cast<std::size_t>(i)]
                == static_cast<int>(loopa::RecordState::Recording)) {
                anyRecording = true;
                break;
            }
            if (s.trackRecordState[static_cast<std::size_t>(i)]
                == static_cast<int>(loopa::RecordState::CountIn)) {
                anyRecording = true;
                break;
            }
        }
        if (anyRecording) {
            m_bridge.stopRecord();
        } else {
            m_bridge.startRecord();
        }
    });
    addAndMakeVisible(m_rec);

    m_metro.setLabel("METRO");
    m_metro.setAccentColour(theme::col(theme::kAccent));
    m_metro.setOnClick([this] {
        const auto s = m_bridge.snapshot();
        m_bridge.setMetronome(!s.metronomeEnabled);
    });
    addAndMakeVisible(m_metro);

    m_settings.setLabel("SETTINGS");
    m_settings.setAccentColour(theme::col(theme::kAccent));
    m_settings.setOnClick([this] { if (m_onSettings) m_onSettings(); });
    addAndMakeVisible(m_settings);
}

void TopBar::paint(juce::Graphics& g) {
    g.fillAll(theme::col(theme::kBg1));
    g.setColour(theme::col(theme::kLine));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void TopBar::resized() {
    auto area = getLocalBounds().reduced(theme::kPadX, theme::kPadY);

    m_brand.setBounds(area.removeFromLeft(70));
    area.removeFromLeft(16);

    m_bpmLabel.setBounds(area.removeFromLeft(34));
    m_bpmValue.setBounds(area.removeFromLeft(90));
    area.removeFromLeft(16);

    m_barLabel.setBounds(area.removeFromLeft(30));
    m_barValue.setBounds(area.removeFromLeft(150));
    area.removeFromLeft(16);

    const int chipW = 72;
    const int chipH = 28;
    const int gap = theme::kChipGap;

    auto chipBounds = [&](juce::Rectangle<int>& a) {
        auto r = a.removeFromLeft(chipW);
        r = r.withSizeKeepingCentre(chipW, chipH);
        a.removeFromLeft(gap);
        return r;
    };

    m_play.setBounds(chipBounds(area));
    m_rec.setBounds(chipBounds(area));
    m_metro.setBounds(chipBounds(area));

    // Settings chip right-aligned.
    const int settingsW = 92;
    auto right = getLocalBounds().reduced(theme::kPadX, theme::kPadY);
    auto rbounds = right.removeFromRight(settingsW).withSizeKeepingCentre(settingsW, chipH);
    m_settings.setBounds(rbounds);
}

void TopBar::update(const loopa::LooperEngine::Snapshot& s) {
    if (s.bpm > 0.0) {
        m_bpmValue.setText(juce::String(s.bpm, 2), juce::dontSendNotification);
    } else {
        m_bpmValue.setText("--.--", juce::dontSendNotification);
    }

    if (s.totalBars > 0 && s.beatsPerBar > 0 && s.totalLengthSamples > 0) {
        const double samplesPerBeat =
            static_cast<double>(s.totalLengthSamples) /
            (static_cast<double>(s.totalBars) * static_cast<double>(s.beatsPerBar));
        const double samplesInBeat = (samplesPerBeat > 0.0)
            ? std::fmod(static_cast<double>(s.samplePosition), samplesPerBeat) : 0.0;
        const int percent = (samplesPerBeat > 0.0)
            ? juce::jlimit(0, 99, static_cast<int>(100.0 * samplesInBeat / samplesPerBeat))
            : 0;
        m_barValue.setText(juce::String::formatted("%02d:%02d:%02d",
                                                    s.currentBar + 1,
                                                    s.currentBeat + 1,
                                                    percent),
                           juce::dontSendNotification);
    } else {
        m_barValue.setText("--:--:--", juce::dontSendNotification);
    }

    m_play.setHot(s.playing);

    bool anyRec = false;
    bool anyCountIn = false;
    for (int i = 0; i < loopa::LooperEngine::kNumTracks; ++i) {
        const auto rs = s.trackRecordState[static_cast<std::size_t>(i)];
        if (rs == static_cast<int>(loopa::RecordState::Recording)) anyRec = true;
        if (rs == static_cast<int>(loopa::RecordState::CountIn))   anyCountIn = true;
    }
    m_rec.setHot(anyRec);
    m_rec.setActive(anyCountIn);

    m_metro.setActive(s.metronomeEnabled);
    m_metro.setHot(false);
}

}  // namespace loopa::app
