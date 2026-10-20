// ui.cpp — the screens: main menu, credits and the (empty) game screen, plus
// the developer demo windows.
#include "ui/ui.h"
#include "ui/ui_theme.h"

#include "imgui.h"
#include "implot.h"  // ImPlot::ShowDemoWindow

namespace {

namespace theme = ui::theme;

// A borderless, transparent window covering the whole display, for a screen's
// contents. Pair with ImGui::End().
void BeginFullscreenPage(const char* id) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoDocking);
}

// Text centered horizontally in the current window, at the cursor's height.
void CenteredText(const char* text, ImVec4 color) {
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::TextColored(color, "%s", text);
}

// A shortcut key went down this frame, unless the player is typing in a text
// field. (Not WantCaptureKeyboard: with keyboard navigation on, that is set
// whenever any ImGui window has focus, and the screens are ImGui windows.)
bool ShortcutPressed(ImGuiKey key) {
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(key, false);
}

}  // namespace

void DrawMainMenu(Screen& screen) {
    BeginFullscreenPage("##main_menu");

    const float fh = ImGui::GetFrameHeight();  // follows font size / DPI scaling
    const ImVec2 button_size = {fh * 8.0f, fh * 1.6f};
    const float spacing = ImGui::GetStyle().ItemSpacing.y;

    // Title plus two buttons, as one block centered vertically.
    ImGui::PushFont(nullptr, theme::kFontHuge);
    const float title_h = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    const float block_h = title_h + fh * 1.5f + 2.0f * button_size.y + spacing;
    ImGui::SetCursorPosY((ImGui::GetWindowHeight() - block_h) * 0.5f);

    ImGui::PushFont(nullptr, theme::kFontHuge);
    CenteredText("ORE IN ORBIT", theme::kInk);
    ImGui::PopFont();
    ImGui::Dummy({0.0f, fh * 1.5f - spacing});

    const float button_x = (ImGui::GetWindowWidth() - button_size.x) * 0.5f;
    ImGui::SetCursorPosX(button_x);
    if (theme::PrimaryButton("Play", button_size))
        screen = Screen::Playing;
    ImGui::SetCursorPosX(button_x);
    if (ImGui::Button("Credits", button_size))
        screen = Screen::Credits;

    ImGui::End();
}

void DrawCredits(Screen& screen) {
    BeginFullscreenPage("##credits");
    theme::SectionHeading("Credits");
    if (ImGui::Button("Back") || ShortcutPressed(ImGuiKey_Escape))
        screen = Screen::MainMenu;
    ImGui::End();
}

void DrawPlaying(Screen& screen) {
    if (ShortcutPressed(ImGuiKey_Escape))
        screen = Screen::MainMenu;
}

void DrawDevWindows() {
    static bool show_imgui_demo = false;
    static bool show_implot_demo = false;
    if (ShortcutPressed(ImGuiKey_F1))
        show_imgui_demo = !show_imgui_demo;
    if (ShortcutPressed(ImGuiKey_F2))
        show_implot_demo = !show_implot_demo;
    if (show_imgui_demo)
        ImGui::ShowDemoWindow(&show_imgui_demo);
    if (show_implot_demo)
        ImPlot::ShowDemoWindow(&show_implot_demo);
}
