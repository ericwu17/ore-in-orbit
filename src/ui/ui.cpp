// ui.cpp — the game screen (DrawPlaying): the build palette, the grid or
// the star map, and the craft panel side by side, the fleet's tabs over
// them and the hint bar under them, banners and toasts over the grid, the
// keys window, the pause menu and the fleet-lost screen. Its parts are in
// the other ui_*.cpp files (see ui_internal.h).
#include "ui/ui.h"
#include "ui/ui_internal.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

// The banner over the grid when a milestone is completed: the fleet
// progress it follows, the milestone it last saw being worked on, and until
// when (ImGui::GetTime()) it shows.
struct MilestoneBanner {
    const Progress* progress = nullptr;
    std::size_t seen = 0;
    double until = 0.0;
};
MilestoneBanner g_banner;

// A banner plate centered across the grid (`grid_min`, `grid_size`): a band
// along its top in `band` (hazard stripes when `hazard`), `title` in mono
// in `color`, and `subtitle` under it. At the top of the grid, or with
// `bottom`, at its bottom. Returns its height.
float DrawBanner(const char* title, const char* subtitle, ImVec4 color, ImVec4 band, bool hazard, ImVec2 grid_min,
                 ImVec2 grid_size, bool bottom) {
    const float fh = ImGui::GetFrameHeight();
    const float pad = fh * 0.45f;
    const float band_h = std::max(4.0f, fh * 0.28f);
    theme::PushMono(theme::kFontTitle);
    const ImVec2 ts = ImGui::CalcTextSize(title);
    theme::PopMono();
    const ImVec2 ss = ImGui::CalcTextSize(subtitle);
    const float w = std::max(ts.x, ss.x) + pad * 2.0f;
    const float h = band_h + ts.y + ss.y + pad * 2.0f;
    const float y = bottom ? grid_min.y + grid_size.y - h - fh * 0.6f : grid_min.y + fh * 0.6f;
    const ImVec2 min(grid_min.x + std::floor((grid_size.x - w) * 0.5f), y);
    const ImVec2 max(min.x + w, min.y + h);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    theme::DrawPlatePanel(draw, min, max);
    const ImVec2 band_max(max.x - 2.0f, min.y + band_h);
    if (hazard) {
        draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), band_max, theme::U32(theme::kBevelDark));
        theme::DrawHazardStripes(draw, ImVec2(min.x + 1.0f, min.y + 1.0f), band_max, band);
    } else {
        draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), band_max, theme::U32(band));
    }
    theme::PushMono(theme::kFontTitle);
    draw->AddText(ImVec2(min.x + (w - ts.x) * 0.5f, min.y + band_h + pad * 0.8f), theme::U32(color), title);
    theme::PopMono();
    draw->AddText(ImVec2(min.x + (w - ss.x) * 0.5f, max.y - pad - ss.y), theme::U32(theme::kTextDim), subtitle);
    return h;
}

// Announces a milestone just completed, for a few seconds, at the top of
// the grid (`grid_min`, `grid_size`).
void DrawMilestoneBanner(const Progress& progress, ImVec2 grid_min, ImVec2 grid_size) {
    const std::size_t current = progress.current_index();
    MilestoneBanner& banner = g_banner;
    if (banner.progress != &progress)
        banner = {&progress, current, 0.0};  // a new or loaded game: nothing to announce
    if (current > banner.seen)
        banner.until = ImGui::GetTime() + 5.0;
    banner.seen = current;
    if (current == 0 || ImGui::GetTime() >= banner.until)
        return;

    const std::string title = std::string("MILESTONE COMPLETE: ") + all_milestones()[current - 1].name;
    const char* subtitle = progress.all_complete() ? "Every milestone done!" : "New recipes unlocked";
    DrawBanner(title.c_str(), subtitle, theme::kGood, theme::kAccent, false, grid_min, grid_size, false);
}

