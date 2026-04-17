#include "LooperEngine.h"

#include <cstring>

namespace loopa {

namespace {

std::uint64_t doubleBits(double d) noexcept {
    std::uint64_t out = 0;
    std::memcpy(&out, &d, sizeof(out));
    return out;
}

double bitsToDouble(std::uint64_t b) noexcept {
    double out = 0.0;
    std::memcpy(&out, &b, sizeof(out));
    return out;
}

std::size_t maxRecordingSamples(double sampleRate) noexcept {
    // 16 bars × 4 beats/bar × 1.0 s/beat (at 60 BPM) = 64 s.
    return static_cast<std::size_t>(64.0 * sampleRate + 0.5);
}

}  // namespace

LooperEngine::LooperEngine()
    : m_tracks{Track(0), Track(1), Track(2), Track(3)} {
    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        m_snapArmed[ix].store(false);
        m_snapMuted[ix].store(false);
        m_snapBars[ix].store(m_tracks[ix].bars());
        m_snapMode[ix].store(static_cast<int>(RecordMode::New));
        m_snapRecordState[ix].store(static_cast<int>(RecordState::Idle));
        m_snapLoopCount[ix].store(0);
        m_snapActiveLoopIx[ix].store(-1);
    }
}

LooperEngine::~LooperEngine() = default;

void LooperEngine::prepareToPlay(double sampleRate, int blockSize) {
    m_sampleRate = sampleRate;
    m_blockSize = blockSize;

    const std::size_t cap = maxRecordingSamples(sampleRate);
    for (int i = 0; i < kNumTracks; ++i) {
        m_recBufs[static_cast<std::size_t>(i)] = std::make_unique<RecordingBuffer>(cap);
    }
    m_scratchMono.assign(static_cast<std::size_t>(blockSize > 0 ? blockSize : 128), 0.0f);

    m_metronome.prepareToPlay(sampleRate);

    auto tb = m_transport.timebase();
    tb.sampleRate = sampleRate;
    m_transport.setTimebase(tb);
}

void LooperEngine::releaseResources() {
    for (auto& rb : m_recBufs) {
        rb.reset();
    }
    m_scratchMono.clear();
}

void LooperEngine::startRecordingOrCountIn() noexcept {
    const bool firstRecording = (m_transport.timebase().bpm <= 0.0)
                             || (m_transport.totalLengthSamples() == 0);

    bool anyStarted = false;
    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        if (!m_tracks[ix].armed()) {
            continue;
        }
        if (m_tracks[ix].recordState() != RecordState::Idle) {
            continue;
        }
        m_recBufs[ix]->reset();
        if (firstRecording) {
            m_tracks[ix].setRecordState(RecordState::Recording);
            pushEvent(EngineEvent{EventKind::RecordingStarted, i, 0, 0, 0.0, 0});
        } else {
            m_tracks[ix].setRecordState(RecordState::CountIn);
        }
        anyStarted = true;
    }
    if (anyStarted) {
        m_transport.setPlaying(true);
    }
}

void LooperEngine::stopAllRecordings() noexcept {
    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        const auto rs = m_tracks[ix].recordState();
        if (rs == RecordState::Recording) {
            m_tracks[ix].setRecordState(RecordState::Idle);
            finishRecording(i);
        } else if (rs == RecordState::CountIn) {
            // Cancel count-in.
            m_tracks[ix].setRecordState(RecordState::Idle);
        }
    }
}

