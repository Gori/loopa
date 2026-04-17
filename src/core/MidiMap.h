#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace loopa {

enum class MidiAction : std::uint8_t {
    StartStopRecord,
    CycleNext,
    CyclePrev,
    MuteSelected,
    NextLoopSelected,
};

inline constexpr std::size_t kMidiActionCount = 5;

struct MidiTrigger {
    enum class Kind : std::uint8_t { None, Note, Cc };
    Kind kind = Kind::None;
    int channel = 0;   // 0-15 (MIDI channels 1-16 shown to the user as +1)
    int number = 0;    // note number (0-127) or CC number (0-127)

    bool valid() const noexcept {
        return kind != Kind::None
            && channel >= 0 && channel < 16
            && number >= 0 && number < 128;
    }

    bool equals(const MidiTrigger& other) const noexcept {
        return kind == other.kind
            && channel == other.channel
            && number == other.number;
    }
};

// Holds one MIDI trigger per action (or none, meaning unmapped). A newly bound
// trigger unbinds any other action that previously used the same trigger
// (last-wins semantics — simpler than reporting conflicts).
class MidiMap {
public:
    MidiMap() = default;

    void bind(MidiAction action, MidiTrigger trigger) {
        if (trigger.kind == MidiTrigger::Kind::None) {
            m_triggers[static_cast<std::size_t>(action)] = {};
            return;
        }
        for (auto& t : m_triggers) {
            if (t.equals(trigger)) {
                t = {};
            }
        }
        m_triggers[static_cast<std::size_t>(action)] = trigger;
    }

    void unbind(MidiAction action) {
        m_triggers[static_cast<std::size_t>(action)] = {};
    }

    MidiTrigger get(MidiAction action) const {
        return m_triggers[static_cast<std::size_t>(action)];
    }

    // Returns the action matched by an incoming MIDI message (Note or CC), if any.
    std::optional<MidiAction> match(MidiTrigger incoming) const {
        if (incoming.kind == MidiTrigger::Kind::None) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < m_triggers.size(); ++i) {
            if (m_triggers[i].equals(incoming)) {
                return static_cast<MidiAction>(i);
            }
        }
        return std::nullopt;
    }

    void clear() {
        for (auto& t : m_triggers) { t = {}; }
    }

    const std::array<MidiTrigger, kMidiActionCount>& triggers() const { return m_triggers; }

private:
    std::array<MidiTrigger, kMidiActionCount> m_triggers{};
};

}  // namespace loopa
