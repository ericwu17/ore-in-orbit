// ui_menus.cpp — the screens outside a game: the main menu, the save list
// and the credits (see ui.h).
#include "ui/ui.h"
#include "ui/ui_internal.h"

#include "persistence/save_slots.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <ctime>

namespace ui {

namespace fs = std::filesystem;

namespace {

// The save list screen's state. Reset, and the list read from disk, each
// time the screen opens; read again after each change it makes.
struct SaveListUiState {
    std::vector<SaveSlot> slots;
    int drawn_frame = -1;  // ImGui::GetFrameCount() when last drawn
    // The save whose name is being edited, and the edit.
    std::optional<fs::path> renaming;
    char new_name[64] = {};
    bool focus_name = false;
    // The save waiting for the player to confirm deleting it.
    std::optional<fs::path> deleting;
    std::string deleting_name;
};
SaveListUiState g_saves;

constexpr const char* kDeletePopup = "Delete save?";

// "2026-10-19 14:05", in local time.
std::string FormatSavedAt(std::int64_t saved_at) {
    const auto t = static_cast<std::time_t>(saved_at);
    const std::tm* local = std::localtime(&t);
    char text[32] = "?";
    if (local)
        std::strftime(text, sizeof text, "%Y-%m-%d %H:%M", local);
    return text;
}

// "1:02:03": hours, minutes and seconds of play.
std::string FormatPlayed(Tick ticks) {
    const Tick seconds = ticks / kTicksPerSecond;
    char text[32];
    std::snprintf(text, sizeof text, "%llu:%02llu:%02llu", static_cast<unsigned long long>(seconds / 3600),
                  static_cast<unsigned long long>(seconds / 60 % 60), static_cast<unsigned long long>(seconds % 60));
    return text;
}

// A row of the credits page.
struct Credit {
    const char* name;
    const char* used_for;
    const char* author;
    const char* license;
};

constexpr Credit kCredits[] = {
    {"SDL2", "Window and input", "Sam Lantinga", "zlib"},
    {"Dear ImGui", "The interface", "Omar Cornut", "MIT"},
    {"ImPlot", "Plots", "Evan Pezent", "MIT"},
    {"nlohmann::json", "Saves and blueprints", "Niels Lohmann", "MIT"},
    {"stb_image", "Loading images", "Sean Barrett", "MIT or public domain"},
    {"Roboto", "Interface text", "Christian Robertson", "Apache 2.0"},
    {"Cousine", "Headings and numbers", "Steve Matteson", "SIL OFL 1.1"},
};

}  // namespace

}  // namespace ui

// ui.h's screens are global; the helpers they use are in namespace ui.
using namespace ui;

void DrawMainMenu(Screen& screen) {
    BeginFullscreenPage("##main_menu");

    const float fh = ImGui::GetFrameHeight();  // follows font size / DPI scaling
    const ImVec2 button_size = {fh * 8.0f, fh * 1.6f};
    const float spacing = ImGui::GetStyle().ItemSpacing.y;

    // Title plus two buttons, as one block centered vertically.
    theme::PushMono(theme::kFontHuge * 1.4f);
    const float title_h = ImGui::GetTextLineHeight();
    const float title_w = ImGui::CalcTextSize("ORE IN ORBIT").x;
    theme::PopMono();
    const float block_h = title_h + fh * 1.5f + 2.0f * button_size.y + spacing;
    ImGui::SetCursorPosY((ImGui::GetWindowHeight() - block_h) * 0.5f);

    theme::PushMono(theme::kFontHuge * 1.4f);
    CenteredText("ORE IN ORBIT", theme::kText);
    theme::PopMono();
    // A hazard-striped rule under the title.
    const ImVec2 title_max = ImGui::GetItemRectMax();
    const float rule_w = std::max(title_w, button_size.x);
    const ImVec2 rule_min((ImGui::GetWindowWidth() - rule_w) * 0.5f + ImGui::GetWindowPos().x, title_max.y + fh * 0.3f);
    theme::DrawHazardStripes(ImGui::GetWindowDrawList(), rule_min,
                             ImVec2(rule_min.x + rule_w, rule_min.y + fh * 0.35f), theme::kAccent);
    ImGui::Dummy({0.0f, fh * 1.5f - spacing});

    const float button_x = (ImGui::GetWindowWidth() - button_size.x) * 0.5f;
    ImGui::SetCursorPosX(button_x);
    if (theme::PrimaryButton("Play", button_size))
        screen = Screen::SaveSelect;
    ImGui::SetCursorPosX(button_x);
    if (ImGui::Button("Credits", button_size))
        screen = Screen::Credits;

    ImGui::End();
}