void LooperEngine::finishRecording(int trackId) noexcept {
    const auto ix = static_cast<std::size_t>(trackId);
    auto& rb = *m_recBufs[ix];
    const std::size_t written = rb.written();

    if (written == 0) {
        rb.reset();
        pushEvent(EngineEvent{EventKind::BpmFitFailed, trackId, 0, 0, 0.0, 0});
        return;
    }

    // Latency compensation: skip the first `comp` samples of the capture buffer
    // — that span contains whatever was happening between the user-perceived
    // bar 0 and the engine's view of bar 0 (due to output + input hardware
    // path delay). The remaining `avail` samples are the user's actual play.
    const std::size_t comp = m_latencyCompensationSamples > 0
                               ? static_cast<std::size_t>(m_latencyCompensationSamples)
                               : 0;
    if (comp >= written) {
        // Nothing useful beyond the compensation window — reject.
        rb.reset();
        pushEvent(EngineEvent{EventKind::BpmFitFailed, trackId, 0, 0, 0.0, 0});
        return;
    }
    const float*      src   = rb.data() + comp;
    const std::size_t avail = written - comp;

    const RecordMode mode = m_tracks[ix].mode();

    // Overdub and Replace modify an existing active loop in place.
    if (mode == RecordMode::Overdub || mode == RecordMode::Replace) {
        auto loop = m_tracks[ix].activeLoop();
        if (!loop || loop->empty()) {
            rb.reset();
            pushEvent(EngineEvent{EventKind::BpmFitFailed, trackId, 0, 0, 0.0, 0});
            return;
        }
        const std::size_t n = (avail > loop->length()) ? loop->length() : avail;
        if (mode == RecordMode::Overdub) {
            loop->overdubSum(src, n, 0);
        } else {
            loop->replaceRegion(src, n, 0);
        }
        rb.reset();
        publishActiveLoopForUi(trackId);
        pushEvent(EngineEvent{EventKind::RecordingComplete, trackId, 0, 0, 0.0,
                              static_cast<std::uint64_t>(n)});
        return;
    }

    // RecordMode::New
    auto tb = m_transport.timebase();
    int bars = m_tracks[ix].bars();
    double bpm = tb.bpm;

    if (bpm <= 0.0) {
        auto fit = detectBpm(avail, m_sampleRate);
        if (!fit) {
            rb.reset();
            pushEvent(EngineEvent{EventKind::BpmFitFailed, trackId, 0, 0, 0.0, 0});
            return;
        }
        bars = fit->bars;
        bpm  = fit->bpm;
        tb.bpm = bpm;
        m_transport.setTimebase(tb);
        m_tracks[ix].setBars(bars);
    }

    const std::size_t trimmed = samplesForBars(bars, bpm, m_sampleRate, tb.beatsPerBar);
    const std::size_t actual = (trimmed == 0 || trimmed > avail) ? avail : trimmed;

    auto loop = std::make_shared<Loop>(bars, bpm, m_sampleRate, actual);
    std::memcpy(loop->data(), src, actual * sizeof(float));
    m_tracks[ix].addLoop(std::move(loop));

    updateTransportLength();
    m_transport.resetPosition();
    publishActiveLoopForUi(trackId);

    rb.reset();
    pushEvent(EngineEvent{EventKind::RecordingComplete, trackId, 0, 0, 0.0,
                          static_cast<std::uint64_t>(actual)});
    pushEvent(EngineEvent{EventKind::BpmLocked, trackId, bars, 0, bpm, 0});
}

std::size_t LooperEngine::recordingTargetSamples(int trackId) const noexcept {
    const auto ix = static_cast<std::size_t>(trackId);
    const RecordMode mode = m_tracks[ix].mode();
    const std::size_t comp = m_latencyCompensationSamples > 0
                               ? static_cast<std::size_t>(m_latencyCompensationSamples)
                               : 0;

    if (mode == RecordMode::Overdub || mode == RecordMode::Replace) {
        auto loop = m_tracks[ix].activeLoop();
        return loop ? (loop->length() + comp) : 0;
    }
    // RecordMode::New
    const auto tb = m_transport.timebase();
    if (tb.bpm <= 0.0) {
        return 0;  // first recording — no target, user stops manually
    }
    return samplesForBars(m_tracks[ix].bars(), tb.bpm, m_sampleRate, tb.beatsPerBar) + comp;
}

