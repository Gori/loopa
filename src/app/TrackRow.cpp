#include "TrackRow.h"

#include "EngineBridge.h"
#include "Theme.h"
#include "WaveformView.h"

#include "core/Track.h"

namespace loopa::app {

namespace {

const char* modeLabel(int modeInt) {
    switch (static_cast<loopa::RecordMode>(modeInt)) {
        case loopa::RecordMode::New:     return "NEW";
        case loopa::RecordMode::Overdub: return "OVERDUB";
        case loopa::RecordMode::Replace: return "REPLACE";
    }
    return "NEW";
}

// Layout constants (see plan file).
constexpr int kNumberColumnWidth = 48;
constexpr int kChipColumnWidth   = 74;
constexpr int kChipHeight        = 24;
constexpr int kChipHGap          = 8;
constexpr int kChipVGap          = 4;
constexpr int kMuteIconSize      = 28;
constexpr int kInnerPadX         = 12;
constexpr int kInnerPadY         = 10;

// Total horizontal strip = number | chip-grid | mute | paddings.
constexpr int kLeftStripWidth =
    kInnerPadX                // outer left pad
  + kNumberColumnWidth
  + kInnerPadX                // gap between number and grid
  + 2 * kChipColumnWidth + kChipHGap   // 2x2 grid width
  + kInnerPadX                // gap between grid and mute
  + kMuteIconSize
  + kInnerPadX;               // outer right pad

}  // namespace

// ---- TrackNumber -----------------------------------------------------------

TrackNumber::TrackNumber(int trackId, std::function<void()> onClick)
    : m_trackId(trackId), m_onClick(std::move(onClick)) {
    setInterceptsMouseClicks(true, false);
}

void TrackNumber::paint(juce::Graphics& g) {
    const auto bg = m_selected
                      ? theme::col(theme::kText)
                      : theme::col(theme::kText).withAlpha(0.55f);
    g.setColour(bg);
    g.setFont(juce::Font(juce::FontOptions(40.0f).withStyle("Bold")));
    const auto text = juce::String::formatted("%02d", m_trackId + 1);
    g.drawFittedText(text, getLocalBounds(), juce::Justification::centred, 1);
}

void TrackNumber::mouseUp(const juce::MouseEvent& e) {
    if (!contains(e.getPosition())) return;
    if (m_onClick) m_onClick();
}

void TrackNumber::setSelected(bool selected) {
    if (m_selected == selected) return;
    m_selected = selected;
    repaint();
}

// ---- TrackRow --------------------------------------------------------------

TrackRow::TrackRow(EngineBridge& bridge, int trackId)
    : m_bridge(bridge), m_trackId(trackId) {

    m_number = std::make_unique<TrackNumber>(trackId, [this] {
        m_bridge.selectTrack(m_trackId);
    });
    addAndMakeVisible(*m_number);

    m_loopChip.setLabel({});
    m_loopChip.setValue("-");
    m_loopChip.setAccentColour(theme::trackColour(trackId));
    m_loopChip.setOnClick([this] { m_bridge.cycleNextLoop(m_trackId); });
    addAndMakeVisible(m_loopChip);

    m_input.setLabel("IN");
    m_input.setAccentColour(theme::trackColour(trackId));
    m_input.setOnClick([this] { cycleInput(); });
    addAndMakeVisible(m_input);

    m_bars.setLabel("BARS");
    m_bars.setAccentColour(theme::trackColour(trackId));
    m_bars.setOnClick([this] { cycleBars(); });
    addAndMakeVisible(m_bars);

    m_mode.setLabel("NEW");
    m_mode.setAccentColour(theme::trackColour(trackId));
    m_mode.setOnClick([this] { cycleMode(); });
    addAndMakeVisible(m_mode);

    m_mute.setIcon(ChipIcon::SpeakerMute);
    m_mute.setDrawBackground(false);
    m_mute.setAccentColour(theme::trackColour(trackId));
    m_mute.setOnClick([this] { toggleMute(); });
    addAndMakeVisible(m_mute);

    m_waveform = std::make_unique<WaveformView>(m_bridge.engine(), m_trackId);
    addAndMakeVisible(*m_waveform);
}

void TrackRow::paint(juce::Graphics& g) {
    const auto bg = m_selected
                      ? theme::trackRowSelectedBg(m_trackId)
                      : theme::col(theme::kBg1);
    g.fillAll(bg);

    g.setColour(theme::col(theme::kLine));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void TrackRow::mouseDown(const juce::MouseEvent&) {
    // Clicking empty row background selects the track. Chips / mute / number
    // consume their own events before this fires.
    m_bridge.selectTrack(m_trackId);
}

void TrackRow::resized() {
    auto bounds = getLocalBounds();
    bounds.removeFromBottom(1);  // separator line

    auto left = bounds.removeFromLeft(kLeftStripWidth).reduced(kInnerPadX, kInnerPadY);

    // Number column.
    m_number->setBounds(left.removeFromLeft(kNumberColumnWidth));
    left.removeFromLeft(kInnerPadX);

    // 2x2 chip grid.
    const int gridWidth = 2 * kChipColumnWidth + kChipHGap;
    auto gridArea = left.removeFromLeft(gridWidth);

    // Vertically centre the grid (2 rows of chips) in the row.
    const int gridHeight = 2 * kChipHeight + kChipVGap;
    gridArea = gridArea.withSizeKeepingCentre(gridArea.getWidth(), gridHeight);

    auto topRow = gridArea.removeFromTop(kChipHeight);
    gridArea.removeFromTop(kChipVGap);
    auto bottomRow = gridArea.removeFromTop(kChipHeight);

    m_loopChip.setBounds(topRow.removeFromLeft(kChipColumnWidth));
    topRow.removeFromLeft(kChipHGap);
    m_bars.setBounds(topRow.removeFromLeft(kChipColumnWidth));

    m_input.setBounds(bottomRow.removeFromLeft(kChipColumnWidth));
    bottomRow.removeFromLeft(kChipHGap);
    m_mode.setBounds(bottomRow.removeFromLeft(kChipColumnWidth));

    left.removeFromLeft(kInnerPadX);

    // Mute icon — vertically centred.
    auto muteArea = left.removeFromLeft(kMuteIconSize)
                         .withSizeKeepingCentre(kMuteIconSize, kMuteIconSize);
    m_mute.setBounds(muteArea);

    if (m_waveform) {
        m_waveform->setBounds(bounds);
    }
}

void TrackRow::update(const loopa::LooperEngine::Snapshot& s) {
    const auto ix = static_cast<std::size_t>(m_trackId);

    const bool muted     = s.trackMuted[ix];
    const int  barsVal   = s.trackBars[ix];
    const int  modeVal   = s.trackMode[ix];
    const int  recState  = s.trackRecordState[ix];
    const int  loopCount = s.trackLoopCount[ix];
    const int  activeIx  = s.trackActiveLoopIx[ix];
    const int  inputCh   = s.trackInputCh[ix];
    const bool selected  = (s.selectedTrackId == m_trackId);

    // Number visual state
    if (m_number) m_number->setSelected(selected);

    // Loop chip: "A / B / C" for active slot, or "-" when empty.
    if (loopCount > 0 && activeIx >= 0) {
        const char letter = static_cast<char>('A' + (activeIx % 26));
        m_loopChip.setValue(juce::String::charToString(letter)
                             + juce::String(juce::CharPointer_UTF8(" \xE2\x96\xBE")));
    } else {
        m_loopChip.setValue(juce::String(juce::CharPointer_UTF8("\xE2\x96\xBE")));
    }
    m_loopChip.setHot(recState == static_cast<int>(loopa::RecordState::Recording));

    // Input chip: "IN 1", "IN 2", ...
    m_input.setValue(juce::String(inputCh + 1));

    // Bars / Mode
    m_bars.setValue(juce::String(barsVal));
    m_mode.setLabel(modeLabel(modeVal));

    // Mute icon active-state == muted
    m_mute.setActive(muted);

    if (selected != m_selected) {
        m_selected = selected;
        repaint();
    }

    // Drive this row's waveform render from the main timer's single tick so
    // every track in the window encodes its Metal commands in the same
    // message-loop iteration — no cross-track frame drift.
    if (m_waveform) m_waveform->renderNow(s);
}

void TrackRow::cycleBars() {
    const int current = m_bridge.snapshot().trackBars[static_cast<std::size_t>(m_trackId)];
    int next = 1;
    switch (current) {
        case 1:  next = 2;  break;
        case 2:  next = 4;  break;
        case 4:  next = 8;  break;
        case 8:  next = 16; break;
        case 16: next = 1;  break;
        default: next = 4;  break;
    }
    m_bridge.setBars(m_trackId, next);
}

void TrackRow::cycleMode() {
    const int current = m_bridge.snapshot().trackMode[static_cast<std::size_t>(m_trackId)];
    int next = (current + 1) % 3;
    m_bridge.setMode(m_trackId, next);
}

void TrackRow::cycleInput() {
    // V1: assume 2 available input channels. Cycle 0 → 1 → 0.
    const int current = m_bridge.snapshot().trackInputCh[static_cast<std::size_t>(m_trackId)];
    const int next = (current + 1) % 2;
    m_bridge.setInputCh(m_trackId, next);
}

void TrackRow::toggleMute() {
    const bool curr = m_bridge.snapshot().trackMuted[static_cast<std::size_t>(m_trackId)];
    m_bridge.setMuted(m_trackId, !curr);
}

}  // namespace loopa::app
