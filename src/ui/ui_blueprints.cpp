// ui_blueprints.cpp — the Blueprints window: the blueprint library on disk
// (see ui_internal.h).
#include "ui/ui_internal.h"

#include "persistence/blueprint_file.h"

#include <cfloat>
#include <cstdio>
#include <utility>

namespace ui {

namespace fs = std::filesystem;

namespace {

// The Blueprints window's state. Reset, and the library read from disk,
// each time it opens; read again after each change it makes.
struct LibraryUiState {
    std::vector<BlueprintSlot> slots;
    bool loaded = false;
    std::string message;  // what the last action did, or why it failed
    ImVec4 message_color = theme::kBad;
    // Saving the clipboard: the name being typed.
    char save_name[64] = {};
    bool focus_save = false;
    // The blueprint whose name is being edited, and the edit.
    std::optional<fs::path> renaming;
    char new_name[64] = {};
    bool focus_name = false;
    // The blueprint waiting for the player to confirm deleting it.
    std::optional<fs::path> deleting;
    std::string deleting_name;
};
LibraryUiState g_library;

constexpr const char* kSaveBlueprintPopup = "Save blueprint";
constexpr const char* kDeleteBlueprintPopup = "Delete blueprint?";

}  // namespace

void DrawBlueprintLibrary(const fs::path& dir) {
    BlueprintUiState& bp = g_blueprints;
    LibraryUiState& ui = g_library;
    if (!bp.show_library) {
        ui.loaded = false;
        return;
    }
    if (!ui.loaded) {
        ui = {};
        ui.slots = list_blueprints(dir);
        ui.loaded = true;
    }
    const auto say = [&](std::string message, ImVec4 color) {
        ui.message = std::move(message);
        ui.message_color = color;
    };

    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 24.0f, fh * 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Blueprints", &bp.show_library, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    bool refresh = false;
    std::optional<fs::path> use;  // the blueprint to pick up, once the list is drawn
    bool commit_rename = false;

    ImGui::BeginDisabled(!bp.clipboard);
    if (theme::PrimaryButton("Save clipboard")) {
        std::snprintf(ui.save_name, sizeof ui.save_name, "%s",
                      bp.clipboard->name.empty() ? ("Blueprint " + std::to_string(ui.slots.size() + 1)).c_str()
                                                 : bp.clipboard->name.c_str());
        ui.focus_save = true;
        ImGui::OpenPopup(kSaveBlueprintPopup);
    }
    ImGui::SetItemTooltip("Keep what was last copied, by name");
    ImGui::SameLine();
    if (ImGui::Button("Copy clipboard as text")) {
        ImGui::SetClipboardText(blueprint_to_string(*bp.clipboard).c_str());
        say("Copied as text: paste it anywhere to share it", theme::kGood);
    }
    ImGui::SetItemTooltip("Put what was last copied on the system clipboard, as a blueprint string");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Import text")) {
        const char* text = ImGui::GetClipboardText();
        std::string error;
        std::optional<Blueprint> imported = blueprint_from_string(text ? text : "", error);
        if (!imported) {
            say("The system clipboard holds no blueprint: " + error, theme::kBad);
        } else {
            if (Trimmed(imported->name).empty())
                imported->name = "Imported blueprint";
            if (add_blueprint(dir, *imported, error))
                say("Imported \"" + imported->name + "\"", theme::kGood);
            else
                say("Couldn't save it: " + error, theme::kBad);
            refresh = true;
        }
    }
    ImGui::SetItemTooltip("Add a blueprint string from the system clipboard");