void LooperEngine::autoStopCompletedRecordings() noexcept {
    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        if (m_tracks[ix].recordState() != RecordState::Recording) {
            continue;
        }
        const std::size_t target = recordingTargetSamples(i);
        if (target == 0) {
            continue;  // manual-stop recording (first loop)
        }
        if (m_recBufs[ix]->written() >= target) {
            m_tracks[ix].setRecordState(RecordState::Idle);
            finishRecording(i);
        }
    }
}

void LooperEngine::updateTransportLength() noexcept {
    std::size_t longest = 0;
    int longestBars = 0;
    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        auto loop = m_tracks[ix].activeLoop();
        if (loop && loop->length() > longest) {
            longest = loop->length();
            longestBars = loop->bars();
        }
    }
    m_transport.setTotalLengthSamples(longest);
    m_transport.setTotalBars(longestBars);
}

void LooperEngine::publishActiveLoopForUi(int trackId) noexcept {
    const auto ix = static_cast<std::size_t>(trackId);
    auto cur = m_tracks[ix].activeLoop();
    std::lock_guard<std::mutex> lk(m_uiLoopMutex);
    m_uiActiveLoop[ix] = std::move(cur);
}

std::shared_ptr<const Loop> LooperEngine::activeLoopForUi(int trackId) const {
    if (trackId < 0 || trackId >= kNumTracks) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lk(m_uiLoopMutex);
    return m_uiActiveLoop[static_cast<std::size_t>(trackId)];
}

void LooperEngine::handleCommand(const EngineCommand& cmd) noexcept {
    const std::size_t tix = (cmd.trackId >= 0 && cmd.trackId < kNumTracks)
                              ? static_cast<std::size_t>(cmd.trackId)
                              : 0;
    switch (cmd.kind) {
        case CommandKind::NoOp:
            break;

        case CommandKind::SetPlaying:
            if (!cmd.boolArg) {
                stopAllRecordings();
            }
            m_transport.setPlaying(cmd.boolArg);
            break;

        case CommandKind::TogglePlay:
            if (m_transport.isPlaying()) {
                stopAllRecordings();
            }
            m_transport.setPlaying(!m_transport.isPlaying());
            break;

        case CommandKind::ResetPosition:
            m_transport.resetPosition();
            break;

        case CommandKind::SetBpm: {
            auto tb = m_transport.timebase();
            tb.bpm = cmd.doubleArg;
            m_transport.setTimebase(tb);
            break;
        }

        case CommandKind::SetBeatsPerBar: {
            auto tb = m_transport.timebase();
            tb.beatsPerBar = cmd.intArg;
            m_transport.setTimebase(tb);
            break;
        }

        case CommandKind::SetMetronomeEnabled:
            m_metronomeEnabled = cmd.boolArg;
            break;

        case CommandKind::SetTrackArmed:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_tracks[tix].setArmed(cmd.boolArg);
            }
            break;

        case CommandKind::SetTrackMuted:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_tracks[tix].setMuted(cmd.boolArg);
            }
            break;

        case CommandKind::SetTrackInputCh:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_tracks[tix].setInputCh(cmd.intArg);
            }
            break;

        case CommandKind::SetTrackBars:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_tracks[tix].setBars(cmd.intArg);
            }
            break;

        case CommandKind::SetTrackMode:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_tracks[tix].setMode(static_cast<RecordMode>(cmd.intArg));
            }
            break;

        case CommandKind::SelectTrack:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_selectedTrackId = cmd.trackId;
                // Single-channel recording: only the selected track is armed.
                for (int i = 0; i < kNumTracks; ++i) {
                    m_tracks[static_cast<std::size_t>(i)].setArmed(i == cmd.trackId);
                }
            }
            break;

        case CommandKind::StartRecord:
            startRecordingOrCountIn();
            break;

        case CommandKind::StopRecord:
            stopAllRecordings();
            break;

        case CommandKind::CycleNextLoop:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_tracks[tix].cycleNextLoop();
                updateTransportLength();
                publishActiveLoopForUi(cmd.trackId);
            }
            break;

        case CommandKind::CyclePrevLoop:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                const int count = static_cast<int>(m_tracks[tix].loopCount());
                for (int k = 0; k + 1 < count; ++k) {
                    m_tracks[tix].cycleNextLoop();
                }
                updateTransportLength();
                publishActiveLoopForUi(cmd.trackId);
            }
            break;

        case CommandKind::ClearActiveLoop:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                if (auto lp = m_tracks[tix].activeLoop()) {
                    lp->clearAudio();
                }
                publishActiveLoopForUi(cmd.trackId);
            }
            break;

        case CommandKind::SwitchActiveLoop:
            if (cmd.trackId >= 0 && cmd.trackId < kNumTracks) {
                m_tracks[tix].setActiveLoopIx(cmd.intArg);
                updateTransportLength();
                publishActiveLoopForUi(cmd.trackId);
            }
            break;

        case CommandKind::SetMasterGain:
            m_masterGain = static_cast<float>(cmd.doubleArg);
            if (m_masterGain < 0.0f) m_masterGain = 0.0f;
            break;

        case CommandKind::SetMetronomeVolume:
            m_metronome.setAmplitude(static_cast<float>(cmd.doubleArg));
            break;

        case CommandKind::SetLatencyCompensation:
            m_latencyCompensationSamples = cmd.intArg < 0 ? 0 : cmd.intArg;
            break;
    }
}

