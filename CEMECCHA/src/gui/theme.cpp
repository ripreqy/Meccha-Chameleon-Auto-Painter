#include "theme.hpp"
#include "../../third-party/imgui/imgui.h"

namespace ce::gui
{
    void apply_dark_neon_theme()
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 4.0f;
        s.FrameRounding = 3.0f;
        s.GrabRounding = 3.0f;
        s.ScrollbarRounding = 3.0f;
        s.PopupRounding = 3.0f;
        s.TabRounding = 3.0f;

        s.WindowPadding = ImVec2(12, 12);
        s.FramePadding = ImVec2(8, 4);
        s.ItemSpacing = ImVec2(8, 6);

        ImVec4* c = s.Colors;

        constexpr ImVec4 bg (0.06f, 0.06f, 0.06f, 0.96f);
        constexpr ImVec4 bg_alt (0.09f, 0.09f, 0.09f, 1.00f);
        constexpr ImVec4 border (0.15f, 0.15f, 0.15f, 1.00f);
        constexpr ImVec4 accent (0.00f, 1.00f, 0.35f, 1.00f);
        constexpr ImVec4 accent_dim(0.00f, 0.60f, 0.20f, 1.00f);
        constexpr ImVec4 text (0.90f, 0.95f, 0.92f, 1.00f);
        constexpr ImVec4 text_dim(0.55f, 0.60f, 0.57f, 1.00f);

        c[ImGuiCol_WindowBg] = bg;
        c[ImGuiCol_ChildBg] = bg;
        c[ImGuiCol_PopupBg] = bg;
        c[ImGuiCol_FrameBg] = bg_alt;
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        c[ImGuiCol_TitleBg] = bg_alt;
        c[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.14f, 0.10f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = bg_alt;
        c[ImGuiCol_MenuBarBg] = bg_alt;

        c[ImGuiCol_Text] = text;
        c[ImGuiCol_TextDisabled] = text_dim;
        c[ImGuiCol_Border] = border;
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        c[ImGuiCol_Button] = accent_dim;
        c[ImGuiCol_ButtonHovered] = accent;
        c[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.75f, 0.25f, 1.00f);

        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.20f, 1.00f, 0.55f, 1.00f);

        c[ImGuiCol_Header] = ImVec4(0.10f, 0.18f, 0.10f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.24f, 0.14f, 1.00f);
        c[ImGuiCol_HeaderActive] = accent_dim;

        c[ImGuiCol_Tab] = ImVec4(0.09f, 0.14f, 0.09f, 1.00f);
        c[ImGuiCol_TabHovered] = accent_dim;
        c[ImGuiCol_TabActive] = ImVec4(0.10f, 0.20f, 0.12f, 1.00f);

        c[ImGuiCol_Separator] = border;
        c[ImGuiCol_SeparatorHovered] = accent_dim;
        c[ImGuiCol_SeparatorActive] = accent;

        c[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.40f, 0.15f, 0.60f);
        c[ImGuiCol_ResizeGripHovered] = accent_dim;
        c[ImGuiCol_ResizeGripActive] = accent;

        c[ImGuiCol_ScrollbarBg] = bg_alt;
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered]= accent_dim;
        c[ImGuiCol_ScrollbarGrabActive]= accent;

        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered]= ImVec4(0.20f, 1.00f, 0.55f, 1.00f);
    }
}
