#pragma once

#include "core/BpmDetector.h"
#include "core/EngineMessages.h"
#include "core/LockFreeQueue.h"
#include "core/Loop.h"
#include "core/Metronome.h"
#include "core/RecordingBuffer.h"
#include "core/Track.h"
#include "core/Transport.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace loopa {

class LooperEngine {
public:
    static constexpr int kNumTracks = 4;
    static constexpr std::size_t kCommandQueueCapacity = 256;
    static constexpr std::size_t kEventQueueCapacity   = 1024;

    // Max recording length per track = 16 bars at 60 BPM at sample rate.
    // Buffers are allocated to this ceiling in prepareToPlay.

    struct Snapshot {
        bool playing = false;
        double bpm = 0.0;
        int beatsPerBar = 4;
        int currentBar = 0;
        int currentBeat = 0;
        int totalBars = 0;
        std::uint64_t samplePosition = 0;
        std::uint64_t totalLengthSamples = 0;
        int selectedTrackId = 0;
        bool metronomeEnabled = false;
        std::array<bool, kNumTracks> trackArmed{};
        std::array<bool, kNumTracks> trackMuted{};
        std::array<int,  kNumTracks> trackBars{{4, 4, 4, 4}};
        std::array<int,  kNumTracks> trackMode{{0, 0, 0, 0}};  // RecordMode as int
        std::array<int,  kNumTracks> trackRecordState{{0, 0, 0, 0}};
        std::array<int,  kNumTracks> trackLoopCount{};
        std::array<int,  kNumTracks> trackActiveLoopIx{{-1, -1, -1, -1}};
        std::array<int,  kNumTracks> trackInputCh{};
    };

    LooperEngine();
    ~LooperEngine();

    LooperEngine(const LooperEngine&) = delete;
    LooperEngine& operator=(const LooperEngine&) = delete;

    void prepareToPlay(double sampleRate, int blockSize);
    void releaseResources();

    // Hot path. Channels pointers + per-channel sample arrays.
    // Input channels may be fewer than tracks. Output is mono mix written into
    // both output channels (L=R). `numSamples` is the block length.
    void processBlock(const float* const* inputs, int numInputChannels,
                      float* const* outputs, int numOutputChannels,
                      int numSamples) noexcept;

    // UI thread API -----------------------------------------------------------
    bool postCommand(const EngineCommand& cmd) noexcept {
        return m_commandQueue.tryPush(cmd);
    }

    std::optional<EngineEvent> drainEvent() noexcept {
        return m_eventQueue.tryPop();
    }

    Snapshot snapshot() const noexcept;

    double currentSampleRate() const noexcept { return m_sampleRate; }
    int    currentBlockSize()  const noexcept { return m_blockSize; }

    // UI-safe accessor. Returns a shared_ptr copy of the track's currently
    // active loop. The audio thread republishes this handle whenever the
    // active loop changes (record complete, cycle loop, switch, etc).
    std::shared_ptr<const Loop> activeLoopForUi(int trackId) const;

    // Hand a freshly-constructed Loop to the engine (e.g. from a timbre
    // transfer worker on the UI or a background thread). The loop is
    // appended to the track's loop list; if activateOnNextBar is true, it
    // becomes the active loop at the next bar-0 crossing — or immediately
    // if the transport isn't running. UI/worker-thread safe.
    void submitLoop(int trackId, std::shared_ptr<Loop> loop, bool activateOnNextBar);

    // Raw pointer to a track's pre-allocated recording buffer. Lifetime is the
    // engine's; pointer is stable after prepareToPlay. Reading samples outside
    // the range [0, recordingBufferWritten(trackId)) returns undefined values.
    const float* recordingBufferData(int trackId) const noexcept;

