// ui_theme.cpp — see ui_theme.h.
#include "ui/ui_theme.h"

#include "implot.h"

#include <algorithm>
#include <cctype>
#include <string>

// Generated from imgui/misc/fonts/Roboto-Medium.ttf and Cousine-Regular.ttf
// by the makefiles: define kRobotoTtf[] and kCousineTtf[].
#include "cousine_ttf.h"
#include "roboto_ttf.h"

namespace ui::theme {

namespace {

ImFont* g_mono = nullptr;

void LoadFonts() {
    ImFontConfig config;
    // The arrays are static data in the executable: the atlas must not free them.
    config.FontDataOwnedByAtlas = false;
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kRobotoTtf), static_cast<int>(sizeof(kRobotoTtf)),
                                kFontBody, &config);
    g_mono = fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kCousineTtf),
                                         static_cast<int>(sizeof(kCousineTtf)), kFontBody, &config);
}

void ApplyImGuiStyle(float dpi_scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);  // fills the few colors not set below

    style.FontSizeBase = kFontBody;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    // Machined: square corners, hard edges.
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 1.0f;
    style.GrabRounding = 1.0f;
    style.TabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(10.0f, 4.0f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = kText;
    c[ImGuiCol_TextDisabled]          = kTextDim;
    c[ImGuiCol_WindowBg]              = kPanel;
    c[ImGuiCol_ChildBg]               = Rgb(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = kInset;
    c[ImGuiCol_Border]                = kBevelDark;
    c[ImGuiCol_BorderShadow]          = Rgb(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = kInset;
    c[ImGuiCol_FrameBgHovered]        = kFrame;
    c[ImGuiCol_FrameBgActive]         = kFrameHover;
    c[ImGuiCol_TitleBg]               = kFrame;
    c[ImGuiCol_TitleBgActive]         = kFrameHover;
    c[ImGuiCol_TitleBgCollapsed]      = kFrame;
    c[ImGuiCol_MenuBarBg]             = kFrame;
    c[ImGuiCol_ScrollbarBg]           = Rgb(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = kFrameHover;
    c[ImGuiCol_ScrollbarGrabHovered]  = kFrameActive;
    c[ImGuiCol_ScrollbarGrabActive]   = kAccentActive;
    c[ImGuiCol_CheckMark]             = kAccent;
    c[ImGuiCol_CheckboxSelectedBg]    = kInset;
    c[ImGuiCol_SliderGrab]            = kSteel;
    c[ImGuiCol_SliderGrabActive]      = kAccent;
    c[ImGuiCol_Button]                = kFrame;
    c[ImGuiCol_ButtonHovered]         = kFrameHover;
    c[ImGuiCol_ButtonActive]          = kFrameActive;
    c[ImGuiCol_Header]                = kFrame;
    c[ImGuiCol_HeaderHovered]         = kFrameHover;
    c[ImGuiCol_HeaderActive]          = kFrameActive;
    c[ImGuiCol_Separator]             = kFrameHover;
    c[ImGuiCol_SeparatorHovered]      = kAccentHover;
    c[ImGuiCol_SeparatorActive]       = kAccent;
    c[ImGuiCol_ResizeGrip]            = Rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = kFrameActive;
    c[ImGuiCol_ResizeGripActive]      = kAccent;
    c[ImGuiCol_InputTextCursor]       = kAccent;
    c[ImGuiCol_Tab]                   = kFrame;
    c[ImGuiCol_TabHovered]            = kFrameHover;
    c[ImGuiCol_TabSelected]           = kPanel;
    c[ImGuiCol_TabSelectedOverline]   = kAccent;
    c[ImGuiCol_TabDimmed]             = kFrame;
    c[ImGuiCol_TabDimmedSelected]     = kPanel;
    c[ImGuiCol_TabDimmedSelectedOverline] = kFrameActive;
    c[ImGuiCol_DockingPreview]        = Rgb(242, 163, 43, 90);
    c[ImGuiCol_DockingEmptyBg]        = kBackdrop;
    c[ImGuiCol_PlotLines]             = kSteelHover;
    c[ImGuiCol_PlotLinesHovered]      = kAccent;
    c[ImGuiCol_PlotHistogram]         = kSteel;
    c[ImGuiCol_PlotHistogramHovered]  = kAccent;
    c[ImGuiCol_TableHeaderBg]         = kFrame;
    c[ImGuiCol_TableBorderStrong]     = kBevelDark;
    c[ImGuiCol_TableBorderLight]      = kFrameHover;
    c[ImGuiCol_TableRowBg]            = Rgb(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = Rgb(255, 255, 255, 8);
    c[ImGuiCol_TextLink]              = kSteelHover;
    c[ImGuiCol_TextSelectedBg]        = Rgb(242, 163, 43, 70);
    c[ImGuiCol_TreeLines]             = kFrameHover;
    c[ImGuiCol_DragDropTarget]        = kAccent;
    c[ImGuiCol_NavCursor]             = kAccent;
    c[ImGuiCol_NavWindowingHighlight] = kAccent;
    c[ImGuiCol_NavWindowingDimBg]     = Rgb(0, 0, 0, 90);
    c[ImGuiCol_ModalWindowDimBg]      = Rgb(0, 0, 0, 140);

    // Sizes above are in unscaled pixels; scale them (and the fonts) once.
    style.ScaleAllSizes(dpi_scale);
    style.FontScaleDpi = dpi_scale;
}

void ApplyImPlotStyle() {
    ImPlotStyle& style = ImPlot::GetStyle();
    ImPlot::StyleColorsDark(&style);
    style.PlotPadding = ImVec2(8.0f, 8.0f);
    style.LegendPadding = ImVec2(8.0f, 6.0f);
    style.MinorAlpha = 0.4f;

    ImVec4* c = style.Colors;
    c[ImPlotCol_FrameBg]      = Rgb(0, 0, 0, 0);
    c[ImPlotCol_PlotBg]       = kInset;
    c[ImPlotCol_PlotBorder]   = kBevelDark;
    c[ImPlotCol_LegendBg]     = Rgb(22, 25, 29, 230);
    c[ImPlotCol_LegendBorder] = kFrameHover;
    c[ImPlotCol_LegendText]   = kText;
    c[ImPlotCol_TitleText]    = kText;
    c[ImPlotCol_InlayText]    = kText;
    c[ImPlotCol_AxisText]     = kTextDim;
    c[ImPlotCol_AxisGrid]     = Rgb(54, 60, 69, 200);
    c[ImPlotCol_AxisTick]     = kFrameHover;
    c[ImPlotCol_Selection]    = kAccent;
    c[ImPlotCol_Crosshairs]   = kTextDim;

    // Series without a semantic color cycle through the palette's accents.
    static const ImVec4 kIndustrialColormap[] = {kAccent, kSteelHover, kGood, kWarn, kTeal, kPlum, kTextDim};
    style.Colormap = ImPlot::AddColormap("Industrial", kIndustrialColormap,
                                         static_cast<int>(sizeof(kIndustrialColormap) / sizeof(kIndustrialColormap[0])));
}

// `text` in capitals, for the stencilled headings.
std::string Upper(const char* text) {
    std::string s(text);
    for (char& ch : s)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// The heading's look, over the item just laid out at `min`..`max` (`width`
// across): the amber block, the capitals, and the ticked rule; `chevron` 0
// for none, else +1 open / -1 closed.
void DrawHeading(ImVec2 min, float width, float text_h, const std::string& text, int chevron, bool hovered) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float block_w = std::max(3.0f, text_h * 0.22f);
    draw->AddRectFilled(ImVec2(min.x, min.y + 1.0f), ImVec2(min.x + block_w, min.y + text_h - 1.0f), U32(kAccent));
    float x = min.x + block_w + text_h * 0.4f;
    if (chevron != 0) {
        const float s = text_h * 0.22f;
        const ImVec2 c(x + s, min.y + text_h * 0.5f);
        const ImU32 color = U32(hovered ? kAccent : kTextDim);
        if (chevron > 0)
            draw->AddTriangleFilled(ImVec2(c.x - s, c.y - s * 0.6f), ImVec2(c.x + s, c.y - s * 0.6f),
                                    ImVec2(c.x, c.y + s * 0.8f), color);
        else
            draw->AddTriangleFilled(ImVec2(c.x - s * 0.6f, c.y - s), ImVec2(c.x + s * 0.8f, c.y),
                                    ImVec2(c.x - s * 0.6f, c.y + s), color);
        x += s * 2.0f + text_h * 0.3f;
    }
    PushMono(kFontTitle * 0.8f);
    const float th = ImGui::GetTextLineHeight();
    draw->AddText(ImVec2(x, min.y + (text_h - th) * 0.5f), U32(hovered ? kAccentHover : kText), text.c_str());
    PopMono();
    // The rule, with a tick every so often.
    const float y = min.y + text_h + 3.0f;
    const ImU32 rule = U32(kFrameHover);
    draw->AddLine(ImVec2(min.x, y), ImVec2(min.x + width, y), rule, 1.0f);
    const float step = text_h * 0.6f;
    for (float t = min.x + width; t > min.x + width * 0.55f; t -= step)
        draw->AddLine(ImVec2(t, y), ImVec2(t, y + 3.0f), rule, 1.0f);
    draw->AddLine(ImVec2(min.x, y), ImVec2(min.x + std::min(text_h * 2.5f, width), y), U32(kAccent), 2.0f);
}

}  // namespace

void ApplyTheme(float dpi_scale) {
    LoadFonts();
    ApplyImGuiStyle(dpi_scale);
    ApplyImPlotStyle();
}

void PushMono(float size) {
    ImGui::PushFont(g_mono, size);
}

void PopMono() {
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// Styled widgets
// ---------------------------------------------------------------------------

bool PrimaryButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccentText);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool DangerButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, kBad);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBadHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kBadActive);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccentText);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float h = std::max(3.0f, (max.y - min.y) * 0.18f);
        DrawHazardStripes(ImGui::GetWindowDrawList(), ImVec2(min.x, max.y - h), max, kAccentText, 0.6f);
    }
    return pressed;
}

