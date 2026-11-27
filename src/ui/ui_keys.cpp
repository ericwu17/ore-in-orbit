// ui_keys.cpp — the action keys, which the player rebinds in the Settings
// menu, and that menu's list of them (see ui_internal.h); loading and saving
// them is ui.h's.
#include "ui/ui.h"
#include "ui/ui_internal.h"

#include <array>
#include <cfloat>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ui {

namespace {

// An action's name in settings.json, its line in the Settings menu, and
// its key unless the player rebinds it.
struct ActionInfo {
    const char* id;
    const char* label;
    ImGuiKeyChord default_key;
};

constexpr std::array<ActionInfo, kActionCount> kActions = {{
    {"pause", "Pause / put away / close", ImGuiKey_Escape},
    {"previous_ship", "Previous ship", ImGuiKey_LeftBracket},
    {"next_ship", "Next ship", ImGuiKey_RightBracket},
    {"star_map", "Star map", ImGuiKey_M},
    {"build_1", "Build palette slot 1", ImGuiKey_1},
    {"build_2", "Build palette slot 2", ImGuiKey_2},
    {"build_3", "Build palette slot 3", ImGuiKey_3},
    {"build_4", "Build palette slot 4", ImGuiKey_4},
    {"build_5", "Build palette slot 5", ImGuiKey_5},
    {"build_6", "Build palette slot 6", ImGuiKey_6},
    {"build_7", "Build palette slot 7", ImGuiKey_7},
    {"build_8", "Build palette slot 8", ImGuiKey_8},
    {"build_9", "Build palette slot 9", ImGuiKey_9},
    {"pick", "Pick the building under the mouse", ImGuiKey_Q},
    {"rotate", "Turn (with Shift: back)", ImGuiKey_R},
    {"copy", "Copy an area", ImGuiMod_Ctrl | ImGuiKey_C},
    {"paste", "Paste the last copy", ImGuiMod_Ctrl | ImGuiKey_V},
    {"undo", "Undo", ImGuiMod_Ctrl | ImGuiKey_Z},
    {"blueprints", "Blueprint library", ImGuiKey_B},
    {"encyclopedia", "Encyclopedia", ImGuiKey_E},
    {"keys_list", "List of keys", ImGuiKey_Slash},
    {"pan", "Pan (hold)", ImGuiKey_Space},
}};

// The modifiers a key can have: Shift is taken (turning back, x5, the
// buildings' settings), and Super is Ctrl on macOS.
constexpr ImGuiKeyChord kBindableMods = ImGuiMod_Ctrl | ImGuiMod_Alt;

struct KeyState {
    std::array<ImGuiKeyChord, kActionCount> keys = [] {
        std::array<ImGuiKeyChord, kActionCount> k{};
        for (std::size_t i = 0; i < k.size(); ++i)
            k[i] = kActions[i].default_key;
        return k;
    }();
    std::array<std::string, kActionCount> names;  // KeyName()'s, kept up to date
    bool changed = false;                          // since TakeKeyBindingsChanged()
    // The Settings menu waiting for a new key for this action, and the
    // keyboard navigation it turned off meanwhile (so Space or Enter don't
    // press a button instead).
    std::optional<Action> capturing;
    bool nav_was_on = false;
};
KeyState g_keys;

std::size_t Index(Action a) {
    return static_cast<std::size_t>(a);
}

ImGuiKey KeyOf(ImGuiKeyChord chord) {
    return static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
}

// The key on its own, as on a key cap.
std::string KeyOnlyName(ImGuiKey key) {
    switch (key) {
    case ImGuiKey_Escape:       return "Esc";
    case ImGuiKey_Slash:        return "?";
    case ImGuiKey_LeftBracket:  return "[";
    case ImGuiKey_RightBracket: return "]";
    case ImGuiKey_Apostrophe:   return "'";
    case ImGuiKey_Comma:        return ",";
    case ImGuiKey_Minus:        return "-";
    case ImGuiKey_Period:       return ".";
    case ImGuiKey_Semicolon:    return ";";
    case ImGuiKey_Equal:        return "=";
    case ImGuiKey_Backslash:    return "\\";
    case ImGuiKey_LeftArrow:    return "Left";
    case ImGuiKey_RightArrow:   return "Right";
    case ImGuiKey_UpArrow:      return "Up";
    case ImGuiKey_DownArrow:    return "Down";
    default:                    return ImGui::GetKeyName(key);
    }
}

std::string DisplayName(ImGuiKeyChord chord) {
    std::string name;
    if (chord & ImGuiMod_Ctrl)
        name += std::string(kCtrlName) + "+";
    if (chord & ImGuiMod_Alt)
        name += "Alt+";
    return name + KeyOnlyName(KeyOf(chord));
}

// The key as settings.json keeps it: ImGui's own key names ("LeftBracket"),
// after "Ctrl+" or "Alt+".
std::string SavedName(ImGuiKeyChord chord) {
    std::string name;
    if (chord & ImGuiMod_Ctrl)
        name += "Ctrl+";
    if (chord & ImGuiMod_Alt)
        name += "Alt+";
    return name + ImGui::GetKeyName(KeyOf(chord));
}

// The keys a player can bind: the keyboard's, but not the modifiers on
// their own, Escape (it cancels picking a key) or the developer windows'.
bool Bindable(ImGuiKey key) {
    if (key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper)
        return false;
    return key != ImGuiKey_Escape && key != ImGuiKey_GraveAccent && key != ImGuiKey_F2;
}

// SavedName()'s chord back, or nothing for one that isn't a key.
std::optional<ImGuiKeyChord> ParseSaved(std::string_view text) {
    ImGuiKeyChord mods = 0;
    for (;;) {
        if (text.starts_with("Ctrl+")) {
            mods |= ImGuiMod_Ctrl;
            text.remove_prefix(5);
        } else if (text.starts_with("Alt+")) {
            mods |= ImGuiMod_Alt;
            text.remove_prefix(4);
        } else {
            break;
        }
    }
    for (int k = ImGuiKey_Tab; k <= ImGuiKey_Oem102; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        // Escape can't be picked in the menu, but it is the pause key's own.
        if ((Bindable(key) || key == ImGuiKey_Escape) && text == ImGui::GetKeyName(key))
            return mods | key;
    }
    return std::nullopt;
}

void RefreshNames() {
    for (std::size_t i = 0; i < g_keys.keys.size(); ++i)
        g_keys.names[i] = DisplayName(g_keys.keys[i]);
}

// Gives `action` the key `chord`; an action that had it already takes
// `action`'s old key, so no key does two things and none is lost.
void Bind(Action action, ImGuiKeyChord chord) {
    const ImGuiKeyChord old = g_keys.keys[Index(action)];
    for (ImGuiKeyChord& k : g_keys.keys)
        if (k == chord)
            k = old;
    g_keys.keys[Index(action)] = chord;
    g_keys.changed = true;
    RefreshNames();
}

void StopCapturing() {
    if (!g_keys.capturing)
        return;
    g_keys.capturing.reset();
    if (g_keys.nav_was_on)
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void StartCapturing(Action action) {
    StopCapturing();
    g_keys.capturing = action;
    ImGuiIO& io = ImGui::GetIO();
    g_keys.nav_was_on = (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

// While capturing: the key pressed this frame, if any, is the action's.
// Escape cancels. True if this frame ended the capture.
bool CaptureKey() {
    if (!g_keys.capturing)
        return false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        StopCapturing();
        return true;
    }
    for (int k = ImGuiKey_Tab; k <= ImGuiKey_Oem102; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        if (!Bindable(key) || !ImGui::IsKeyPressed(key, false))
            continue;
        const Action action = *g_keys.capturing;
        StopCapturing();
        Bind(action, (ImGui::GetIO().KeyMods & kBindableMods) | key);
        PlayUi(audio::Sound::UiConfirm);
        return true;
    }
    return false;
}

const char* NameAt(std::size_t i) {
    if (g_keys.names[i].empty())
        RefreshNames();
    return g_keys.names[i].c_str();
}

}  // namespace

bool ActionPressed(Action action) {
    const ImGuiKeyChord chord = g_keys.keys[Index(action)];
    return ShortcutPressed(KeyOf(chord)) && (ImGui::GetIO().KeyMods & kBindableMods) == (chord & kBindableMods);
}

bool ActionDown(Action action) {
    const ImGuiKeyChord chord = g_keys.keys[Index(action)];
    const ImGuiKeyChord mods = chord & kBindableMods;
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyDown(KeyOf(chord)) &&
           (ImGui::GetIO().KeyMods & mods) == mods;
}

const char* KeyName(Action action) {
    return NameAt(Index(action));
}

bool CapturingKey() {
    return g_keys.capturing.has_value();
}

void DrawKeyBindings(float height) {
    const float fh = ImGui::GetFrameHeight();
    // Before the buttons, so the key that ends a capture can't also press
    // one.
    const bool just_captured = CaptureKey();
    bool waiting_hovered = false;  // the button waiting for a key
    ImGui::BeginChild("##key_bindings", ImVec2(fh * 18.0f, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, fh * 6.0f);
        for (std::size_t i = 0; i < kActions.size(); ++i) {
            const Action action = static_cast<Action>(i);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kActions[i].label);
            ImGui::TableNextColumn();
            const bool waiting = g_keys.capturing == action;
            const bool pressed =
                theme::SegmentedButton(waiting ? "Press a key" : NameAt(i), waiting, ImVec2(-FLT_MIN, 0.0f));
            waiting_hovered |= waiting && ImGui::IsItemHovered();
            if (pressed && !just_captured) {
                if (waiting)
                    StopCapturing();
                else
                    StartCapturing(action);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    // A click anywhere else gives up on the new key (the buttons act when
    // let go, after this).
    if (g_keys.capturing && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !waiting_hovered)
        StopCapturing();
    ImGui::BeginDisabled(g_keys.keys == KeyState{}.keys);
    if (ImGui::Button("Reset to defaults")) {
        StopCapturing();
        for (std::size_t i = 0; i < kActions.size(); ++i)
            g_keys.keys[i] = kActions[i].default_key;
        g_keys.changed = true;
        RefreshNames();
    }
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fh * 18.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Click a key, then press the new one (with %s or Alt if you like); Esc cancels. "
                       "A key another action had swaps over to it. Shift and the mouse stay as they are.",
                       kCtrlName);
    ImGui::PopTextWrapPos();
}

}  // namespace ui

using namespace ui;

void LoadKeyBindings(const std::map<std::string, std::string>& saved) {
    for (std::size_t i = 0; i < kActions.size(); ++i) {
        g_keys.keys[i] = kActions[i].default_key;
        const auto it = saved.find(kActions[i].id);
        if (it == saved.end())
            continue;
        if (const std::optional<ImGuiKeyChord> chord = ParseSaved(it->second))
            g_keys.keys[i] = *chord;
    }
    g_keys.changed = false;
    RefreshNames();
}

std::map<std::string, std::string> KeyBindingsToSave() {
    std::map<std::string, std::string> saved;
    for (std::size_t i = 0; i < kActions.size(); ++i)
        if (g_keys.keys[i] != kActions[i].default_key)
            saved[kActions[i].id] = SavedName(g_keys.keys[i]);
    return saved;
}

bool TakeKeyBindingsChanged() {
    return std::exchange(g_keys.changed, false);
}
