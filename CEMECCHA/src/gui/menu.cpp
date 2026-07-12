#include "menu.hpp"
#include "theme.hpp"
#include "../features/manager.hpp"
#include "../features/autopainter.hpp"
#include "../features/esp.hpp"
#include "../core/globals.hpp"
#include "../../third-party/imgui/imgui.h"
#include <Windows.h>

namespace ce::gui
{
    Menu& menu() { static Menu m; return m; }

    void Menu::tick_hotkey()
    {
        const bool down = (GetAsyncKeyState(toggle_key) & 0x8000) != 0;
        if (down && !prev_down)
        {
            open = !open;
            ce::globals::g_menu_open.store(open);
        }
        prev_down = down;
    }

    void Menu::draw()
    {

        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = open;

        if (!open) return;

        ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
        ImGui::Begin("CEMECCHA — MECCHA Chameleon Internal", &open, ImGuiWindowFlags_NoCollapse);

        ImGui::TextColored(ImVec4(0.f, 1.f, 0.4f, 1.f), "> CEMECCHA v1.0");
        ImGui::TextDisabled("INSERT toggles menu   |   default Paint hotkey: F1");
        ImGui::Separator();

        auto& mgr = ce::features::Manager::get();

        if (ImGui::BeginTabBar("cemeccha_tabs"))
        {
            if (ImGui::BeginTabItem("Painter"))
            {
                if (auto* p = mgr.get_by_name<ce::features::AutoPainter>("AutoPainter"))
                    p->draw();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("ESP"))
            {
                if (auto* e = mgr.get_by_name<ce::features::ESP>("ESP"))
                    e->draw();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("About"))
            {
                ImGui::TextWrapped( "CEMECCHA internal for MECCHA CHAMELEON. Auto Painter: 1:1 chameleon " "paint via SceneCapture2D + runtime triangle cache. ESP: skeleton / " "box / name / distance over hider + hunter actors.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
