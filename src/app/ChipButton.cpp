#include "ChipButton.h"

#include "Theme.h"

namespace loopa::app {

ChipButton::ChipButton()
    : m_accent(theme::col(theme::kAccent)) {
}

void ChipButton::setLabel(juce::String label) {
    m_label = std::move(label);
    repaint();
}

void ChipButton::setValue(juce::String value) {
    m_value = std::move(value);
    repaint();
}

void ChipButton::setAccentColour(juce::Colour c) {
    m_accent = c;
    repaint();
}

void ChipButton::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    repaint();
}

void ChipButton::setHot(bool hot) {
    if (m_hot == hot) return;
    m_hot = hot;
    repaint();
}

void ChipButton::setSelected(bool selected) {
    if (m_selected == selected) return;
    m_selected = selected;
    repaint();
}

void ChipButton::setEnabled2(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    repaint();
}

void ChipButton::setOnClick(std::function<void()> onClick) {
    m_onClick = std::move(onClick);
}

void ChipButton::setIcon(ChipIcon i) {
    if (m_icon == i) return;
    m_icon = i;
    repaint();
}

void ChipButton::setDrawBackground(bool draw) {
    if (m_drawBackground == draw) return;
    m_drawBackground = draw;
    repaint();
}

namespace {

// Simple vector speaker silhouette inside the given rect. `slashed` overlays a
// diagonal line to indicate muted state. All coordinates are in the caller's
// paint space.
void paintSpeakerIcon(juce::Graphics& g, juce::Rectangle<float> r,
                       juce::Colour colour, bool slashed) {
    const float w = r.getWidth();
    const float h = r.getHeight();
    const float cx = r.getX();
    const float cy = r.getY();

    // Speaker body: small rectangle + triangular horn.
    juce::Path speaker;
    const float bodyW = w * 0.22f;
    const float bodyH = h * 0.32f;
    const float bodyX = cx + w * 0.14f;
    const float bodyY = cy + (h - bodyH) * 0.5f;
    speaker.addRectangle(bodyX, bodyY, bodyW, bodyH);

    const float hornLeft  = bodyX + bodyW;
    const float hornRight = cx + w * 0.58f;
    const float hornTop   = cy + h * 0.18f;
    const float hornBot   = cy + h * 0.82f;
    speaker.startNewSubPath(hornLeft, bodyY);
    speaker.lineTo(hornRight, hornTop);
    speaker.lineTo(hornRight, hornBot);
    speaker.lineTo(hornLeft, bodyY + bodyH);
    speaker.closeSubPath();

    g.setColour(colour);
    g.fillPath(speaker);

    if (slashed) {
        // Diagonal slash from upper-right to lower-left.
        const float slashThickness = std::max(1.5f, w * 0.08f);
        juce::Path slash;
        slash.startNewSubPath(cx + w * 0.86f, cy + h * 0.14f);
        slash.lineTo(cx + w * 0.14f, cy + h * 0.86f);
        g.strokePath(slash, juce::PathStrokeType(slashThickness,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    } else {
        // Two short sound-wave arcs on the right of the horn when unmuted.
        const float arcX   = cx + w * 0.64f;
        const float arcCy  = cy + h * 0.50f;
        const float arcR1  = w * 0.10f;
        const float arcR2  = w * 0.18f;
        juce::Path arcs;
        arcs.addCentredArc(arcX, arcCy, arcR1, arcR1, 0.0f,
                            -0.9f, 0.9f, true);
        arcs.addCentredArc(arcX, arcCy, arcR2, arcR2, 0.0f,
                            -0.9f, 0.9f, true);
        g.strokePath(arcs, juce::PathStrokeType(std::max(1.0f, w * 0.05f)));
    }
}

}  // namespace

void ChipButton::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    juce::Colour bg = theme::col(theme::kBg2);
    juce::Colour fg = theme::col(theme::kText);
    juce::Colour accent = m_accent;

    if (m_hot) {
        bg = accent;
        fg = theme::col(theme::kBg0);
    } else if (m_active) {
        bg = accent.withAlpha(0.16f);
        fg = accent;
    }

    if (!m_enabled) {
        bg = theme::col(theme::kBg2);
        fg = theme::col(theme::kTextDim);
    }

    if (m_drawBackground) {
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, theme::kCornerRadius);
    }

    if (m_selected) {
        g.setColour(accent);
        g.drawRoundedRectangle(bounds, theme::kCornerRadius, theme::kStrokeWidth);
    }

    if (m_icon == ChipIcon::SpeakerMute) {
        // Colour rule: dim grey when unmuted (m_active == false), track-coloured
        // when muted (m_active == true). `m_hot` not used for icons.
        const juce::Colour iconCol = m_active ? accent : theme::col(theme::kTextDim);
        paintSpeakerIcon(g, bounds, iconCol, m_active);
        return;
    }

    g.setColour(fg);
    g.setFont(theme::fontLabel().withHeight(11.0f));

    auto text = m_label.isEmpty() ? m_value
              : (m_value.isEmpty() ? m_label : m_label + " " + m_value);
    g.drawFittedText(text, getLocalBounds(), juce::Justification::centred, 1);
}

void ChipButton::mouseUp(const juce::MouseEvent& e) {
    if (!m_enabled || !contains(e.getPosition())) return;
    if (m_onClick) m_onClick();
}

}  // namespace loopa::app
