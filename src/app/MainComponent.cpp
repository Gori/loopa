#include "MainComponent.h"

#include "Settings.h"
#include "SettingsWindow.h"
#include "Theme.h"

#include "core/EngineMessages.h"
#include "core/Logger.h"
#include "core/LooperEngine.h"

#include <string>

namespace loopa::app {

MainComponent::MainComponent(loopa::LooperEngine& engine,
                             juce::AudioDeviceManager& deviceManager,
                             TimbreTransferService& timbre)
    : m_engine(engine), m_deviceManager(deviceManager), m_bridge(engine, timbre) {
    setWantsKeyboardFocus(true);

    m_topBar = std::make_unique<TopBar>(m_bridge);
    m_topBar->setOnSettingsClicked([this] { openSettings(); });
    addAndMakeVisible(*m_topBar);

    for (int i = 0; i < loopa::LooperEngine::kNumTracks; ++i) {
        m_rows[static_cast<std::size_t>(i)] = std::make_unique<TrackRow>(m_bridge, i);
        addAndMakeVisible(*m_rows[static_cast<std::size_t>(i)]);
    }

    // Apply persisted settings to engine + tracks.
    const auto& s = Settings::instance().data();
    m_bridge.setMasterGain(s.masterGain);
    m_bridge.setMetronomeVolume(s.metronomeVolume);
    for (int i = 0; i < loopa::LooperEngine::kNumTracks; ++i) {
        m_bridge.setBars(i, s.defaultBarsForNewTracks);
    }

    // Default: track 0 selected (which also arms it under single-channel mode).
    m_bridge.selectTrack(0);

    setSize(1200, 720);
    startTimerHz(60);
}

MainComponent::~MainComponent() {
    stopTimer();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(theme::col(theme::kBg0));
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    if (m_topBar) {
        m_topBar->setBounds(area.removeFromTop(theme::kTopBarHeight));
    }
    const int rowH = area.getHeight() / loopa::LooperEngine::kNumTracks;
    for (int i = 0; i < loopa::LooperEngine::kNumTracks; ++i) {
        auto& row = m_rows[static_cast<std::size_t>(i)];
        if (row) {
            const int h = (i == loopa::LooperEngine::kNumTracks - 1)
                            ? area.getHeight() : rowH;
            row->setBounds(area.removeFromTop(h));
        }
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R') {
        const auto s = m_engine.snapshot();
        bool anyRec = false;
        for (int i = 0; i < loopa::LooperEngine::kNumTracks; ++i) {
            const auto rs = s.trackRecordState[static_cast<std::size_t>(i)];
            if (rs == static_cast<int>(loopa::RecordState::Recording) ||
                rs == static_cast<int>(loopa::RecordState::CountIn)) {
                anyRec = true; break;
            }
        }
        if (anyRec) m_bridge.stopRecord();
        else        m_bridge.startRecord();
        return true;
    }
    if (key.getTextCharacter() == ' ') {
        m_bridge.togglePlay();
        return true;
    }
    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M') {
        const auto s = m_engine.snapshot();
        m_bridge.setMetronome(!s.metronomeEnabled);
        return true;
    }
    if (key.getKeyCode() == ',' && key.getModifiers().isCommandDown()) {
        openSettings();
        return true;
    }
    return false;
}

void MainComponent::openSettings() {
    if (!m_settingsWindow) {
        m_settingsWindow = std::make_unique<SettingsWindow>(m_bridge, m_deviceManager);
    }
    m_settingsWindow->setVisible(true);
    m_settingsWindow->toFront(true);
}

void MainComponent::timerCallback() {
    // Drain engine events (UI-consumption side).
    while (auto ev = m_engine.drainEvent()) {
        if (ev->kind == loopa::EventKind::BpmLocked) {
            if (!m_metronomeAutoTriggered
                && Settings::instance().data().metronomeAutoOnAfterFirstRecord) {
                m_bridge.setMetronome(true);
                m_metronomeAutoTriggered = true;
            }
        } else if (ev->kind == loopa::EventKind::LoopAdded) {
            LOG_INFO("MainComponent: LoopAdded event — track "
                     + std::to_string(ev->trackId)
                     + ", new loop index " + std::to_string(ev->intArg)
                     + " (waiting for next bar-0 to activate)");
        } else if (ev->kind == loopa::EventKind::RecordingComplete) {
            LOG_INFO("MainComponent: RecordingComplete event — track "
                     + std::to_string(ev->trackId)
                     + ", " + std::to_string(ev->uint64Arg) + " samples captured");
        }
    }

    const auto s = m_engine.snapshot();
    if (m_topBar) m_topBar->update(s);
    for (auto& row : m_rows) {
        if (row) row->update(s);
    }

    if (s.playing && s.totalBars > 0 && s.currentBar != m_lastLoggedBar) {
        m_lastLoggedBar = s.currentBar;
        LOG_INFO("bar "
                 + std::to_string(s.currentBar + 1)
                 + " / "
                 + std::to_string(s.totalBars)
                 + " @ "
                 + std::to_string(s.bpm)
                 + " BPM");
    }
}

}  // namespace loopa::app