// The fight, along the bottom of the grid: how many enemies are on it, or
// when the next wave comes. Returns its height (0 when there's none).
float DrawBattleBanner(const FleetShip& ship, const Sector& sector, Tick now, ImVec2 grid_min, ImVec2 grid_size) {
    const Battle& battle = ship.battle;
    std::string title;
    std::string subtitle;
    ImVec4 color = theme::kBad;
    if (battle.under_attack()) {
        title = "UNDER ATTACK: " + std::to_string(battle.enemies.size()) + " enemies";
        subtitle = sector.hostile() ? std::to_string(sector.waves_left) + " more waves to come" : "The last wave";
    } else if (battle.next_wave) {
        const Tick left = *battle.next_wave > now ? *battle.next_wave - now : 0;
        title = "NEXT WAVE IN " + std::to_string((left + kTicksPerSecond - 1) / kTicksPerSecond) + " s";
        subtitle = std::to_string(sector.waves_left) + " waves left in " + sector.name;
        color = theme::kWarnText;
    } else {
        return 0.0f;
    }
    return DrawBanner(title.c_str(), subtitle.c_str(), color, battle.under_attack() ? theme::kBad : theme::kAccent,
                      true, grid_min, grid_size, true);
}

// The last message (SetMessage), for a few seconds after it's set: a toast
// centered over the bottom of the grid, `above` pixels higher (over the
// battle banner), fading out at the end.
void DrawToast(ImVec2 grid_min, ImVec2 grid_size, float above) {
    const BuildUiState& build = g_build;
    const double age = ImGui::GetTime() - build.message_time;
    if (build.message.empty() || age >= kMessageSeconds)
        return;
    const float alpha = static_cast<float>(std::min(1.0, (kMessageSeconds - age) / 0.5));
    const float fh = ImGui::GetFrameHeight();
    const float pad = fh * 0.4f;
    const float wrap = std::min(grid_size.x * 0.8f, fh * 24.0f);
    const ImVec2 ts = ImGui::CalcTextSize(build.message.c_str(), nullptr, false, wrap);
    const float stripe = std::max(3.0f, fh * 0.15f);
    const float w = ts.x + pad * 2.0f + stripe;
    const float h = ts.y + pad * 2.0f;
    const ImVec2 min(grid_min.x + std::floor((grid_size.x - w) * 0.5f),
                     grid_min.y + grid_size.y - h - fh * 0.6f - (above > 0.0f ? above + fh * 0.4f : 0.0f));
    const ImVec2 max(min.x + w, min.y + h);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x + 3.0f, max.y + 3.0f),
                        theme::U32(theme::kShadow, 0.35f * alpha));
    draw->AddRectFilled(min, max, theme::U32(theme::kPanel, alpha));
    draw->AddRect(min, max, theme::U32(theme::kBevelLight, alpha));
    draw->AddRectFilled(min, ImVec2(min.x + stripe, max.y), theme::U32(build.message_color, alpha));
    draw->AddText(nullptr, 0.0f, ImVec2(min.x + stripe + pad, min.y + pad), theme::U32(build.message_color, alpha),
                  build.message.c_str(), nullptr, wrap);
}

// Back to the main menu from a game: the game screen's state is the next
// game's to start over with.
void LeaveGame(Screen& screen) {
    screen = Screen::MainMenu;
    g_camera = {};  // the next game starts centred on its bridge
    g_fleet = {};
    g_map = {};
    g_banner = {};
    g_build.inspected.reset();
    g_build.inspected_building = nullptr;
    LeaveBlueprintTool();  // the clipboard stays, to paste into another game
    g_blueprints.show_library = false;
    g_build.show_keys = false;
    g_settings = {};
}

// One hint in the bar under the grid: a key cap, then what it does.
void Hint(const char* key, const char* does) {
    theme::KeyCap(key);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextColored(theme::kTextDim, "%s", does);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
}

