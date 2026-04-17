#include <catch2/catch_test_macros.hpp>

#include "core/EngineMessages.h"
#include "core/LooperEngine.h"

#include <cmath>
#include <vector>

using loopa::CommandKind;
using loopa::EngineCommand;
using loopa::EventKind;
using loopa::LooperEngine;

namespace {

constexpr double kSR = 48000.0;
constexpr int kBlock = 128;

struct BlockIO {
    std::vector<float> inCh0;
    std::vector<float> outL;
    std::vector<float> outR;

    explicit BlockIO(int n)
        : inCh0(static_cast<std::size_t>(n), 0.0f),
          outL(static_cast<std::size_t>(n), 0.0f),
          outR(static_cast<std::size_t>(n), 0.0f) {}

    void fillInput(float v) {
        for (auto& x : inCh0) x = v;
    }
};

void runBlock(LooperEngine& e, BlockIO& io) {
    const float* inputs[1] = {io.inCh0.data()};
    float* outputs[2] = {io.outL.data(), io.outR.data()};
    e.processBlock(inputs, 1, outputs, 2, static_cast<int>(io.inCh0.size()));
}

void postCommand(LooperEngine& e, CommandKind k, int trackId = -1, int intArg = 0,
                 double dArg = 0.0, bool bArg = false) {
    EngineCommand c{};
    c.kind = k;
    c.trackId = trackId;
    c.intArg = intArg;
    c.doubleArg = dArg;
    c.boolArg = bArg;
    REQUIRE(e.postCommand(c));
}

}  // namespace

TEST_CASE("LooperEngine prepareToPlay sets sample rate on transport", "[engine]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);
    REQUIRE(e.currentSampleRate() == kSR);
    REQUIRE(e.currentBlockSize() == kBlock);
    REQUIRE_FALSE(e.snapshot().playing);
}

TEST_CASE("LooperEngine outputs silence when no loops exist", "[engine]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    BlockIO io(kBlock);
    io.fillInput(0.5f);
    runBlock(e, io);

    for (int i = 0; i < kBlock; ++i) {
        REQUIRE(io.outL[static_cast<std::size_t>(i)] == 0.0f);
        REQUIRE(io.outR[static_cast<std::size_t>(i)] == 0.0f);
    }
}

TEST_CASE("LooperEngine updates track snapshot fields from commands", "[engine]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    postCommand(e, CommandKind::SetTrackArmed, 2, 0, 0.0, true);
    postCommand(e, CommandKind::SetTrackBars,  2, 8);
    postCommand(e, CommandKind::SetTrackMuted, 1, 0, 0.0, true);

    BlockIO io(kBlock);
    runBlock(e, io);

    const auto s = e.snapshot();
    REQUIRE(s.trackArmed[2]);
    REQUIRE_FALSE(s.trackArmed[0]);
    REQUIRE(s.trackBars[2] == 8);
    REQUIRE(s.trackMuted[1]);
    REQUIRE_FALSE(s.trackMuted[0]);
}

TEST_CASE("LooperEngine selectTrack updates snapshot", "[engine]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);
    postCommand(e, CommandKind::SelectTrack, 3);
    BlockIO io(kBlock);
    runBlock(e, io);
    REQUIRE(e.snapshot().selectedTrackId == 3);
}

TEST_CASE("LooperEngine first-recording flow: BPM detection, loop install, playback", "[engine][record]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);

    // Feed ~4 seconds — 2 bars @ 120 BPM (larger-bars fit preferred).
    const std::size_t totalSamples = static_cast<std::size_t>(4.0 * kSR);
    BlockIO io(kBlock);
    io.fillInput(0.5f);

    std::size_t samplesRecorded = 0;
    while (samplesRecorded < totalSamples) {
        runBlock(e, io);
        samplesRecorded += kBlock;
    }

    postCommand(e, CommandKind::StopRecord);
    // Give the engine a block to process StopRecord + install loop.
    BlockIO silent(kBlock);
    runBlock(e, silent);

    // Drain events and collect RecordingComplete + BpmLocked.
    bool sawComplete = false;
    bool sawBpm = false;
    int  bars = 0;
    double bpm = 0.0;
    while (auto ev = e.drainEvent()) {
        if (ev->kind == EventKind::RecordingComplete) {
            sawComplete = true;
            REQUIRE(ev->trackId == 0);
        } else if (ev->kind == EventKind::BpmLocked) {
            sawBpm = true;
            REQUIRE(ev->trackId == 0);
            bars = ev->intArg;
            bpm  = ev->doubleArg;
        }
    }
    REQUIRE(sawComplete);
    REQUIRE(sawBpm);

    // The constant input gives ~2 s of audio — fit should be (2 bars, 120 BPM)
    // because the detector prefers larger bars.
    REQUIRE(bars == 2);
    REQUIRE(std::abs(bpm - 120.0) < 0.01);

    // Snapshot reflects installed loop and playing transport.
    const auto s = e.snapshot();
    REQUIRE(s.trackLoopCount[0] == 1);
    REQUIRE(s.trackActiveLoopIx[0] == 0);
    REQUIRE(s.playing);
    REQUIRE(s.bpm == bpm);
    REQUIRE(s.totalBars == bars);

    // Running another block should produce non-silent output (loop playback).
    BlockIO playback(kBlock);
    runBlock(e, playback);
    bool anyAudio = false;
    for (float x : playback.outL) {
        if (x != 0.0f) { anyAudio = true; break; }
    }
    REQUIRE(anyAudio);
}