    if (!ui.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ui.message_color, "%s", ui.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();

    if (ui.slots.empty())
        ImGui::TextColored(theme::kTextDim, "No blueprints yet: copy an area (%s+C), then save it here.", kCtrlName);
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ui.slots.empty() && ImGui::BeginTable("##blueprint_table", 3, table_flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();
        for (const BlueprintSlot& slot : ui.slots) {
            ImGui::PushID(slot.path.string().c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (!slot.blueprint) {
                // Unreadable: it can only be deleted.
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::kTextDim, "%s", slot.path.filename().string().c_str());
                ImGui::SameLine();
                theme::HelpMarker(("Can't be read: " + slot.error).c_str());
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                if (theme::DangerSmallButton("Delete")) {
                    ui.deleting = slot.path;
                    ui.deleting_name = slot.path.filename().string();
                }
                ImGui::PopID();
                continue;
            }
            const Blueprint& blueprint = *slot.blueprint;
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
                // The whole row: double-click to pick it up.
                ImGui::Selectable(blueprint.name.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, fh));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    use = slot.path;
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(BlueprintSummary(blueprint).c_str());

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Use"))
                use = slot.path;
            ImGui::SetItemTooltip("Pick it up to paste");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy text")) {
                ImGui::SetClipboardText(blueprint_to_string(blueprint).c_str());
                say("Copied \"" + blueprint.name + "\" as text", theme::kGood);
            }
            ImGui::SetItemTooltip("Put it on the system clipboard, as a blueprint string to share");
            ImGui::SameLine();
            if (theme::PencilButton("##rename")) {
                ui.renaming = slot.path;
                std::snprintf(ui.new_name, sizeof ui.new_name, "%s", blueprint.name.c_str());
                ui.focus_name = true;
            }
            ImGui::SetItemTooltip("Rename");
            ImGui::SameLine();
            if (theme::DangerSmallButton("Delete")) {
                ui.deleting = slot.path;
                ui.deleting_name = blueprint.name;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Naming the clipboard, to save it.
    if (ImGui::BeginPopupModal(kSaveBlueprintPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Name:");
        if (ui.focus_save) {
            ImGui::SetKeyboardFocusHere();
            ui.focus_save = false;
        }
        ImGui::SetNextItemWidth(fh * 12.0f);
        const bool entered = ImGui::InputText("##save_name", ui.save_name, sizeof ui.save_name,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        bool close = false;
        if (theme::PrimaryButton("Save") || entered) {
            const std::string name = Trimmed(ui.save_name);
            std::string error;
            if (name.empty()) {
                say("A blueprint needs a name.", theme::kBad);
            } else if (bp.clipboard) {
                Blueprint named = *bp.clipboard;
                named.name = name;
                if (add_blueprint(dir, named, error))
                    say("Saved \"" + name + "\"", theme::kGood);
                else
                    say("Couldn't save it: " + error, theme::kBad);
                refresh = true;
            }
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ShortcutPressed(ImGuiKey_Escape))
            close = true;
        if (close)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Opened here, not in the row, so the popup's ID doesn't depend on it.
    if (ui.deleting && !ImGui::IsPopupOpen(kDeleteBlueprintPopup))
        ImGui::OpenPopup(kDeleteBlueprintPopup);
    if (ImGui::BeginPopupModal(kDeleteBlueprintPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"? This can't be undone.", ui.deleting_name.c_str());
        ImGui::Spacing();
        bool close = false;
        if (theme::DangerButton("Delete")) {
            std::string error;
            if (ui.deleting && !delete_blueprint(*ui.deleting, error))
                say("Couldn't delete it: " + error, theme::kBad);
            else
                ui.message.clear();
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
            say("A blueprint needs a name.", theme::kBad);
        else if (!rename_blueprint(*ui.renaming, name, error))
            say("Couldn't rename it: " + error, theme::kBad);
        ui.renaming.reset();
        refresh = true;
    }
    if (use)
        for (const BlueprintSlot& slot : ui.slots)
            if (slot.path == *use && slot.blueprint) {
                HoldBlueprint(*slot.blueprint);
                bp.show_library = false;
            }
    if (refresh)
        ui.slots = list_blueprints(dir);
    ImGui::End();
}

}  // namespace ui
