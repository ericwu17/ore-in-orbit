// ui_common.cpp — the game screen's UI-only state, and the small widgets
// every part of the UI uses (see ui_internal.h).
#include "ui/ui_internal.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <utility>

namespace ui {

void BeginFullscreenPage(const char* id, ImGuiWindowFlags extra) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | extra);
}

void CenteredText(const char* text, ImVec4 color) {
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::TextColored(color, "%s", text);
}

bool ShortcutPressed(ImGuiKey key) {
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(key, false);
}

bool ChordPressed(ImGuiKeyChord chord) {
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(chord);
}

BuildUiState g_build;

SettingsUiState g_settings;

const Building* InspectedBuilding(const Ship& ship) {
    if (g_build.inspected && ship.building_anchored_at(*g_build.inspected) == g_build.inspected_building)
        return g_build.inspected_building;
    g_build.inspected.reset();
    g_build.inspected_building = nullptr;
    return nullptr;
}

Camera g_camera;

BlueprintUiState g_blueprints;

void LeaveBlueprintTool() {
    g_blueprints.tool = Tool::Build;
    g_blueprints.copy_start.reset();
    g_blueprints.held.reset();
    g_blueprints.paste_status.clear();
}

FleetUiState g_fleet;

MapUiState g_map;

void SwitchToShip(int id) {
    FleetUiState& f = g_fleet;
    if (id == f.current)
        return;
    f.cameras[f.current] = g_camera;
    g_camera = f.cameras[id];  // a ship not seen yet: centred on its bridge
    f.cameras.erase(id);
    f.current = id;
    f.renaming.reset();
    g_build.inspected.reset();
    g_build.inspected_building = nullptr;
}

FleetShip& CurrentShip(Fleet& fleet) {
    if (FleetShip* s = fleet.find(g_fleet.current))
        return *s;
    g_fleet.current = fleet.flagship().id;
    return fleet.flagship();
}

void HoldBlueprint(const Blueprint& blueprint) {
    LeaveBlueprintTool();
    g_blueprints.tool = Tool::Paste;
    g_blueprints.held = blueprint;
}

void ToggleSelected(ItemKind kind) {
    LeaveBlueprintTool();
    if (g_build.selected == kind)
        g_build.selected.reset();
    else
        g_build.selected = kind;
}

void SetMessage(std::string message, ImVec4 color) {
    g_build.message = std::move(message);
    g_build.message_color = color;
    g_build.message_time = ImGui::GetTime();
}

void SetBuildMessage(BuildResult result) {
    SetMessage(describe(result));
}

std::optional<BuildingSettings> SettingsOf(const Building& b) {
    const std::optional<ItemKind> kind = b.item_kind();
    if (!kind)
        return std::nullopt;
    BuildingSettings s;
    s.kind = *kind;
    if (const auto* machine = dynamic_cast<const Machine*>(&b))
        s.recipe = machine->get_recipe();
    else if (const auto* collector = dynamic_cast<const Collector*>(&b))
        s.target = collector->get_target();
    else if (const auto* sorter = dynamic_cast<const Sorter*>(&b)) {
        s.filter = sorter->get_filter();
        s.has_filter = true;
    } else
        return std::nullopt;
    return s;
}

void RememberSettings(const BuildingSettings& s) {
    g_settings.last[static_cast<int>(s.kind)] = s;
}

bool ApplySettings(Ship& ship, GridPos p, const BuildingSettings& s) {
    const Building* b = ship.building_at(p);
    const std::optional<BuildingSettings> now = b ? SettingsOf(*b) : std::nullopt;
    if (!now)
        return false;
    if (dynamic_cast<const Machine*>(b))
        return s.recipe && s.recipe != now->recipe && ship.set_recipe(p, s.recipe);
    if (dynamic_cast<const Collector*>(b))
        return s.target && s.target != now->target && ship.set_collector_target(p, *s.target);
    return s.has_filter && s.filter != now->filter && ship.set_sort_filter(p, s.filter);
}

bool SameSettings(const BuildingSettings& a, const BuildingSettings& b) {
    return a.recipe == b.recipe && a.target == b.target && a.filter == b.filter;
}

const char* SettingsName(const BuildingSettings& s) {
    if (s.recipe)
        return recipe_name(*s.recipe);
    if (s.target)
        return item_name(*s.target);
    if (s.filter)
        return item_name(*s.filter);
    return nullptr;
}

