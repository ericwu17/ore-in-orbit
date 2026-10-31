// ui_theme.h — the dark, industrial look shared by every presentation file:
// the palette, the Dear ImGui / ImPlot style and embedded UI fonts
// (ApplyTheme), and a handful of styled widgets built on top of them.
//
// Every color the UI draws comes from here, so re-theming the app is an edit
// to this file alone.
#pragma once

#include "imgui.h"

namespace ui::theme {

// 0-255 channels -> ImVec4, usable in constant expressions.
constexpr ImVec4 Rgb(int r, int g, int b, int a = 255) {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
}

// ImVec4 -> packed ImU32 for draw-list calls, with an optional alpha multiplier.
constexpr ImU32 U32(ImVec4 c, float alpha = 1.0f) {
    auto channel = [](float v) { return static_cast<ImU32>(v * 255.0f + 0.5f); };
    return IM_COL32(channel(c.x), channel(c.y), channel(c.z), channel(c.w * alpha));
}

// ---------------------------------------------------------------------------
// Palette: gunmetal plates, pale text, hazard amber.
// ---------------------------------------------------------------------------

constexpr ImVec4 kBackdrop    = Rgb(18, 20, 23);     // behind everything (the GL clear color), the grid
constexpr ImVec4 kPanel       = Rgb(31, 35, 40);     // window and panel plates
constexpr ImVec4 kInset       = Rgb(22, 25, 29);     // inputs, plot areas, popups: sunk into the plate
constexpr ImVec4 kFrame       = Rgb(43, 48, 55);     // frames, buttons, icon tiles
constexpr ImVec4 kFrameHover  = Rgb(54, 60, 69);     // hovered frames, rules
constexpr ImVec4 kFrameActive = Rgb(67, 74, 85);     // pressed frames, scrollbar grabs
constexpr ImVec4 kBevelLight  = Rgb(74, 82, 94);     // the lit top / left edge of a plate
constexpr ImVec4 kBevelDark   = Rgb(11, 12, 14);     // the shaded bottom / right edge, borders
constexpr ImVec4 kText        = Rgb(216, 219, 224);  // text, icons
constexpr ImVec4 kTextDim     = Rgb(138, 145, 155);  // secondary text, inactive icons
constexpr ImVec4 kShadow      = Rgb(0, 0, 0);        // drop shadows (used at low alpha)

constexpr ImVec4 kAccent       = Rgb(242, 163, 43);  // hazard amber: primary accent, selection
constexpr ImVec4 kAccentHover  = Rgb(255, 184, 74);
constexpr ImVec4 kAccentActive = Rgb(201, 133, 22);
constexpr ImVec4 kAccentText   = Rgb(20, 18, 14);    // text on amber
constexpr ImVec4 kSteel        = Rgb(91, 120, 150);  // secondary accent: progress, links, counts
constexpr ImVec4 kSteelHover   = Rgb(112, 145, 178);

constexpr ImVec4 kGood       = Rgb(95, 174, 110);   // income, good outcomes
constexpr ImVec4 kBad        = Rgb(224, 69, 44);    // losses, errors
constexpr ImVec4 kBadHover   = Rgb(240, 92, 66);
constexpr ImVec4 kBadActive  = Rgb(184, 52, 32);
constexpr ImVec4 kWarn       = Rgb(242, 193, 46);   // nearly full, piling up
constexpr ImVec4 kWarnText   = kWarn;               // readable as is on the dark plates
constexpr ImVec4 kTeal       = Rgb(72, 168, 178);
constexpr ImVec4 kPlum       = Rgb(160, 108, 150);

// Ship build grid (the game screen).
constexpr ImVec4 kGridLine           = Rgb(44, 49, 56);    // empty cells
constexpr ImVec4 kGridMajor          = Rgb(58, 65, 75);    // every kGridMajorEvery cells
constexpr ImVec4 kTileHull           = Rgb(90, 97, 107);   // steel deck plating
constexpr ImVec4 kTileBridge         = kAccentActive;
constexpr ImVec4 kTileCollector      = Rgb(150, 92, 58);   // oxidized copper
constexpr ImVec4 kTileBelt           = Rgb(64, 70, 79);
constexpr ImVec4 kTileExpressBelt    = Rgb(150, 112, 40);
constexpr ImVec4 kTileTunnelMouth    = kBevelDark;
constexpr ImVec4 kTileStorage        = Rgb(112, 98, 72);   // cargo holds
constexpr ImVec4 kTileMachine        = Rgb(70, 92, 116);   // blue-grey
constexpr ImVec4 kTilePower          = Rgb(98, 120, 64);   // olive
constexpr ImVec4 kTileSystem         = Rgb(150, 62, 48);   // oxide red
constexpr ImVec4 kTileTurret         = Rgb(48, 52, 58);    // gunmetal
constexpr ImVec4 kTileGlyph          = Rgb(236, 238, 241); // letters and arrows on tiles
constexpr int kGridMajorEvery = 8;

// Items on belts, by material family.
constexpr ImVec4 kItemIron      = Rgb(150, 160, 174);
constexpr ImVec4 kItemCopper    = Rgb(212, 124, 62);
constexpr ImVec4 kItemSilica    = Rgb(228, 204, 150);
constexpr ImVec4 kItemCarbon    = Rgb(52, 48, 46);
constexpr ImVec4 kItemExotic    = kPlum;
constexpr ImVec4 kItemChemical  = kGood;       // plastic
constexpr ImVec4 kItemComponent = kSteelHover;
constexpr ImVec4 kItemPlaceable = kAccent;
constexpr ImVec4 kItemOutline   = kBevelDark;

// Unscaled font sizes, as passed to ImGui::PushFont(nullptr, size); the DPI
// scale is applied on top by style.FontScaleDpi.
constexpr float kFontSmall = 12.5f;
constexpr float kFontBody  = 16.0f;
constexpr float kFontTitle = 20.0f;
constexpr float kFontHuge  = 30.0f;

// Loads the embedded UI fonts and installs the ImGui and ImPlot styles. Call
// once, after both contexts exist and before the first frame.
void ApplyTheme(float dpi_scale);

// The monospace font (headings, numbers, key caps), at `size`: pair with
// PopMono().
void PushMono(float size = kFontBody);
void PopMono();

// ---------------------------------------------------------------------------
// Styled widgets
// ---------------------------------------------------------------------------

// The main action of a panel: amber fill, dark text.
bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));