SaveSelectResult DrawSaveSelect(Screen& screen, const fs::path& saves_dir, std::string& message) {
    SaveListUiState& ui = g_saves;
    if (ImGui::GetFrameCount() != ui.drawn_frame + 1) {
        ui = {};
        ui.slots = list_saves(saves_dir);
        message.clear();
    }
    ui.drawn_frame = ImGui::GetFrameCount();

    SaveSelectResult result;
    // Changes to the list wait until it has been drawn.
    std::optional<fs::path> copy_from;
    bool commit_rename = false;

    BeginFullscreenPage("##save_select");
    const float fh = ImGui::GetFrameHeight();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fh * 32.0f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::BeginChild("##saves", ImVec2(width, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    theme::SectionHeading("Saves");
    if (theme::PrimaryButton("New game"))
        result.action = SaveSelectResult::Action::NewGame;
    ImGui::SameLine();
    const bool popup_open = ImGui::IsPopupOpen(kDeletePopup);
    if (ImGui::Button("Back") || (!popup_open && ShortcutPressed(ImGuiKey_Escape)))
        screen = Screen::MainMenu;
    if (!message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kBad, "%s", message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();

    if (ui.slots.empty())
        ImGui::TextColored(theme::kTextDim, "No saves yet: start a new game.");
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ui.slots.empty() && ImGui::BeginTable("##save_table", 4, table_flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Last saved");
        ImGui::TableSetupColumn("Played");
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();
        for (const SaveSlot& slot : ui.slots) {
            ImGui::PushID(slot.path.string().c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (!slot.header) {
                // Unreadable: it can only be deleted.
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::kTextDim, "%s", slot.path.filename().string().c_str());
                ImGui::SameLine();
                theme::HelpMarker(("Can't be loaded: " + slot.error).c_str());
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                if (theme::DangerSmallButton("Delete")) {
                    ui.deleting = slot.path;
                    ui.deleting_name = slot.path.filename().string();
                }
                ImGui::PopID();
                continue;
            }
            const SaveHeader& header = *slot.header;

            if (ui.renaming == slot.path) {
                if (ui.focus_name) {
                    ImGui::SetKeyboardFocusHere();
                    ui.focus_name = false;
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##name", ui.new_name, sizeof ui.new_name,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    commit_rename = true;
                else if (ImGui::IsItemDeactivated())
                    ui.renaming.reset();  // Escape, or clicked elsewhere
            } else {
                // The whole row: double-click to load.
                ImGui::Selectable(header.name.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, fh));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    result = {SaveSelectResult::Action::Load, slot.path};
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(FormatSavedAt(header.saved_at).c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(FormatPlayed(header.played).c_str());

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Load"))
                result = {SaveSelectResult::Action::Load, slot.path};
            ImGui::SameLine();
            if (theme::PencilButton("##rename")) {
                ui.renaming = slot.path;
                std::snprintf(ui.new_name, sizeof ui.new_name, "%s", header.name.c_str());
                ui.focus_name = true;
            }
            ImGui::SetItemTooltip("Rename");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
                copy_from = slot.path;
            ImGui::SameLine();
            if (theme::DangerSmallButton("Delete")) {
                ui.deleting = slot.path;
                ui.deleting_name = header.name;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Opened here, not in the row, so the popup's ID doesn't depend on it.
    if (ui.deleting && !popup_open)
        ImGui::OpenPopup(kDeletePopup);
    bool refresh = false;
    if (ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"? This can't be undone.", ui.deleting_name.c_str());
        ImGui::Spacing();
        bool close = false;
        if (theme::DangerButton("Delete")) {
            std::string error;
            message = !ui.deleting || delete_save(*ui.deleting, error) ? "" : "Couldn't delete it: " + error;
            refresh = close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ShortcutPressed(ImGuiKey_Escape))
            close = true;
        if (close) {
            ui.deleting.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (commit_rename && ui.renaming) {
        const std::string name = Trimmed(ui.new_name);
        std::string error;
        if (name.empty())
            message = "A save needs a name.";
        else
            message = rename_save(*ui.renaming, name, error) ? "" : "Couldn't rename it: " + error;
        ui.renaming.reset();
        refresh = true;
    }
    if (copy_from) {
        std::string error;
        message = copy_save(saves_dir, *copy_from, error) ? "" : "Couldn't copy it: " + error;
    }
    if (refresh || copy_from)
        ui.slots = list_saves(saves_dir);

    ImGui::EndChild();
    ImGui::End();
    return result;
}

void DrawCredits(Screen& screen) {
    BeginFullscreenPage("##credits");
    const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 32.0f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::BeginChild("##credits_list", ImVec2(width, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    theme::SectionHeading("Credits");
    if (ImGui::Button("Back") || ShortcutPressed(ImGuiKey_Escape))
        screen = Screen::MainMenu;
    ImGui::Spacing();

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##credits_table", 4, table_flags)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Used for", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("By");
        ImGui::TableSetupColumn("License");
        ImGui::TableHeadersRow();
        for (const Credit& credit : kCredits) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(credit.name);
            ImGui::TableNextColumn();
            ImGui::TextColored(theme::kTextDim, "%s", credit.used_for);
            ImGui::TableNextColumn();
            ImGui::TextColored(theme::kTextDim, "%s", credit.author);
            ImGui::TableNextColumn();
            ImGui::TextColored(theme::kTextDim, "%s", credit.license);
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::End();
}
