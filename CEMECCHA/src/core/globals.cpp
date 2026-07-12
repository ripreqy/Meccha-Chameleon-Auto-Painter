#include "globals.hpp"
#include "logger.hpp"
#include <Windows.h>

namespace ce::globals
{
    void init_module()
    {
        HMODULE hm = GetModuleHandleW(nullptr);
        if (!hm) return;

        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hm);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(reinterpret_cast<uint8_t*>(hm) + dos->e_lfanew);

        g_game.base = reinterpret_cast<uintptr_t>(hm);
        g_game.size = static_cast<size_t>(nt->OptionalHeader.SizeOfImage);

        log::info("game module base = 0x%llX  size = 0x%llX", g_game.base, g_game.size);
    }

    bool resolve_ue_globals()
    {

        constexpr uintptr_t GWORLD_RVA = 0x9C86620;
        constexpr uintptr_t GOBJECTS_RVA = 0x9F3D7D0;
        constexpr uintptr_t GNAMES_RVA = 0x9E17C48;
        constexpr uintptr_t APPEND_STRING_RVA = 0x13B3290;

        if (!g_game.base)
        {
            log::err("module base not set — cannot resolve UE globals");
            return false;
        }

        g_gworld_addr = g_game.base + GWORLD_RVA;
        g_gobjects_addr = g_game.base + GOBJECTS_RVA;
        g_gnames_addr = g_game.base + GNAMES_RVA;
        g_append_string_addr = g_game.base + APPEND_STRING_RVA;

        log::info("GWorld @ 0x%llX", g_gworld_addr);
        log::info("GObjects @ 0x%llX", g_gobjects_addr);
        log::info("GNames @ 0x%llX", g_gnames_addr);
        log::info("AppendString @ 0x%llX", g_append_string_addr);
        return true;
    }
}