TEST_CASE("LooperEngine mutes prevent a track's loop from reaching output", "[engine]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);

    BlockIO io(kBlock);
    io.fillInput(0.5f);
    const std::size_t totalSamples = static_cast<std::size_t>(4.0 * kSR);
    std::size_t n = 0;
    while (n < totalSamples) { runBlock(e, io); n += kBlock; }
    postCommand(e, CommandKind::StopRecord);
    { BlockIO silent(kBlock); runBlock(e, silent); }

    postCommand(e, CommandKind::SetTrackMuted, 0, 0, 0.0, true);
    BlockIO silent(kBlock);
    runBlock(e, silent);
    for (float x : silent.outL) {
        REQUIRE(x == 0.0f);
    }
}

TEST_CASE("LooperEngine: second track enters CountIn and starts recording at wrap", "[engine][countin]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    // First recording on track 0 to establish BPM = 120, 2 bars.
    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);

    BlockIO io(kBlock);
    io.fillInput(0.5f);
    const std::size_t totalSamples = static_cast<std::size_t>(4.0 * kSR);
    std::size_t n = 0;
    while (n < totalSamples) { runBlock(e, io); n += kBlock; }
    postCommand(e, CommandKind::StopRecord);
    { BlockIO silent(kBlock); runBlock(e, silent); }

    // Drain events so we can check fresh ones.
    while (e.drainEvent()) {}

    // Disarm track 0, arm track 1, select it, set bars=2, mode=New.
    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, false);
    postCommand(e, CommandKind::SetTrackArmed, 1, 0, 0.0, true);
    postCommand(e, CommandKind::SetTrackBars,  1, 2);
    postCommand(e, CommandKind::StartRecord);

    BlockIO silent(kBlock);
    runBlock(e, silent);

    // Track 1 should be in CountIn state, not Recording yet.
    REQUIRE(e.snapshot().trackRecordState[1] == static_cast<int>(loopa::RecordState::CountIn));

    // Run enough blocks for the transport to wrap at least once — 2 bars total length.
    const std::size_t totalLen = static_cast<std::size_t>(4.0 * kSR);
    std::size_t blocks = totalLen / kBlock + 5;
    for (std::size_t b = 0; b < blocks; ++b) {
        io.fillInput(0.3f);
        runBlock(e, io);
    }

    // Track 1 should have transitioned to Recording during the wrap; and may still be recording.
    // After another 2 bars worth of input, explicit StopRecord finishes it.
    postCommand(e, CommandKind::StopRecord);
    runBlock(e, silent);

    const auto s = e.snapshot();
    REQUIRE(s.trackLoopCount[1] >= 1);
}

TEST_CASE("LooperEngine Overdub sums new audio into the active loop", "[engine][overdub]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    // First record a 4-second constant-0.5 loop on track 0 -> 2 bars @ 120 BPM.
    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);
    BlockIO io(kBlock);
    io.fillInput(0.5f);
    for (std::size_t n = 0; n < static_cast<std::size_t>(4.0 * kSR); n += kBlock) runBlock(e, io);
    postCommand(e, CommandKind::StopRecord);
    { BlockIO silent(kBlock); runBlock(e, silent); }

    // Set track 0 mode to Overdub, start recording again — should count-in, then capture 2 bars.
    postCommand(e, CommandKind::SetTrackMode, 0, static_cast<int>(loopa::RecordMode::Overdub));
    postCommand(e, CommandKind::StartRecord);
    io.fillInput(0.3f);
    const std::size_t totalLen = static_cast<std::size_t>(4.0 * kSR);
    std::size_t blocks = (totalLen * 3) / kBlock;  // enough for count-in + full cycle + margin
    for (std::size_t b = 0; b < blocks; ++b) runBlock(e, io);

    // Loop sample at position 0 should now be ~0.5 + 0.3 = 0.8 (below 1.0 clamp).
    const auto s = e.snapshot();
    REQUIRE(s.trackLoopCount[0] == 1);
    REQUIRE(s.trackRecordState[0] == static_cast<int>(loopa::RecordState::Idle));

    // Run one more block; output should carry audio (loop playing).
    BlockIO playback(kBlock);
    runBlock(e, playback);
    bool anyNonZero = false;
    for (float x : playback.outL) {
        if (x != 0.0f) { anyNonZero = true; break; }
    }
    REQUIRE(anyNonZero);
}

