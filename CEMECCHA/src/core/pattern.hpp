#pragma once

#include <cstdint>

namespace ce::pattern
{
    uintptr_t scan(uintptr_t start, size_t size, const char* signature);
    uintptr_t scan_module(const wchar_t* module_name, const char* signature);
    uintptr_t resolve_rip_rel32(uintptr_t inst_addr, int offset_of_disp, int inst_size);
}
