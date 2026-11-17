// See theme.h.
#include "ui/theme.h"

namespace ui::theme
{
    namespace
    {
        int g_colors = 0, g_vars = 0;
        ImVec4 c(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }
        void color(ImGuiCol idx, ImVec4 v)
        {
            ImGui::PushStyleColor(idx, v);
            ++g_colors;
        }
        void var(ImGuiStyleVar idx, float v)
        {
            ImGui::PushStyleVar(idx, v);
            ++g_vars;
        }
        void var(ImGuiStyleVar idx, ImVec2 v)
        {
            ImGui::PushStyleVar(idx, v);
            ++g_vars;
        }
    }

    void push()
    {
        g_colors = g_vars = 0;
        const ImVec4 red = c(178, 18, 26), redDark = c(110, 12, 18), orange = c(246, 166, 35), blue = c(126, 178, 230),
                     night = c(7, 10, 22, 0.96f), panel = c(16, 22, 44);
        color(ImGuiCol_Text, c(226, 234, 246));
        color(ImGuiCol_TextDisabled, c(120, 132, 156));
        color(ImGuiCol_WindowBg, night);
        color(ImGuiCol_ChildBg, c(0, 0, 0, 0.0f));
        color(ImGuiCol_PopupBg, c(10, 14, 30, 0.98f));
        color(ImGuiCol_Border, c(178, 18, 26, 0.55f));
        color(ImGuiCol_FrameBg, panel);
        color(ImGuiCol_FrameBgHovered, c(34, 46, 86));
        color(ImGuiCol_FrameBgActive, c(46, 62, 112));
        color(ImGuiCol_TitleBg, redDark);
        color(ImGuiCol_TitleBgActive, red);
        color(ImGuiCol_CheckMark, orange);
        color(ImGuiCol_SliderGrab, orange);
        color(ImGuiCol_SliderGrabActive, c(255, 200, 90));
        color(ImGuiCol_Button, redDark);
        color(ImGuiCol_ButtonHovered, red);
        color(ImGuiCol_ButtonActive, c(214, 40, 44));
        color(ImGuiCol_Header, c(246, 166, 35, 0.55f));
        color(ImGuiCol_HeaderHovered, c(246, 166, 35, 0.40f));
        color(ImGuiCol_HeaderActive, c(246, 166, 35, 0.70f));
        color(ImGuiCol_Separator, c(178, 18, 26, 0.70f));
        color(ImGuiCol_Tab, c(24, 18, 22));
        color(ImGuiCol_TabHovered, c(246, 166, 35, 0.75f));
        color(ImGuiCol_TabSelected, red);
        color(ImGuiCol_TabSelectedOverline, orange);
        color(ImGuiCol_TabDimmed, c(24, 18, 22));
        color(ImGuiCol_TabDimmedSelected, redDark);
        color(ImGuiCol_NavCursor, blue);
        color(ImGuiCol_ScrollbarGrab, c(110, 12, 18));
        color(ImGuiCol_TableRowBg, c(0, 0, 0, 0.0f));
        var(ImGuiStyleVar_WindowRounding, 0.0f);
        var(ImGuiStyleVar_FrameRounding, 2.0f);
        var(ImGuiStyleVar_TabRounding, 0.0f);
        var(ImGuiStyleVar_WindowBorderSize, 2.0f);
        var(ImGuiStyleVar_TabBarBorderSize, 2.0f);
    }

    void pop()
    {
        ImGui::PopStyleColor(g_colors);
        ImGui::PopStyleVar(g_vars);
        g_colors = g_vars = 0;
    }

    ImFont *titleFont()
    {
        ImFontAtlas *atlas = ImGui::GetIO().Fonts;
        return atlas->Fonts.Size > 1 ? atlas->Fonts[1] : nullptr;
    }
}