    // How many samples have been captured so far in the current recording.
    // Atomic, updated by the audio thread every block. Zero when not recording.
    std::uint64_t recordingBufferWritten(int trackId) const noexcept;

private:
    void handleCommand(const EngineCommand& cmd) noexcept;
    void drainLoopInbox() noexcept;
    void pushEvent(const EngineEvent& ev) noexcept;
    void writeSnapshotAtomics() noexcept;
    void startRecordingOrCountIn() noexcept;
    void stopAllRecordings() noexcept;
    void finishRecording(int trackId) noexcept;
    void updateTransportLength() noexcept;
    void mixLoopsIntoMono(float* mono, int numSamples) noexcept;
    void captureInputs(const float* const* inputs, int numInputChannels,
                       int numSamples,
                       const std::array<int, kNumTracks>& captureStart) noexcept;
    void handleTransportCrossings(int numSamples,
                                  std::array<int, kNumTracks>& captureStart) noexcept;
    void autoStopCompletedRecordings() noexcept;
    std::size_t recordingTargetSamples(int trackId) const noexcept;
    void publishActiveLoopForUi(int trackId) noexcept;

    // Owned state --------------------------------------------------------------
    std::array<Track, kNumTracks> m_tracks;
    Transport m_transport;
    Metronome m_metronome;

    std::array<std::unique_ptr<RecordingBuffer>, kNumTracks> m_recBufs;
    std::vector<float> m_scratchMono;  // pre-allocated in prepareToPlay

    double m_sampleRate = 0.0;
    int    m_blockSize  = 0;
    int    m_selectedTrackId = 0;
    bool   m_metronomeEnabled = false;
    float  m_masterGain = 1.0f;
    int    m_latencyCompensationSamples = 0;   // input + output, in samples

    // Queues between UI <-> audio thread.
    LockFreeQueue<EngineCommand, kCommandQueueCapacity> m_commandQueue;
    LockFreeQueue<EngineEvent,   kEventQueueCapacity>   m_eventQueue;

    // Snapshot mirrors for UI polling (audio thread writes, UI reads).
    std::atomic<std::uint64_t> m_snapSamplePos{0};
    std::atomic<std::uint64_t> m_snapTotalLen{0};
    std::atomic<int>           m_snapBar{0};
    std::atomic<int>           m_snapBeat{0};
    std::atomic<int>           m_snapTotalBars{0};
    std::atomic<bool>          m_snapPlaying{false};
    std::atomic<std::uint64_t> m_snapBpmBits{0};      // double bit-pattern
    std::atomic<int>           m_snapBeatsPerBar{4};
    std::atomic<bool>          m_snapMetronomeEnabled{false};
    std::atomic<int>           m_snapSelectedTrackId{0};

    std::array<std::atomic<bool>, kNumTracks> m_snapArmed{};
    std::array<std::atomic<bool>, kNumTracks> m_snapMuted{};
    std::array<std::atomic<int>,  kNumTracks> m_snapBars{};
    std::array<std::atomic<int>,  kNumTracks> m_snapMode{};
    std::array<std::atomic<int>,  kNumTracks> m_snapRecordState{};
    std::array<std::atomic<int>,  kNumTracks> m_snapLoopCount{};
    std::array<std::atomic<int>,  kNumTracks> m_snapActiveLoopIx{};
    std::array<std::atomic<int>,  kNumTracks> m_snapInputCh{};
    std::array<std::atomic<std::uint64_t>, kNumTracks> m_snapRecordingWritten{};

    // Mirror of each track's active loop, kept up-to-date by the audio thread
    // for safe UI reads. Protected by m_uiLoopMutex on both sides. The same
    // mutex also guards m_loopInbox below.
    mutable std::mutex m_uiLoopMutex;
    std::array<std::shared_ptr<const Loop>, kNumTracks> m_uiActiveLoop;

    // Inbox for loops produced off the audio thread (timbre transfer
    // completions). UI/worker thread pushes under m_uiLoopMutex; audio
    // thread drains via try_lock at the start of processBlock.
    struct LoopInboxEntry {
        int trackId = -1;
        std::shared_ptr<Loop> loop;
        bool activateOnNextBar = false;
    };
    std::vector<LoopInboxEntry> m_loopInbox;

    // Per-track "switch to this loop index at next bar-0" intent, written by
    // drainLoopInbox and consumed in handleTransportCrossings.
    std::array<std::optional<int>, kNumTracks> m_pendingActiveLoopIx{};
};

}  // namespace loopa