bool DangerSmallButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, Rgb(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb(224, 69, 44, 50));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(224, 69, 44, 90));
    ImGui::PushStyleColor(ImGuiCol_Border, Rgb(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, kBad);
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleColor(5);
    return pressed;
}

bool SegmentedButton(const char* label, bool selected, ImVec2 size) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, kSteel);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSteelHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kSteel);
        ImGui::PushStyleColor(ImGuiCol_Text, kTileGlyph);
    }
    const bool pressed = ImGui::Button(label, size);
    if (selected) {
        ImGui::PopStyleColor(4);
        // An amber strip along the bottom marks the one picked.
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, max.y - 2.0f), max, U32(kAccent));
    }
    return pressed;
}

void KeyCap(const char* key) {
    PushMono(kFontSmall);
    const ImVec2 ts = ImGui::CalcTextSize(key);
    PopMono();
    const float h = ImGui::GetTextLineHeight();
    const float pad_x = h * 0.3f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + std::max(ts.x + 2.0f * pad_x, h), min.y + h);
    ImGui::Dummy(ImVec2(max.x - min.x, h));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, U32(kFrame));
    draw->AddLine(min, ImVec2(max.x, min.y), U32(kBevelLight));
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), U32(kAccent), 1.0f);
    PushMono(kFontSmall);
    draw->AddText(ImVec2((min.x + max.x - ts.x) * 0.5f, min.y + (h - ts.y) * 0.5f), U32(kAccent), key);
    PopMono();
}