std::string BlueprintSummary(const Blueprint& blueprint) {
    const std::size_t n = blueprint.buildings.size();
    return std::to_string(blueprint.size.w) + "x" + std::to_string(blueprint.size.h) + ", " + std::to_string(n) +
           (n == 1 ? " building" : " buildings");
}

Inventory BlueprintCost(const Blueprint& blueprint) {
    Inventory cost;
    for (const BlueprintBuilding& b : blueprint.buildings)
        cost.add(b.kind);
    return cost;
}

namespace {

// A square tile `size` across at `min`: `bg` with bevelled edges (an amber
// border when `selected`), the icon over it (faded by `alpha`) and its
// badges, in mono. Disabled widgets fade all of it.
void DrawIconFrame(ImDrawList* draw, const Icon& icon, ImVec2 min, float size, ImVec4 bg, Badges badges,
                   float alpha, bool selected = false) {
    const float style_alpha = ImGui::GetStyle().Alpha;
    const ImVec2 max(min.x + size, min.y + size);
    draw->AddRectFilled(min, max, theme::U32(bg, style_alpha));
    draw->AddLine(min, ImVec2(max.x - 1.0f, min.y), theme::U32(theme::kBevelLight, style_alpha));
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), theme::U32(theme::kBevelDark, style_alpha));
    if (selected)
        draw->AddRect(min, max, theme::U32(theme::kAccent, style_alpha), 0.0f, 0, 2.0f);
    const float pad = size * 0.08f;
    DrawIcon(draw, icon, ImVec2(min.x + pad, min.y + pad), ImVec2(max.x - pad, max.y - pad), style_alpha * alpha);
    if (!badges.top_left && !badges.bottom_right)
        return;
    theme::PushMono(theme::kFontSmall);
    const ImU32 ink = theme::U32(theme::kText, style_alpha);
    const ImU32 halo = theme::U32(theme::kBevelDark, style_alpha);
    if (badges.top_left)
        theme::DrawHaloText(draw, ImVec2(min.x + 3.0f, min.y + 1.0f), theme::U32(theme::kAccent, style_alpha), halo,
                            badges.top_left);
    if (badges.bottom_right) {
        const ImVec2 ts = ImGui::CalcTextSize(badges.bottom_right);
        theme::DrawHaloText(draw, ImVec2(max.x - ts.x - 3.0f, max.y - ts.y - 1.0f), ink, halo, badges.bottom_right);
    }
    theme::PopMono();
}

}  // namespace

bool IconButton(const char* id, const Icon& icon, float size, bool selected, Badges badges,
                float fade) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec4 bg = ImGui::IsItemActive() ? theme::kFrameActive
                      : hovered || selected ? theme::kFrameHover
                                            : theme::kFrame;
    DrawIconFrame(ImGui::GetWindowDrawList(), icon, min, size, bg, badges, fade, selected);
    return pressed;
}

void IconTile(const Icon& icon, float size, Badges badges) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    DrawIconFrame(ImGui::GetWindowDrawList(), icon, min, size, theme::kFrame, badges, 1.0f);
}

void IconImage(const Icon& icon, float size) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    DrawIcon(ImGui::GetWindowDrawList(), icon, min, ImVec2(min.x + size, min.y + size), ImGui::GetStyle().Alpha);
    ImGui::SetItemTooltip("%s", IconName(icon));
}

