#include <Windows.h>

#include "core/logger.hpp"
#include "core/globals.hpp"
#include "sdk/ue_object.hpp"
#include "hooks/dx12_hook.hpp"
#include "gui/menu.hpp"
#include "gui/theme.hpp"
#include "features/manager.hpp"
#include "features/autopainter.hpp"
#include "features/esp.hpp"

#include "../third-party/imgui/imgui.h"

namespace
{
    HMODULE g_self = nullptr;
    HANDLE g_loader_thread = nullptr;

    void wait_for_dxgi()
    {
        for (int i = 0; i < 200; ++i)
        {
            if (GetModuleHandleW(L"dxgi.dll") && GetModuleHandleW(L"d3d12.dll"))
            return;
            Sleep(50);
        }
    }

    void on_render()
    {
        static bool once = false;
        if (!once)
        {
            ce::gui::apply_dark_neon_theme();
            once = true;
        }
        ce::gui::menu().tick_hotkey();
        ce::features::Manager::get().tick();
        ce::gui::menu().draw();
    }

    bool on_input(void*, uint32_t, uintptr_t, intptr_t)
    {
        return ce::globals::g_menu_open.load();
    }

    DWORD WINAPI loader(LPVOID)
    {
        ce::log::open_console("CEMECCHA");
        ce::log::info("  CEMECCHS");
        ce::log::info("  Built: " __DATE__ " " __TIME__);

        ce::globals::init_module();
        wait_for_dxgi();

        if (!ce::globals::resolve_ue_globals())
        {
            ce::log::err("failed to resolve UE globals — aborting");
            return 1;
        }
        ce::ue::init_reflection_caches();

        auto& mgr = ce::features::Manager::get();
        auto& painter = mgr.add<ce::features::AutoPainter>();
        painter.apply_preset(ce::features::AutoPainter::Preset::Balanced);
        (void)mgr.add<ce::features::ESP>();

        if (!ce::hooks::install_dx12(on_render, on_input))
        {
            ce::log::err("DX12 hook install failed");
            return 1;
        }

        ce::globals::g_initialized.store(true);
        ce::log::info("CEMECCHA fully initialized — press INSERT for menu, F1 to paint");

        while (!ce::globals::g_shutdown_requested.load())
        {
            if (GetAsyncKeyState(VK_END) & 0x8000)
            {
                ce::log::info("VK_END pressed — shutting down");
                ce::globals::g_shutdown_requested.store(true);
                break;
            }
            Sleep(100);
        }

        ce::hooks::uninstall_dx12();
        ce::log::info("CEMECCHA unloading");
        ce::log::close_console();

        FreeLibraryAndExitThread(g_self, 0);
    }
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        g_self = mod;
        g_loader_thread = CreateThread(nullptr, 0, loader, nullptr, 0, nullptr);
        if (g_loader_thread) CloseHandle(g_loader_thread);
    }
    return TRUE;
}