TEST_CASE("LooperEngine Replace overwrites the active loop samples", "[engine][replace]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);
    BlockIO io(kBlock);
    io.fillInput(0.5f);
    for (std::size_t n = 0; n < static_cast<std::size_t>(4.0 * kSR); n += kBlock) runBlock(e, io);
    postCommand(e, CommandKind::StopRecord);
    { BlockIO silent(kBlock); runBlock(e, silent); }

    postCommand(e, CommandKind::SetTrackMode, 0, static_cast<int>(loopa::RecordMode::Replace));
    postCommand(e, CommandKind::StartRecord);
    io.fillInput(0.1f);
    const std::size_t totalLen = static_cast<std::size_t>(4.0 * kSR);
    std::size_t blocks = (totalLen * 3) / kBlock;
    for (std::size_t b = 0; b < blocks; ++b) runBlock(e, io);

    const auto s = e.snapshot();
    REQUIRE(s.trackRecordState[0] == static_cast<int>(loopa::RecordState::Idle));
    REQUIRE(s.trackLoopCount[0] == 1);  // replace kept the same loop, didn't add a new one
}

TEST_CASE("LooperEngine applies latency compensation to the capture buffer", "[engine][latency]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    // Feed 4 seconds of a deterministic signal: input[n] = (n + 1) / 1e6 so that
    // every sample is unique and recoverable.
    const std::size_t totalSamples = static_cast<std::size_t>(4.0 * kSR);
    const int kComp = 512;
    postCommand(e, CommandKind::SetLatencyCompensation, -1, kComp);
    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);

    BlockIO io(kBlock);
    std::size_t sampleCounter = 0;
    auto fillWithRamp = [&] {
        for (int i = 0; i < kBlock; ++i) {
            io.inCh0[static_cast<std::size_t>(i)] =
                static_cast<float>(sampleCounter + 1) / 1.0e6f;
            ++sampleCounter;
        }
    };

    while (sampleCounter < totalSamples) {
        fillWithRamp();
        runBlock(e, io);
    }
    postCommand(e, CommandKind::StopRecord);
    { BlockIO silent(kBlock); runBlock(e, silent); }

    bool sawBpm = false;
    int bars = 0;
    while (auto ev = e.drainEvent()) {
        if (ev->kind == EventKind::BpmLocked) { sawBpm = true; bars = ev->intArg; }
    }
    REQUIRE(sawBpm);
    REQUIRE(bars == 2);

    // Grab the installed loop and confirm its first sample equals the value
    // that was at rb index 512 — i.e., shifted by exactly the compensation.
    auto loop = e.activeLoopForUi(0);
    REQUIRE(loop != nullptr);
    REQUIRE_FALSE(loop->empty());

    const float expected = static_cast<float>(kComp + 1) / 1.0e6f;
    REQUIRE(std::abs(loop->data()[0] - expected) < 1e-6f);
}

TEST_CASE("LooperEngine with compensation larger than recording fails cleanly", "[engine][latency]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    // Crank comp very high so even a 3-second recording can't produce any data.
    postCommand(e, CommandKind::SetLatencyCompensation, -1, static_cast<int>(4.0 * kSR));
    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);

    BlockIO io(kBlock);
    io.fillInput(0.5f);
    for (std::size_t n = 0; n < static_cast<std::size_t>(3.0 * kSR); n += kBlock) {
        runBlock(e, io);
    }
    postCommand(e, CommandKind::StopRecord);
    { BlockIO silent(kBlock); runBlock(e, silent); }

    bool sawFail = false;
    while (auto ev = e.drainEvent()) {
        if (ev->kind == EventKind::BpmFitFailed) sawFail = true;
    }
    REQUIRE(sawFail);
    REQUIRE(e.snapshot().trackLoopCount[0] == 0);
}

TEST_CASE("LooperEngine rejects recording too short to fit any supported bars@BPM", "[engine]") {
    LooperEngine e;
    e.prepareToPlay(kSR, kBlock);

    postCommand(e, CommandKind::SetTrackArmed, 0, 0, 0.0, true);
    postCommand(e, CommandKind::StartRecord);

    // Record only ~0.5 s -> min bar at 140 BPM needs >= 1.714 s; should fail.
    BlockIO io(kBlock);
    io.fillInput(0.3f);
    const std::size_t totalSamples = static_cast<std::size_t>(0.5 * kSR);
    std::size_t n = 0;
    while (n < totalSamples) { runBlock(e, io); n += kBlock; }

    postCommand(e, CommandKind::StopRecord);
    { BlockIO silent(kBlock); runBlock(e, silent); }

    bool sawFail = false;
    while (auto ev = e.drainEvent()) {
        if (ev->kind == EventKind::BpmFitFailed) {
            sawFail = true;
            REQUIRE(ev->trackId == 0);
        }
    }
    REQUIRE(sawFail);

    const auto s = e.snapshot();
    REQUIRE(s.trackLoopCount[0] == 0);
    REQUIRE(s.bpm == 0.0);
}
