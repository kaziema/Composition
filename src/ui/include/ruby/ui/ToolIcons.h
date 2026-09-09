#pragma once

#include <QColor>
#include <QRect>

class QPainter;

namespace ruby::ui {

// Drawn rather than set in a font. Text glyphs were tried first and are actively
// wrong: several have Unicode emoji presentation, so the hand rendered as a
// full-colour emoji and broke the tool bar's monochrome run.
//
// These are drawn as vector paths instead. No font dependency, monochrome by
// construction, correct at any DPI.

enum class ToolIcon {
    // Reserved for the project selector window. Drawn but inert for now, and dimmed so
    // that reads as "not yet" rather than as a button that ignores you.
    Home,

    Selection,
    Pan,
    Text,
    Shape,
    Pen,
    Mask,
    Hand,
    Anchor,

    // Panel switches rather than tools: they change what the left dock shows instead of
    // changing what a click in the viewer means. Same bar because they are the same kind
    // of reach, and separated from the tools by a divider so the difference is visible.
    Project,
    Effects,
};

// Paints the icon centred in `box`, tinted `color`. Geometry is authored in a
// 16x16 space and scaled to fit, so call sites only pass the button rect.
void paintToolIcon(QPainter& p, const QRect& box, ToolIcon icon, const QColor& color);

}  // namespace ruby::ui