// A destructive action (confirming a delete): red fill, hazard stripes
// along its bottom edge while hovered.
bool DangerButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));

// A small, borderless button in the "bad" color, for removing things.
bool DangerSmallButton(const char* label);

// One option of a segmented control (lay several out with SameLine): the
// selected one is filled steel. Returns true when clicked.
bool SegmentedButton(const char* label, bool selected, ImVec2 size = ImVec2(0.0f, 0.0f));

// A key cap ("R", "LMB"): a small bevelled plate around mono text, laid
// out like a text item.
void KeyCap(const char* key);

// A small square button with a drawn pencil icon, one text line tall, for
// switching a displayed value into an edit field. Returns true when clicked.
bool PencilButton(const char* str_id);

// A small square button with a drawn cross, one text line tall, for closing
// a section. Returns true when clicked.
bool CloseButton(const char* str_id);

// A dimmed "(?)" that shows `text` in a wrapped tooltip on hover.
void HelpMarker(const char* text);

// A heading: a short amber block, the title in capitals (monospace), and a
// thin rule with tick marks under it.
void SectionHeading(const char* text);

// SectionHeading() that folds what's under it away when clicked, with a
// chevron saying which way it is; returns whether it's open. Its state is
// kept by ImGui per label (in the current ID scope).
bool CollapsingSection(const char* text, bool default_open = true);

// A gunmetal plate with bevelled edges and a rivet in each corner, drawn
// straight into a draw list.
void DrawPlatePanel(ImDrawList* draw, ImVec2 min, ImVec2 max);

// Diagonal hazard stripes in `color` over `min`..`max` (clipped to it).
void DrawHazardStripes(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 color, float alpha = 1.0f);

// Text with a one-pixel halo all around, so labels drawn over busy
// backgrounds stay readable.
void DrawHaloText(ImDrawList* draw, ImVec2 pos, ImU32 color, ImU32 halo, const char* text);

}  // namespace ui::theme