void LooperEngine::pushEvent(const EngineEvent& ev) noexcept {
    m_eventQueue.tryPush(ev);
}

void LooperEngine::mixLoopsIntoMono(float* mono, int numSamples) noexcept {
    if (!m_transport.isPlaying()) {
        return;
    }
    const std::size_t pos = m_transport.samplePosition();

    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        if (m_tracks[ix].muted()) {
            continue;
        }
        auto loop = m_tracks[ix].activeLoop();
        if (!loop || loop->empty()) {
            continue;
        }
        for (int s = 0; s < numSamples; ++s) {
            mono[s] += loop->readWrapped(pos + static_cast<std::size_t>(s));
        }
    }
}

void LooperEngine::captureInputs(const float* const* inputs, int numInputChannels,
                                 int numSamples,
                                 const std::array<int, kNumTracks>& captureStart) noexcept {
    if (inputs == nullptr || numInputChannels <= 0) {
        return;
    }
    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        const int startOff = captureStart[ix];
        if (startOff < 0 || startOff >= numSamples) {
            continue;
        }
        int ch = m_tracks[ix].inputCh();
        if (ch < 0 || ch >= numInputChannels) {
            ch = 0;
        }
        const float* src = inputs[ch];
        if (src == nullptr) {
            continue;
        }
        const int n = numSamples - startOff;
        m_recBufs[ix]->append(src + startOff, static_cast<std::size_t>(n));
    }
}