bool PencilButton(const char* str_id) {
    const float s = ImGui::GetTextLineHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(str_id, ImVec2(s, s));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered)
        draw->AddRectFilled(min, ImVec2(min.x + s, min.y + s), U32(ImGui::IsItemActive() ? kFrameActive : kFrameHover));

    // A pencil leaning 45 degrees: the tip at the bottom left, the body
    // running up to the top right, ending in a short eraser band.
    const ImU32 color = U32(hovered ? kAccent : kTextDim);
    auto at = [&](float x, float y) { return ImVec2(min.x + s * x, min.y + s * y); };
    const float w = 0.11f;  // half the body's width, along the perpendicular
    draw->AddQuadFilled(at(0.38f - w, 0.62f - w), at(0.66f - w, 0.34f - w), at(0.66f + w, 0.34f + w),
                        at(0.38f + w, 0.62f + w), color);
    draw->AddQuadFilled(at(0.71f - w, 0.29f - w), at(0.78f - w, 0.22f - w), at(0.78f + w, 0.22f + w),
                        at(0.71f + w, 0.29f + w), color);
    draw->AddTriangleFilled(at(0.38f - w, 0.62f - w), at(0.38f + w, 0.62f + w), at(0.2f, 0.8f), color);
    return pressed;
}

bool CloseButton(const char* str_id) {
    const float s = ImGui::GetTextLineHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(str_id, ImVec2(s, s));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (hovered)
        draw->AddRectFilled(min, ImVec2(min.x + s, min.y + s), U32(ImGui::IsItemActive() ? kFrameActive : kFrameHover));
    const ImU32 color = U32(hovered ? kAccent : kTextDim);
    const float a = s * 0.28f;
    const float b = s * 0.72f;
    draw->AddLine(ImVec2(min.x + a, min.y + a), ImVec2(min.x + b, min.y + b), color, 2.0f);
    draw->AddLine(ImVec2(min.x + b, min.y + a), ImVec2(min.x + a, min.y + b), color, 2.0f);
    return pressed;
}

