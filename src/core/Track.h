#pragma once

#include "core/Loop.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace loopa {

enum class RecordMode : std::uint8_t {
    New,       // record a new loop into a new slot
    Overdub,   // destructively sum new audio into the active loop
    Replace,   // overwrite the active loop's samples
};

enum class RecordState : std::uint8_t {
    Idle,       // not recording; may be playing
    CountIn,    // waiting for the transport to reach bar 0 before recording starts
    Recording,  // capturing input into the current RecordingBuffer
};

inline constexpr std::array<int, 5> kAllowedBars{1, 2, 4, 8, 16};

inline bool isAllowedBarCount(int bars) noexcept {
    for (int b : kAllowedBars) {
        if (b == bars) return true;
    }
    return false;
}

class Track {
public:
    explicit Track(int id) : m_id(id) {}

    int id() const noexcept         { return m_id; }

    bool armed() const noexcept     { return m_armed; }
    void setArmed(bool v) noexcept  { m_armed = v; }

    bool muted() const noexcept     { return m_muted; }
    void setMuted(bool v) noexcept  { m_muted = v; }

    int inputCh() const noexcept    { return m_inputCh; }
    void setInputCh(int ch) noexcept { m_inputCh = ch; }

    int bars() const noexcept       { return m_bars; }
    void setBars(int b) noexcept {
        if (isAllowedBarCount(b)) {
            m_bars = b;
        }
    }

    RecordMode mode() const noexcept      { return m_mode; }
    void setMode(RecordMode m) noexcept   { m_mode = m; }

    RecordState recordState() const noexcept { return m_recordState; }
    void setRecordState(RecordState s) noexcept { m_recordState = s; }

    int activeLoopIx() const noexcept { return m_activeLoopIx; }
    void setActiveLoopIx(int i) noexcept {
        if (i >= 0 && static_cast<std::size_t>(i) < m_loops.size()) {
            m_activeLoopIx = i;
        } else if (i < 0) {
            m_activeLoopIx = -1;
        }
    }

    std::size_t loopCount() const noexcept { return m_loops.size(); }

    std::shared_ptr<Loop> loopAt(std::size_t i) const {
        if (i >= m_loops.size()) return nullptr;
        return m_loops[i];
    }

    std::shared_ptr<Loop> activeLoop() const {
        if (m_activeLoopIx < 0) return nullptr;
        return m_loops[static_cast<std::size_t>(m_activeLoopIx)];
    }

    // Append a new loop and make it active. Returns its index.
    int addLoop(std::shared_ptr<Loop> loop) {
        m_loops.push_back(std::move(loop));
        m_activeLoopIx = static_cast<int>(m_loops.size()) - 1;
        return m_activeLoopIx;
    }

    // Append a loop without changing the active index (unless there was no
    // active loop, in which case the new one becomes active). Used by the
    // timbre-transfer path, which defers activation to the next bar-0
    // crossing so the switch lands phase-aligned with the playing loop.
    int appendLoop(std::shared_ptr<Loop> loop) {
        m_loops.push_back(std::move(loop));
        const int idx = static_cast<int>(m_loops.size()) - 1;
        if (m_activeLoopIx < 0) m_activeLoopIx = idx;
        return idx;
    }

    // Remove the loop at index i. If it was active, select the next loop
    // (or -1 if the list is now empty).
    void removeLoop(int i) {
        if (i < 0 || static_cast<std::size_t>(i) >= m_loops.size()) {
            return;
        }
        m_loops.erase(m_loops.begin() + i);
        if (m_loops.empty()) {
            m_activeLoopIx = -1;
        } else if (m_activeLoopIx >= static_cast<int>(m_loops.size())) {
            m_activeLoopIx = static_cast<int>(m_loops.size()) - 1;
        }
    }

    // Cycle to the next loop (wraps).
    void cycleNextLoop() {
        if (m_loops.empty()) return;
        m_activeLoopIx = (m_activeLoopIx + 1) % static_cast<int>(m_loops.size());
    }

    // Replace the active loop's pointer (used by Overdub/Replace commands after
    // they build a new version of the loop).
    void replaceActiveLoop(std::shared_ptr<Loop> loop) {
        if (m_activeLoopIx < 0) return;
        m_loops[static_cast<std::size_t>(m_activeLoopIx)] = std::move(loop);
    }

private:
    int m_id;
    bool m_armed = false;
    bool m_muted = false;
    int m_inputCh = 0;
    int m_bars = 4;                               // default, overridden by first recording
    RecordMode m_mode = RecordMode::New;
    RecordState m_recordState = RecordState::Idle;
    int m_activeLoopIx = -1;
    std::vector<std::shared_ptr<Loop>> m_loops;
};

}  // namespace loopa