void LooperEngine::handleTransportCrossings(int numSamples,
                                            std::array<int, kNumTracks>& captureStart) noexcept {
    if (!m_transport.isPlaying() || m_transport.totalLengthSamples() == 0) {
        return;
    }
    const std::size_t len = m_transport.totalLengthSamples();
    const std::size_t curPos = m_transport.samplePosition();
    const std::size_t spBeat = m_transport.samplesPerBeat();

    // Scan the block for bar-0 (loop wrap) and beat boundaries.
    for (int s = 0; s < numSamples; ++s) {
        const std::size_t abs = (curPos + static_cast<std::size_t>(s)) % len;

        // Loop-wrap crossing: any CountIn tracks begin recording here.
        if (abs == 0) {
            for (int i = 0; i < kNumTracks; ++i) {
                const auto ix = static_cast<std::size_t>(i);
                if (m_tracks[ix].recordState() == RecordState::CountIn) {
                    m_tracks[ix].setRecordState(RecordState::Recording);
                    m_recBufs[ix]->reset();
                    captureStart[ix] = s;
                    pushEvent(EngineEvent{EventKind::RecordingStarted, i, 0, 0, 0.0, 0});
                }
            }
        }

        // Beat boundaries: metronome click + count-in tick events.
        if (spBeat > 0 && abs % spBeat == 0) {
            bool anyCountIn = false;
            for (int i = 0; i < kNumTracks; ++i) {
                if (m_tracks[static_cast<std::size_t>(i)].recordState() == RecordState::CountIn) {
                    anyCountIn = true;
                    break;
                }
            }
            if (m_metronomeEnabled || anyCountIn) {
                const bool downbeat = (abs == 0);
                m_metronome.trigger(s, downbeat);
            }
            if (anyCountIn) {
                const int beatsRemaining = static_cast<int>((len - abs) / spBeat);
                for (int i = 0; i < kNumTracks; ++i) {
                    if (m_tracks[static_cast<std::size_t>(i)].recordState() == RecordState::CountIn) {
                        pushEvent(EngineEvent{EventKind::CountInTick, i, beatsRemaining,
                                              0, 0.0, 0});
                    }
                }
            }
        }
    }
}

void LooperEngine::processBlock(const float* const* inputs, int numInputChannels,
                                float* const* outputs, int numOutputChannels,
                                int numSamples) noexcept {
    while (auto cmd = m_commandQueue.tryPop()) {
        handleCommand(*cmd);
    }

    const auto bufSize = static_cast<std::size_t>(numSamples);
    if (m_scratchMono.size() < bufSize) {
        // Rare: block size grew past reservation; use what we have.
        // Fallback to output zero in that edge case.
        for (int ch = 0; ch < numOutputChannels; ++ch) {
            std::memset(outputs[ch], 0, bufSize * sizeof(float));
        }
        return;
    }
    std::memset(m_scratchMono.data(), 0, bufSize * sizeof(float));

    // Track per-track capture start offset (-1 = don't capture this block).
    std::array<int, kNumTracks> captureStart{};
    for (int i = 0; i < kNumTracks; ++i) {
        captureStart[static_cast<std::size_t>(i)] =
            (m_tracks[static_cast<std::size_t>(i)].recordState() == RecordState::Recording) ? 0 : -1;
    }

    handleTransportCrossings(numSamples, captureStart);

    captureInputs(inputs, numInputChannels, numSamples, captureStart);
    autoStopCompletedRecordings();

    mixLoopsIntoMono(m_scratchMono.data(), numSamples);
    m_metronome.render(m_scratchMono.data(), numSamples);

    if (m_masterGain != 1.0f) {
        for (int i = 0; i < numSamples; ++i) {
            m_scratchMono[static_cast<std::size_t>(i)] *= m_masterGain;
        }
    }

    // Copy mono mix to every output channel.
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        std::memcpy(outputs[ch], m_scratchMono.data(), bufSize * sizeof(float));
    }

    if (m_transport.isPlaying()) {
        m_transport.advance(bufSize);
    }

    writeSnapshotAtomics();
}

