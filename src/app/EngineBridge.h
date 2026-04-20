#pragma once

#include "TimbreTransferService.h"

#include "core/EngineMessages.h"
#include "core/Logger.h"
#include "core/Loop.h"
#include "core/LooperEngine.h"

#include <memory>
#include <string>
#include <utility>

namespace loopa::app {

// Thin facade that the UI uses to talk to the audio engine. All methods are
// safe to call from the UI (message) thread. Commands are posted through the
// engine's lock-free queue; reads come from the engine's atomic snapshot.
class EngineBridge {
public:
    EngineBridge(loopa::LooperEngine& engine, TimbreTransferService& timbre)
        : m_engine(engine), m_timbre(timbre) {}

    loopa::LooperEngine::Snapshot snapshot() const { return m_engine.snapshot(); }

    // Transport ----------------------------------------------------------------
    void togglePlay()            { post(loopa::CommandKind::TogglePlay); }
    void resetPosition()         { post(loopa::CommandKind::ResetPosition); }
    void setBpm(double bpm)      { loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetBpm;
                                   c.doubleArg = bpm; m_engine.postCommand(c); }
    void setMetronome(bool on)   { loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetMetronomeEnabled;
                                   c.boolArg = on; m_engine.postCommand(c); }
    void setMasterGain(float v)  { loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetMasterGain;
                                   c.doubleArg = v; m_engine.postCommand(c); }
    void setMetronomeVolume(float v) { loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetMetronomeVolume;
                                   c.doubleArg = v; m_engine.postCommand(c); }

    // Recording ----------------------------------------------------------------
    void startRecord()           { post(loopa::CommandKind::StartRecord); }
    void stopRecord()            { post(loopa::CommandKind::StopRecord); }

    // Track state --------------------------------------------------------------
    void setArmed(int trackId, bool armed) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetTrackArmed;
        c.trackId = trackId; c.boolArg = armed; m_engine.postCommand(c);
    }
    void setMuted(int trackId, bool muted) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetTrackMuted;
        c.trackId = trackId; c.boolArg = muted; m_engine.postCommand(c);
    }
    void setInputCh(int trackId, int ch) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetTrackInputCh;
        c.trackId = trackId; c.intArg = ch; m_engine.postCommand(c);
    }
    void setBars(int trackId, int bars) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetTrackBars;
        c.trackId = trackId; c.intArg = bars; m_engine.postCommand(c);
    }
    void setMode(int trackId, int modeInt) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SetTrackMode;
        c.trackId = trackId; c.intArg = modeInt; m_engine.postCommand(c);
    }
    void selectTrack(int trackId) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::SelectTrack;
        c.trackId = trackId; m_engine.postCommand(c);
    }
    void cycleNextLoop(int trackId) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::CycleNextLoop;
        c.trackId = trackId; m_engine.postCommand(c);
    }
    void cyclePrevLoop(int trackId) {
        loopa::EngineCommand c{}; c.kind = loopa::CommandKind::CyclePrevLoop;
        c.trackId = trackId; m_engine.postCommand(c);
    }

    // Timbre transfer ----------------------------------------------------------
    // Takes the track's currently active loop, runs it through the neural
    // model on a background thread, and — on completion — hands the new loop
    // to the engine, which activates it at the next bar-0 crossing. Safe to
    // call from the UI thread. Silently ignored if the track has no active
    // loop or is already converting.
    void convertActiveLoop(int trackId, int timbreIndex) {
        LOG_INFO("EngineBridge::convertActiveLoop: UI click on track "
                 + std::to_string(trackId)
                 + " with timbre preset " + std::to_string(timbreIndex));
        auto source = m_engine.activeLoopForUi(trackId);
        if (!source || source->empty()) {
            LOG_WARN("EngineBridge::convertActiveLoop: no active loop on track "
                     + std::to_string(trackId) + ", ignored");
            return;
        }

        auto& engine = m_engine;
        const bool accepted = m_timbre.submit(
            trackId, std::move(source), timbreIndex,
            [&engine](int tid, std::shared_ptr<loopa::Loop> result, std::string error) {
                if (result) {
                    LOG_INFO("EngineBridge: completion on UI thread for track "
                             + std::to_string(tid) + " — handing loop to engine");
                    engine.submitLoop(tid, std::move(result), /*activateOnNextBar=*/true);
                } else {
                    LOG_WARN(std::string("Timbre transfer failed on track ")
                             + std::to_string(tid) + ": " + error);
                }
            });
        if (!accepted) {
            LOG_INFO("EngineBridge::convertActiveLoop: service rejected submit on track "
                     + std::to_string(trackId) + " (already busy or invalid input)");
        }
    }

    bool isConverting(int trackId) const {
        return m_timbre.isBusy(trackId);
    }

    int numTimbres() const { return m_timbre.numTimbres(); }
    std::string timbreName(int index) const { return m_timbre.presetName(index); }

    // For completeness; the engine is the owner.
    loopa::LooperEngine& engine() noexcept { return m_engine; }

private:
    void post(loopa::CommandKind k) {
        loopa::EngineCommand c{};
        c.kind = k;
        m_engine.postCommand(c);
    }

    loopa::LooperEngine& m_engine;
    TimbreTransferService& m_timbre;
};

}  // namespace loopa::app
