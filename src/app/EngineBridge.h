#pragma once

#include "core/EngineMessages.h"
#include "core/LooperEngine.h"

namespace loopa::app {

// Thin facade that the UI uses to talk to the audio engine. All methods are
// safe to call from the UI (message) thread. Commands are posted through the
// engine's lock-free queue; reads come from the engine's atomic snapshot.
class EngineBridge {
public:
    explicit EngineBridge(loopa::LooperEngine& engine) : m_engine(engine) {}

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

    // For completeness; the engine is the owner.
    loopa::LooperEngine& engine() noexcept { return m_engine; }

private:
    void post(loopa::CommandKind k) {
        loopa::EngineCommand c{};
        c.kind = k;
        m_engine.postCommand(c);
    }

    loopa::LooperEngine& m_engine;
};

}  // namespace loopa::app