void LooperEngine::writeSnapshotAtomics() noexcept {
    m_snapSamplePos.store(static_cast<std::uint64_t>(m_transport.samplePosition()),
                          std::memory_order_relaxed);
    m_snapTotalLen.store(static_cast<std::uint64_t>(m_transport.totalLengthSamples()),
                         std::memory_order_relaxed);
    m_snapBar.store(m_transport.currentBar(), std::memory_order_relaxed);
    m_snapBeat.store(m_transport.currentBeat(), std::memory_order_relaxed);
    m_snapTotalBars.store(m_transport.totalBars(), std::memory_order_relaxed);
    m_snapPlaying.store(m_transport.isPlaying(), std::memory_order_relaxed);
    m_snapBpmBits.store(doubleBits(m_transport.timebase().bpm),
                        std::memory_order_relaxed);
    m_snapBeatsPerBar.store(m_transport.timebase().beatsPerBar, std::memory_order_relaxed);
    m_snapMetronomeEnabled.store(m_metronomeEnabled, std::memory_order_relaxed);
    m_snapSelectedTrackId.store(m_selectedTrackId, std::memory_order_relaxed);

    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        m_snapArmed[ix].store(m_tracks[ix].armed(), std::memory_order_relaxed);
        m_snapMuted[ix].store(m_tracks[ix].muted(), std::memory_order_relaxed);
        m_snapBars[ix].store(m_tracks[ix].bars(), std::memory_order_relaxed);
        m_snapMode[ix].store(static_cast<int>(m_tracks[ix].mode()), std::memory_order_relaxed);
        m_snapRecordState[ix].store(static_cast<int>(m_tracks[ix].recordState()),
                                    std::memory_order_relaxed);
        m_snapLoopCount[ix].store(static_cast<int>(m_tracks[ix].loopCount()),
                                  std::memory_order_relaxed);
        m_snapActiveLoopIx[ix].store(m_tracks[ix].activeLoopIx(), std::memory_order_relaxed);
        m_snapInputCh[ix].store(m_tracks[ix].inputCh(), std::memory_order_relaxed);
        const auto written = m_recBufs[ix] ? m_recBufs[ix]->written() : std::size_t{0};
        m_snapRecordingWritten[ix].store(static_cast<std::uint64_t>(written),
                                         std::memory_order_relaxed);
    }
}

const float* LooperEngine::recordingBufferData(int trackId) const noexcept {
    if (trackId < 0 || trackId >= kNumTracks) return nullptr;
    const auto& buf = m_recBufs[static_cast<std::size_t>(trackId)];
    return buf ? buf->data() : nullptr;
}

std::uint64_t LooperEngine::recordingBufferWritten(int trackId) const noexcept {
    if (trackId < 0 || trackId >= kNumTracks) return 0;
    return m_snapRecordingWritten[static_cast<std::size_t>(trackId)]
        .load(std::memory_order_relaxed);
}

LooperEngine::Snapshot LooperEngine::snapshot() const noexcept {
    Snapshot s;
    s.samplePosition = m_snapSamplePos.load(std::memory_order_relaxed);
    s.totalLengthSamples = m_snapTotalLen.load(std::memory_order_relaxed);
    s.currentBar = m_snapBar.load(std::memory_order_relaxed);
    s.currentBeat = m_snapBeat.load(std::memory_order_relaxed);
    s.totalBars = m_snapTotalBars.load(std::memory_order_relaxed);
    s.playing = m_snapPlaying.load(std::memory_order_relaxed);
    s.bpm = bitsToDouble(m_snapBpmBits.load(std::memory_order_relaxed));
    s.beatsPerBar = m_snapBeatsPerBar.load(std::memory_order_relaxed);
    s.metronomeEnabled = m_snapMetronomeEnabled.load(std::memory_order_relaxed);
    s.selectedTrackId = m_snapSelectedTrackId.load(std::memory_order_relaxed);
    for (int i = 0; i < kNumTracks; ++i) {
        const auto ix = static_cast<std::size_t>(i);
        s.trackArmed[ix] = m_snapArmed[ix].load(std::memory_order_relaxed);
        s.trackMuted[ix] = m_snapMuted[ix].load(std::memory_order_relaxed);
        s.trackBars[ix] = m_snapBars[ix].load(std::memory_order_relaxed);
        s.trackMode[ix] = m_snapMode[ix].load(std::memory_order_relaxed);
        s.trackRecordState[ix] = m_snapRecordState[ix].load(std::memory_order_relaxed);
        s.trackLoopCount[ix] = m_snapLoopCount[ix].load(std::memory_order_relaxed);
        s.trackActiveLoopIx[ix] = m_snapActiveLoopIx[ix].load(std::memory_order_relaxed);
        s.trackInputCh[ix] = m_snapInputCh[ix].load(std::memory_order_relaxed);
    }
    return s;
}

}  // namespace loopa
