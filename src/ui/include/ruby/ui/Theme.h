#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

// Design tokens. Values are literal and intentional: colours, row heights and panel
// widths are fixed, not suggestions. Everything is square except traffic lights, tab dots,
// stopwatches, and A/V dots. Depth comes from 1px borders and value steps, not
// shadows.

namespace ruby::ui::theme {

// --- Chrome -----------------------------------------------------------------
inline const QColor kTitlebar{"#3c3c3c"};
inline const QColor kTitlebarText{"#b8b8b8"};
inline const QColor kMenuBar{"#333333"};
inline const QColor kMenuLabel{"#c8c8c8"};
inline const QColor kMenuActive{"#4a4a4a"};
inline const QColor kToolBar{"#2b2b2b"};

// --- Panels -----------------------------------------------------------------
inline const QColor kGutter{"#1a1a1a"};        // page behind panels, 2px grid
inline const QColor kPanelBody{"#1e1e1e"};
inline const QColor kPanelBorder{"#2f2f2f"};
inline const QColor kTabStrip{"#2b2b2b"};
inline const QColor kTabActiveBg{"#1e1e1e"};
inline const QColor kTabActiveText{"#e5e5e5"};
inline const QColor kTabInactiveText{"#8f8f8f"};
inline const QColor kTabDotActive{"#4a9eda"};
inline const QColor kTabDotInactive{"#555555"};
inline const QColor kDivider{"#111111"};       // hard rule
inline const QColor kRuleSoft{"#161616"};
inline const QColor kRuleSecondary{"#1a1a1a"};
inline const QColor kColumnHeader{"#262626"};
inline const QColor kColumnHeaderText{"#8f8f8f"};
inline const QColor kSubToolbar{"#232323"};
inline const QColor kFieldBg{"#141414"};       // inset: search, numeric well
inline const QColor kFieldBorder{"#0d0d0d"};
inline const QColor kButtonBg{"#2b2b2b"};
inline const QColor kButtonBorder{"#111111"};
inline const QColor kButtonText{"#c8c8c8"};
inline const QColor kPrimaryBg{"#4a7fb5"};
inline const QColor kPrimaryBorder{"#2f5f92"};
inline const QColor kPrimaryText{"#ffffff"};
inline const QColor kToggleActiveBg{"#4a4a4a"};
inline const QColor kToggleActiveText{"#f0f0f0"};

// Track is darker than the panel so it reads as a
// groove rather than a border, and the handle is light enough to look like a control.
inline const QColor kScrollTrack{"#161616"};
inline const QColor kScrollHandle{"#4a4a4a"};
inline const QColor kScrollHandleHover{"#5f5f5f"};

// --- Text -------------------------------------------------------------------
inline const QColor kTextPrimary{"#e5e5e5"};
inline const QColor kTextBody{"#d5d5d5"};
inline const QColor kTextSecondary{"#b8b8b8"};
inline const QColor kTextTertiary{"#a8a8a8"};
inline const QColor kTextDim{"#8f8f8f"};
inline const QColor kTextDimmer{"#7a7a7a"};
inline const QColor kTextFaint{"#6a6a6a"};
inline const QColor kTextSelectedLayer{"#ffffff"};

// --- Editor semantics -------------------------------------------------------
inline const QColor kAccent{"#4a9eda"};        // current-time indicator, active stopwatch
inline const QColor kStopwatchFill{"#2b4b6b"};
inline const QColor kRowSelected{"#2f4358"};
inline const QColor kRowTimeline{"#2b2b2b"};
inline const QColor kRowProperty{"#212121"};
inline const QColor kValueScrubbable{"#f0a63c"};
inline const QColor kValueUnderline{"#6a5a3a"};
inline const QColor kKeyframe{"#c8c8c8"};
inline const QColor kKeyframeSelected{"#4a9eda"};
inline const QColor kKeyframeBorder{"#111111"};
inline const QColor kKeyConnector{"#4a4a4a"};
inline const QColor kExpressionText{"#9fd18f"};
inline const QColor kCacheReady{"#7cb342"};
// Green for RAM, blue for disk, under the ruler. AE's colours on purpose: every person
// this app is for has already learned them, and the entire value of a visible cache is
// that it is read at a glance rather than studied.
inline const QColor kCacheRam{"#4c8b2b"};
inline const QColor kCacheDisk{"#2f6690"};
// The work area's ends. Bright enough to grab, dim enough not to compete with the
// playhead, which is the one thing on the ruler that must always win.
inline const QColor kWorkAreaEdge{"#8a8a8a"};
inline const QColor kWorkAreaOutside{"#000000"};
inline const QColor kGraphBg{"#1b1b1b"};
inline const QColor kGraphGrid{"#232323"};
inline const QColor kTrackBg{"#232323"};
inline const QColor kRulerTick{"#3a3a3a"};

// --- Layer label colors -----------------------------------------------------
struct LayerLabel {
    QColor stripe;   // 3px stripe on the layer name
    QColor bar;      // timeline bar fill
    QColor topEdge;  // bar top edge
};

inline const LayerLabel kLabelLavender{QColor("#b8a4d8"), QColor("#6e5b8a"), QColor("#9a86b8")};
inline const LayerLabel kLabelLavender2{QColor("#b8a4d8"), QColor("#5d4d75"), QColor("#8674a3")};
inline const LayerLabel kLabelAqua{QColor("#7dabd8"), QColor("#3f5f7d"), QColor("#6b8dab")};
inline const LayerLabel kLabelGray{QColor("#8f8f8f"), QColor("#4a4a4a"), QColor("#6f6f6f")};
inline const LayerLabel kLabelGreen{QColor("#9fd18f"), QColor("#4c6b40"), QColor("#75955f")};

// --- Geometry ---------------------------------------------------------------
// Fixed and dense. These are not suggestions.
namespace metrics {
inline constexpr int kGutter = 2;

inline constexpr int kTitlebarH = 34;
inline constexpr int kMenuBarH = 26;
inline constexpr int kToolBarH = 30;
inline constexpr int kTabStripH = 26;
inline constexpr int kSubToolbarH = 26;
// The ruler's text row: column names on the left, timecodes on the right.
inline constexpr int kColumnLabelH = 20;
// Under it, the work area and then what is cached. Green for RAM, blue for disk.
inline constexpr int kWorkAreaH = 6;
inline constexpr int kCacheBarH = 4;

// The whole header, which is what every row offset below it is measured from.
//
// Summed rather than written down, and the bars added INSIDE it rather than below it, so
// that every existing "content starts here" calculation stayed correct without being
// touched. Two new strips across the top of a panel is exactly the change that produces
// the overlap bugs this file keeps collecting comments about.
inline constexpr int kColumnHeaderH = kColumnLabelH + kWorkAreaH + kCacheBarH;

inline constexpr int kProjectRowH = 22;
inline constexpr int kProjectFooterH = 22;
inline constexpr int kLayerRowH = 26;
inline constexpr int kPropertyRowH = 22;
inline constexpr int kGraphStripH = 104;
inline constexpr int kInspectorGroupH = 22;
inline constexpr int kInspectorRowH = 21;
inline constexpr int kKeyframeRowH = 22;

inline constexpr int kProjectPanelW = 250;
inline constexpr int kInspectorPanelW = 268;
inline constexpr int kBrowserPanelW = 494;
// The keyframe navigator has a column of its own, between Parent and the track.
//
// It used to be drawn at trackLeft() - kNavW, which is exactly where Parent already was:
// on a property row nothing else lived there so it looked fine, and on a layer row it
// printed straight over the word "None". Two things sharing a column is not a layout, it
// is two layouts that happen not to have collided yet.
inline constexpr int kKeyNavW = 52;

// Layer column sub-widths, left to right, matching the order AE lays them out in:
//   A/V | # | Layer Name | switches | Mode | T | Track Matte | Parent | keys
inline constexpr int kAvToggleW = 78;  // eye, audio, solo, lock
inline constexpr int kIndexW = 20;
inline constexpr int kLayerNameW = 182;
inline constexpr int kSwitchW = 14;
inline constexpr int kSwitchCount = 8;  // shy, collapse, quality, fx, blend, blur, adj, 3D
inline constexpr int kSwitchesW = kSwitchW * kSwitchCount + 4;
inline constexpr int kModeW = 56;
inline constexpr int kPreserveW = 16;  // the "T" box
inline constexpr int kTrkMatW = 74;
inline constexpr int kParentW = 52;

// Summed rather than written down. The number used to be a literal, and every column
// added since has meant editing a total by hand and hoping it still matched what the
// painter did. It does not have to match: it can be derived.
inline constexpr int kLayerColumnW = kAvToggleW + kIndexW + kLayerNameW + kSwitchesW +
                                     kModeW + kPreserveW + kTrkMatW + kParentW + kKeyNavW;

inline constexpr int kToolButtonW = 24;
inline constexpr int kToolButtonH = 22;
inline constexpr int kLayerBarH = 15;
inline constexpr int kKeyframeSize = 8;
inline constexpr int kScrollBarW = 12;

// "All UI transitions should be effectively instant. This is a dense pro tool."
inline constexpr int kMaxTransitionMs = 80;
}  // namespace metrics

// --- Type -------------------------------------------------------------------
// Archivo (UI) and Space Mono (code). Neither is bundled yet, so these fall back to
// the platform's nearest compact grotesque and monospace faces.
QString uiFontFamily();
QString monoFontFamily();

// Numbers, timecodes, counts and tick labels.
//
// Numerals use the interface face rather than a monospace one, so they match the rest
// of the UI instead of looking like a terminal. Kept as its own function rather than
// calling uiFontFamily() at each site, so this is one decision in one place, and so
// genuine code (expressions) can keep a real monospace font.
//
// Tabular figures are requested, which matters more than it sounds: with proportional
// digits a value jitters left and right while you scrub it, because a 1 is narrower than
// a 0. Tabular locks every digit to the same advance.
[[nodiscard]] QFont numericFont(int pixelSize);

namespace type {
inline constexpr int kScreenHeading = 15;
inline constexpr int kSubtitle = 13;
inline constexpr int kTimeReadout = 12;   // mono
inline constexpr int kTitlebar = 12;      // 11.5 rounded
inline constexpr int kTabLabel = 11;
inline constexpr int kRowLabel = 11;      // layer names, project items, presets
inline constexpr int kPropertyLabel = 11; // 10.5 rounded
inline constexpr int kColumnHeader = 10;
inline constexpr int kMeta = 10;          // 9.5 rounded, mono
}  // namespace type

// Application palette built from the tokens above. Preferred over a blanket
// `QWidget { background: ... }` stylesheet rule, which propagates into every
// subclass and stops custom-painted widgets from drawing their own chrome.
QPalette palette();

// Stylesheet for the widget classes the palette cannot express. Deliberately
// scoped to named classes only.
QString styleSheet();

}  // namespace ruby::ui::theme
