#pragma once

#include <cstdint>
#include <atomic>

namespace ce::globals
{
    struct ModuleInfo
    {
        uintptr_t base = 0;
        size_t size = 0;
    };

    inline ModuleInfo g_game{};

    inline uintptr_t g_gworld_addr = 0;
    inline uintptr_t g_gobjects_addr = 0;
    inline uintptr_t g_gnames_addr = 0;
    inline uintptr_t g_append_string_addr = 0;

    inline std::atomic<bool> g_shutdown_requested{ false };
    inline std::atomic<bool> g_menu_open{ false };
    inline std::atomic<bool> g_initialized{ false };

    void init_module();
    bool resolve_ue_globals();
}