// The bar under the grid (`min`, `width` across): the keys that do
// something now, for the tool in hand, and the "Keys" button that opens
// the overlay of all of them.
void DrawHintBar(ImVec2 min, float width) {
    const float fh = ImGui::GetFrameHeight();
    const BlueprintUiState& bp = g_blueprints;
    ImGui::SetCursorScreenPos(min);
    ImGui::BeginChild("##hints", ImVec2(width, fh), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const char* keys_label = "? Keys";
    const float keys_w = ImGui::CalcTextSize(keys_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::PushClipRect(min, ImVec2(min.x + width - keys_w - ImGui::GetStyle().ItemSpacing.x, min.y + fh), true);
    ImGui::SetCursorPosY((fh - ImGui::GetTextLineHeight()) * 0.5f);
    if (g_map.shown) {
        Hint("LMB", "sector details");
        Hint("Drag", "pan");
        Hint("Wheel", "zoom");
        Hint("M", "back to the ship");
    } else if (bp.tool == Tool::Copy) {
        Hint("Drag", "box over what to copy");
        Hint("RMB", "stop");
        Hint("Esc", "stop");
    } else if (bp.tool == Tool::Paste) {
        Hint("LMB", "paste");
        Hint("R", "turn");
        Hint("RMB", "stop");
        if (!bp.paste_status.empty())
            ImGui::TextColored(theme::kBad, "%s", bp.paste_status.c_str());
    } else if (ImGui::GetIO().KeyShift) {
        Hint("Shift+RMB", "copy a building's settings");
        Hint("Shift+LMB", "paste them (drag for several)");
        if (g_settings.copied)
            if (const char* name = SettingsName(*g_settings.copied))
                ImGui::TextColored(theme::kAccent, "Holding: %s", name);
    } else if (g_build.selected) {
        Hint("LMB", "build");
        Hint("RMB", "remove");
        if (is_rotatable(*g_build.selected)) {
            std::string facing = std::string("turn (now ") + DirectionName(g_build.facing) + ")";
            Hint("R / Shift+R", facing.c_str());
        }
        Hint("Q", "pick / build nothing");
        Hint("Shift", "settings");
    } else {
        Hint("LMB", "inspect");
        Hint("RMB", "remove");
        Hint("Q", "pick the building under the mouse");
        Hint("1-9", "build");
        Hint("Shift", "settings");
    }
    ImGui::PopClipRect();
    ImGui::SetCursorPos(ImVec2(width - keys_w, 0.0f));
    if (theme::SegmentedButton(keys_label, g_build.show_keys, ImVec2(keys_w, fh)))
        g_build.show_keys = !g_build.show_keys;
    ImGui::SetItemTooltip("Every key and mouse control (?)");
    ImGui::EndChild();
}

// The keys overlay (? or the Keys button): every control, in a table.
void DrawKeysWindow() {
    if (!g_build.show_keys)
        return;
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 22.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Keys", &g_build.show_keys, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const std::string copy = std::string(kCtrlName) + "+C";
    const std::string paste = std::string(kCtrlName) + "+V";
    const struct {
        const char* key;
        const char* does;
    } rows[] = {
        {"LMB / drag", "Build the selected item; belts face the way you drag"},
        {"LMB a building", "Inspect it: a machine's recipe, a collector's target, a sorter's filter"},
        {"RMB / drag", "Remove buildings (refunded)"},
        {"Shift+RMB", "Copy a building's settings"},
        {"Shift+LMB / drag", "Paste them onto buildings that can take them"},
        {"Wheel", "Zoom"},
        {"MMB drag, Space", "Pan"},
        {"1-9", "Pick the item to build (again: build nothing)"},
        {"RMB a palette icon", "Hand-craft one (Shift: 5)"},
        {"R / Shift+R", "Turn what you're building, or the blueprint"},
        {"Q", "Pick the building under the mouse, or build nothing"},
        {copy.c_str(), "Copy an area (drag a box)"},
        {paste.c_str(), "Pick the last copy up to paste"},
        {"B", "Blueprint library"},
        {"M", "Star map"},
        {"[ / ]", "Previous / next ship"},
        {"?", "This list"},
        {"Esc", "Put the tool away, close what's open, then pause"},
    };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            theme::KeyCap(row.key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.does);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(fh * 20.0f);
    ImGui::TextColored(theme::kTextDim,
                       "New machines, collectors and sorters start with the settings last picked for their kind. "
                       "Buildings go on the ship's hull, which its layout fixes, with its bridge and engines.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}

// The end of a game whose fleet is lost: the news, and the way out.
void DrawFleetLost(Screen& screen) {
    BeginFullscreenPage("##fleet_lost");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetCursorPosY(viewport->WorkSize.y * 0.35f);
    // A band of red hazard stripes right across the screen, behind the news.
    const ImVec2 band_min(viewport->WorkPos.x, ImGui::GetCursorScreenPos().y - fh * 0.5f);
    theme::DrawHazardStripes(ImGui::GetWindowDrawList(), band_min,
                             ImVec2(band_min.x + viewport->WorkSize.x, band_min.y + fh * 0.4f), theme::kBad, 0.8f);
    ImGui::Dummy(ImVec2(0.0f, fh * 0.2f));
    theme::PushMono(theme::kFontHuge);
    CenteredText("FLEET LOST", theme::kBad);
    theme::PopMono();
    CenteredText("Every ship's bridge has been destroyed.", theme::kText);
    CenteredText("Load the last save (from a minute ago, at most) to try again.", theme::kTextDim);
    ImGui::Dummy(ImVec2(0.0f, fh));
    const ImVec2 button(fh * 8.0f, fh * 1.6f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button.x) * 0.5f);
    if (theme::PrimaryButton("Main menu", button) || ShortcutPressed(ImGuiKey_Escape))
        LeaveGame(screen);
    ImGui::End();
}

}  // namespace

}  // namespace ui

// ui.h's screens are global; the helpers they use are in namespace ui.
using namespace ui;

void DrawPlaying(Screen& screen, World& world, bool& paused, const std::filesystem::path& blueprints_dir) {
    if (world.fleet_lost()) {
        paused = false;
        DrawFleetLost(screen);
        return;
    }
    // Escape opens the pause menu, or closes it again; first, though, it puts
    // the copy / paste tool away, then closes what's open, one at a time:
    // the Send cargo window, the Build a new ship window, the inspector, the
    // keys overlay, the star map, the Blueprints window. Not when it closed a dropdown or tooltip popup:
    // ImGui already did that before we got here, so look at whether one was
    // open last frame.
    static bool popup_was_open = false;
    BlueprintUiState& bp = g_blueprints;
    if (ShortcutPressed(ImGuiKey_Escape) && (paused || !popup_was_open)) {
        if (paused)
            paused = false;
        else if (bp.tool != Tool::Build)
            LeaveBlueprintTool();
        else if (g_fleet.show_send)
            g_fleet.show_send = false;
        else if (g_fleet.show_build)
            g_fleet.show_build = false;
        else if (g_build.inspected) {
            g_build.inspected.reset();
            g_build.inspected_building = nullptr;
        } else if (g_build.show_keys)
            g_build.show_keys = false;
        else if (g_map.shown)
            g_map.shown = false;
        else if (bp.show_library)
            bp.show_library = false;
        else
            paused = true;
    }

    FleetShip& current = CurrentShip(world.get_fleet());
    Ship& ship = current.ship;
    if (!paused) {
        if (ShortcutPressed(ImGuiKey_LeftBracket))
            CycleShip(world.get_fleet(), -1);
        if (ShortcutPressed(ImGuiKey_RightBracket))
            CycleShip(world.get_fleet(), 1);
        if (ShortcutPressed(ImGuiKey_M))
            g_map.shown = !g_map.shown;
        const std::vector<ItemKind> list = BuildList(ship);
        for (std::size_t i = 0; i < std::min<std::size_t>(list.size(), 9); ++i)
            if (ShortcutPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i))))
                ToggleSelected(list[i]);
        if (ShortcutPressed(ImGuiKey_Q)) {
            // Over a building: pick it, as it faces and with its settings for
            // new ones. Anywhere else: build nothing.
            LeaveBlueprintTool();
            const Building* b =
                g_build.hover_cell && !g_map.shown ? ship.building_at(*g_build.hover_cell) : nullptr;
            const std::optional<ItemKind> kind = b ? b->item_kind() : std::nullopt;
            if (kind && is_placeable(*kind)) {
                g_build.selected = kind;
                if (is_rotatable(*kind))
                    g_build.facing = b->facing();
                if (const std::optional<BuildingSettings> settings = SettingsOf(*b))
                    RememberSettings(*settings);
            } else {
                g_build.selected.reset();
            }
        }
        if (ShortcutPressed(ImGuiKey_Slash))
            g_build.show_keys = !g_build.show_keys;
        if (ShortcutPressed(ImGuiKey_R)) {
            // Shift turns it back the other way: three quarter turns, for a
            // blueprint.
            const bool back = ImGui::GetIO().KeyShift;
            if (bp.tool != Tool::Paste || !bp.held) {
                g_build.facing = back ? rotated_ccw(g_build.facing) : rotated_cw(g_build.facing);
            } else {
                for (int turns = back ? 3 : 1; turns > 0; --turns)
                    bp.held = rotated_cw(*bp.held);
            }
        }
        if (ChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            LeaveBlueprintTool();
            bp.tool = Tool::Copy;
        }
        if (ChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            if (bp.clipboard)
                HoldBlueprint(*bp.clipboard);
            else
                SetMessage(std::string("Nothing copied yet: ") + kCtrlName + "+C copies an area");
        }
        if (ShortcutPressed(ImGuiKey_B))
            bp.show_library = !bp.show_library;
    }

    // The mouse wheel zooms the grid; it never scrolls the page.
    BeginFullscreenPage("##playing", ImGuiWindowFlags_NoScrollWithMouse);
    const float fh = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().WindowPadding.x * 2.0f;
    const float palette_w = fh * 11.0f;
    const float craft_w = fh * 12.0f;
    DrawBuildPalette(ship, palette_w);
    ImGui::SameLine(0.0f, gap);
    const ImVec2 column_min = ImGui::GetCursorScreenPos();
    ImVec2 column_avail = ImGui::GetContentRegionAvail();
    column_avail.x = std::max(0.0f, column_avail.x - craft_w - gap);
    // The middle column: the fleet's tabs, the grid under them, and the
    // hint bar under that.
    DrawFleetStrip(world);
    const float strip_h = fh + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 grid_min(column_min.x, column_min.y + strip_h);
    const ImVec2 grid_avail(column_avail.x, std::max(0.0f, column_avail.y - strip_h * 2.0f));
    ImGui::SetCursorScreenPos(grid_min);
    // The strip may have switched ships, or renamed this one.
    FleetShip& shown = CurrentShip(world.get_fleet());
    if (g_map.shown) {
        g_build.hover_cell.reset();
        DrawStarMap(world, shown, grid_avail);
    } else {
        DrawShipGrid(shown.ship, shown.battle, world.get_tick(), grid_avail);
    }
    DrawMilestoneBanner(world.get_fleet().get_progress(), grid_min, grid_avail);
    const float battle_h =
        g_map.shown ? 0.0f
                    : DrawBattleBanner(shown, world.get_map().at(shown.sector), world.get_tick(), grid_min, grid_avail);
    DrawToast(grid_min, grid_avail, battle_h);
    DrawHintBar(ImVec2(grid_min.x, grid_min.y + grid_avail.y + ImGui::GetStyle().ItemSpacing.y), grid_avail.x);
    ImGui::SetCursorScreenPos(ImVec2(column_min.x + column_avail.x + gap, column_min.y));
    DrawCraftPanel(world, shown, craft_w);
    if (!paused) {
        DrawBlueprintLibrary(blueprints_dir);
        DrawSendCargoWindow(world, shown);
        DrawBuildShipWindow(world, shown);
        DrawKeysWindow();
    }
    popup_was_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

    // The pause menu: a modal over the game, which dims it and takes the
    // mouse while it is open.
    constexpr const char* kPausePopup = "Paused";
    if (paused && !ImGui::IsPopupOpen(kPausePopup))
        ImGui::OpenPopup(kPausePopup);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kPausePopup, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        const ImVec2 button_size = {fh * 8.0f, fh * 1.6f};
        if (theme::PrimaryButton("Resume", button_size))
            paused = false;
        if (ImGui::Button("Main menu", button_size)) {
            paused = false;
            LeaveGame(screen);
        }
        if (!paused)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}
