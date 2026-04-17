#pragma once

#include <cstdint>

namespace loopa {

// Messages passed through lock-free SPSC queues between UI and audio threads.
// Both types must stay trivially copyable so they can be stored by value.

enum class CommandKind : std::uint8_t {
    NoOp,
    SetPlaying,            // boolArg: playing
    TogglePlay,
    ResetPosition,
    SetBpm,                // doubleArg: bpm
    SetBeatsPerBar,        // intArg: beatsPerBar
    SetMetronomeEnabled,   // boolArg: enabled

    SetTrackArmed,         // trackId, boolArg
    SetTrackMuted,         // trackId, boolArg
    SetTrackInputCh,       // trackId, intArg
    SetTrackBars,          // trackId, intArg (1/2/4/8/16)
    SetTrackMode,          // trackId, intArg (0=New,1=Overdub,2=Replace)
    SelectTrack,           // trackId

    StartRecord,           // begins recording on armed tracks
    StopRecord,            // stops recording on any currently recording tracks

    CycleNextLoop,         // trackId (-1 = selected track)
    CyclePrevLoop,         // trackId
    ClearActiveLoop,       // trackId
    SwitchActiveLoop,      // trackId, intArg: new loop index

    SetMasterGain,         // doubleArg: linear gain 0..1+
    SetMetronomeVolume,    // doubleArg: linear 0..1

    SetLatencyCompensation, // intArg: samples (>= 0). Applied to subsequent recordings.
};

struct EngineCommand {
    CommandKind kind = CommandKind::NoOp;
    int    trackId   = -1;   // -1 = global or "selected"
    int    intArg    = 0;
    double doubleArg = 0.0;
    bool   boolArg   = false;
};

enum class EventKind : std::uint8_t {
    NoOp,
    PositionUpdate,        // uint64Arg: samplePosition, intArg: bar, intArg2: beat
    BarTick,               // intArg: bar index (0-based)
    RecordingStarted,      // trackId
    RecordingComplete,     // trackId, uint64Arg: samplesCaptured
    BpmFitFailed,          // trackId
    BpmLocked,             // doubleArg: bpm, intArg: bars
    CountInTick,           // trackId, intArg: beats remaining (N..1)
    LevelPeak,             // trackId, doubleArg: peak [0..1]
};

struct EngineEvent {
    EventKind kind = EventKind::NoOp;
    int    trackId   = -1;
    int    intArg    = 0;
    int    intArg2   = 0;
    double doubleArg = 0.0;
    std::uint64_t uint64Arg = 0;
};

}  // namespace loopa