std::string ShortCount(int n) {
    if (n < 10'000)
        return std::to_string(n);
    if (n < 10'000'000)
        return std::to_string(n / 1'000) + "k";
    return std::to_string(n / 1'000'000) + "M";
}

std::string Fraction(int have, int need) {
    return std::to_string(have) + " / " + std::to_string(need);
}

void AmountLine(const Icon& icon, const std::string& text, ImVec4 color, bool named, const char* suffix) {
    ImGui::AlignTextToFramePadding();
    IconImage(icon, ImGui::GetFrameHeight());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextColored(color, "%s%s%s%s", text.c_str(), named ? " " : "", named ? IconName(icon) : "", suffix);
}

std::vector<ItemKind> BuildList(const Ship& ship) {
    const Progress& progress = ship.get_bridge().get_progress();
    std::vector<ItemKind> list;
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (is_placeable(kind) &&
            (progress.unlocked(kind) || ship.inventory().count(kind) > 0 || kind == g_build.selected))
            list.push_back(kind);
    }
    return list;
}

const Recipe* HandRecipeFor(ItemKind kind) {
    for (const Recipe& r : all_recipes())
        if (r.hand_craftable && r.outputs.front().kind == kind)
            return &r;
    return nullptr;
}

std::string Trimmed(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

bool ContainsIgnoringCase(std::string_view text, std::string_view part) {
    const auto same = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return part.empty() || std::search(text.begin(), text.end(), part.begin(), part.end(), same) != text.end();
}

bool BeginIconCombo(const char* id, const Icon& icon, const char* text) {
    // The frame is drawn empty, then the value over it, in this window's
    // draw list (once open, the popup is the current window).
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float w = ImGui::CalcItemWidth();
    const bool open = ImGui::BeginCombo(id, "", ImGuiComboFlags_HeightLargest);
    const float fh = ImGui::GetFrameHeight();
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float s = fh - 4.0f;
    draw->PushClipRect(min, ImVec2(min.x + w - fh, min.y + fh), true);  // short of the arrow
    const ImVec2 icon_min(min.x + 2.0f, min.y + 2.0f);
    DrawIcon(draw, icon, icon_min, ImVec2(icon_min.x + s, icon_min.y + s));
    draw->AddText(ImVec2(icon_min.x + s + ImGui::GetStyle().ItemInnerSpacing.x, min.y + pad.y),
                  ImGui::GetColorU32(ImGuiCol_Text), text);
    draw->PopClipRect();
    return open;
}

namespace {

// Search for a combo's options: a box above them, emptied and focused as the
// popup opens, that hides the options not containing what's typed (ignoring
// case); once something is typed, Enter picks the first one left. The
// options are icon buttons in rows. Only one combo is open at a time, so they
// all share this.
struct ComboSearch {
    char text[64] = {};
    bool enter = false;  // Enter went down in the box this frame
    int shown = 0;       // options shown so far this frame
    IconFlow flow;       // lays the options out
};
ComboSearch g_combo_search;

// Whether the option `label` matches what's typed.
bool ComboSearchMatches(const char* label) {
    return ContainsIgnoringCase(label, g_combo_search.text);
}

// A heading over the options after it, which start a new row.
void ComboSearchHeading(const char* text) {
    ImGui::SeparatorText(text);
    g_combo_search.flow.Start(g_combo_search.flow.size);
}

}  // namespace

void BeginComboSearch() {
    ComboSearch& search = g_combo_search;
    if (ImGui::IsWindowAppearing()) {
        search.text[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    // Enter with nothing typed picks nothing (the first option is "None").
    search.enter = ImGui::InputTextWithHint("##search", "Search", search.text, sizeof(search.text),
                                            ImGuiInputTextFlags_EnterReturnsTrue) &&
                   search.text[0] != '\0';
    search.shown = 0;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                        ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 14.0f));
    ImGui::BeginChild("##options", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);
    search.flow.Start(ImGui::GetFrameHeight() * 1.6f);
}

bool ComboSearchShows(const char* label) {
    if (!ComboSearchMatches(label))
        return false;
    ++g_combo_search.shown;
    g_combo_search.flow.Next();
    return true;
}

bool ComboSearchOption(const Icon& icon, bool selected) {
    const bool picked = IconButton("##option", icon, g_combo_search.flow.size, selected) ||
                        (g_combo_search.enter && g_combo_search.shown == 1);
    // The options' child window isn't the popup, so the button won't close it.
    if (picked)
        ImGui::CloseCurrentPopup();
    return picked;
}

void EndComboSearch() {
    if (g_combo_search.shown == 0)
        ImGui::TextColored(theme::kTextDim, "No matches");
    ImGui::EndChild();
}

std::optional<ItemKind> ComboSearchItemOptions(std::optional<ItemKind> current) {
    std::optional<ItemKind> picked;
    for (const ItemCategory category : {ItemCategory::Placeable, ItemCategory::RawResource,
                                        ItemCategory::Material, ItemCategory::Component}) {
        bool any = false;
        for (int i = 0; i < kItemKindCount && !any; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            any = item_category(kind) == category && !is_layout_part(kind) && ComboSearchMatches(item_name(kind));
        }
        if (!any)
            continue;
        ComboSearchHeading(CategoryName(category));
        for (int i = 0; i < kItemKindCount; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            if (item_category(kind) != category || is_layout_part(kind) || !ComboSearchShows(item_name(kind)))
                continue;
            ImGui::PushID(i);
            if (ComboSearchOption(ItemIcon(kind), current == kind))
                picked = kind;
            ImGui::SetItemTooltip("%s", item_name(kind));
            ImGui::PopID();
        }
    }
    return picked;
}

}  // namespace ui