void HelpMarker(const char* text) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void SectionHeading(const char* text) {
    ImGui::Spacing();
    const float width = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, h));
    DrawHeading(min, width, h, Upper(text), 0, false);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

bool CollapsingSection(const char* text, bool default_open) {
    ImGui::Spacing();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID(text);
    bool open = storage->GetBool(id, default_open);
    const float width = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::PushID(text);
    if (ImGui::InvisibleButton("##section", ImVec2(width, h))) {
        open = !open;
        storage->SetBool(id, open);
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    DrawHeading(min, width, h, Upper(text), open ? 1 : -1, hovered);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    return open;
}

void DrawPlatePanel(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    // A short hard shadow under the plate.
    draw->AddRectFilled(ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x + 3.0f, max.y + 3.0f), U32(kShadow, 0.35f));
    draw->AddRectFilled(min, max, U32(kPanel));
    // Bevels: lit top and left, shaded bottom and right.
    draw->AddLine(ImVec2(min.x, min.y), ImVec2(max.x - 1.0f, min.y), U32(kBevelLight));
    draw->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y - 1.0f), U32(kBevelLight));
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), U32(kBevelDark), 2.0f);
    draw->AddLine(ImVec2(max.x - 1.0f, min.y), ImVec2(max.x - 1.0f, max.y), U32(kBevelDark), 2.0f);
    // A rivet in each corner.
    const float inset = 6.0f;
    const float r = 1.8f;
    for (const ImVec2 c : {ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, min.y + inset),
                           ImVec2(min.x + inset, max.y - inset), ImVec2(max.x - inset, max.y - inset)}) {
        draw->AddCircleFilled(ImVec2(c.x + 0.6f, c.y + 0.6f), r, U32(kBevelDark));
        draw->AddCircleFilled(c, r, U32(kFrameActive));
    }
}

void DrawHazardStripes(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec4 color, float alpha) {
    const float h = max.y - min.y;
    const float w = std::max(4.0f, h * 0.9f);  // one stripe's width along x
    const ImU32 fill = U32(color, alpha);
    draw->PushClipRect(min, max, true);
    for (float x = min.x - h; x < max.x; x += w * 2.0f)
        draw->AddQuadFilled(ImVec2(x, max.y), ImVec2(x + h, min.y), ImVec2(x + h + w, min.y), ImVec2(x + w, max.y),
                            fill);
    draw->PopClipRect();
}

void DrawHaloText(ImDrawList* draw, ImVec2 pos, ImU32 color, ImU32 halo, const char* text) {
    constexpr float kOffsets[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    for (const auto& o : kOffsets)
        draw->AddText(ImVec2(pos.x + o[0], pos.y + o[1]), halo, text);
    draw->AddText(pos, color, text);
}

}  // namespace ui::theme
