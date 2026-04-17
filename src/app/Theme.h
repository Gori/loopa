#pragma once

#include <juce_graphics/juce_graphics.h>

namespace loopa::app::theme {

constexpr juce::uint32 kBg0     = 0xFF0B0B0DU;
constexpr juce::uint32 kBg1     = 0xFF141418U;
constexpr juce::uint32 kBg2     = 0xFF1E1E24U;
constexpr juce::uint32 kLine    = 0xFF2A2A31U;
constexpr juce::uint32 kText    = 0xFFEDEDF0U;
constexpr juce::uint32 kTextDim = 0xFF7E7E88U;
constexpr juce::uint32 kAccent  = 0xFFF4A93AU;
constexpr juce::uint32 kRecord  = 0xFFE5484DU;
constexpr juce::uint32 kPlay    = 0xFF3DDC97U;

constexpr float kCornerRadius = 4.0f;
constexpr float kStrokeWidth  = 1.0f;

constexpr int kTopBarHeight = 56;
constexpr int kPadX = 12;
constexpr int kPadY = 8;
constexpr int kChipGap = 6;

inline juce::Font fontLabel()    { return juce::Font(juce::FontOptions(11.0f).withStyle("Plain")); }
inline juce::Font fontBody()     { return juce::Font(juce::FontOptions(13.0f).withStyle("Plain")); }
inline juce::Font fontNumeric()  { return juce::Font(juce::FontOptions(15.0f).withStyle("Plain")); }
inline juce::Font fontLarge()    { return juce::Font(juce::FontOptions(22.0f).withStyle("Plain")); }

inline juce::Colour col(juce::uint32 argb) { return juce::Colour(argb); }

// Per-track palette. Track 1..4 get distinct hues. Picked so every track has
// roughly the same perceived brightness on the dark background.
constexpr juce::uint32 kTrackBase[4] = {
    0xFFF4A93AU,   // track 1 — amber (same as kAccent, keeps existing look)
    0xFF4AD9E0U,   // track 2 — cyan
    0xFFF39AC9U,   // track 3 — magenta/pink
    0xFF7FDE8CU,   // track 4 — green
};

inline juce::Colour trackColour(int trackId) {
    return col(kTrackBase[((trackId % 4) + 4) % 4]);
}

// Derived roles — all hue-locked to the track colour, varying alpha/brightness.
inline juce::Colour trackPlayed(int t)    { return trackColour(t).withAlpha(0.35f); }
inline juce::Colour trackUpcoming(int t)  { return trackColour(t).withAlpha(0.95f); }
inline juce::Colour trackBar(int t)       { return trackColour(t).withAlpha(0.45f); }
inline juce::Colour trackBeat(int t)      { return trackColour(t).withAlpha(0.20f); }
inline juce::Colour trackLoopStart(int t) { return trackColour(t); }
inline juce::Colour trackPlayhead(int t)  { return trackColour(t).brighter(0.2f); }
inline juce::Colour trackRecording(int t) { return trackColour(t).withAlpha(0.95f); }

// Darkened hue for a selected track row's background. Same hue as the waveform
// but low-brightness + slightly desaturated so white text reads cleanly on top.
inline juce::Colour trackRowSelectedBg(int t) {
    return trackColour(t).withBrightness(0.14f).withSaturation(0.55f);
}

}  // namespace loopa::app::theme
