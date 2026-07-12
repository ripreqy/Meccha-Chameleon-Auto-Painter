#include "pattern.hpp"

#include <Windows.h>
#include <cstring>
#include <vector>

namespace
{
    struct ByteMask
    {
        uint8_t value;
        bool wildcard;
    };

    std::vector<ByteMask> parse(const char* sig)
    {
        std::vector<ByteMask> out;
        while (*sig)
        {
            if (*sig == ' ') { ++sig; continue; }
            if (*sig == '?')
            {
                out.push_back({ 0, true });
                if (*(sig + 1) == '?') ++sig;
                ++sig;
                continue;
            }

            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(sig[0]);
            int lo = hex(sig[1]);
            if (hi < 0 || lo < 0) break;
            out.push_back({ static_cast<uint8_t>((hi << 4) | lo), false });
            sig += 2;
        }
        return out;
    }
}

namespace ce::pattern
{
    uintptr_t scan(uintptr_t start, size_t size, const char* signature)
    {
        auto mask = parse(signature);
        if (mask.empty()) return 0;

        const size_t n = mask.size();
        const auto* base = reinterpret_cast<const uint8_t*>(start);

        for (size_t i = 0; i + n <= size; ++i)
        {
            bool hit = true;
            for (size_t j = 0; j < n; ++j)
            {
                if (mask[j].wildcard) continue;
                if (base[i + j] != mask[j].value) { hit = false; break; }
            }
            if (hit) return start + i;
        }
        return 0;
    }

    uintptr_t scan_module(const wchar_t* module_name, const char* signature)
    {
        HMODULE hm = GetModuleHandleW(module_name);
        if (!hm) return 0;

        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hm);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(reinterpret_cast<uint8_t*>(hm) + dos->e_lfanew);

        const auto base = reinterpret_cast<uintptr_t>(hm);
        const auto size = static_cast<size_t>(nt->OptionalHeader.SizeOfImage);

        return scan(base, size, signature);
    }

    uintptr_t resolve_rip_rel32(uintptr_t inst_addr, int offset_of_disp, int inst_size)
    {
        if (!inst_addr) return 0;
        int32_t disp = *reinterpret_cast<int32_t*>(inst_addr + offset_of_disp);
        return inst_addr + inst_size + disp;
    }
}
