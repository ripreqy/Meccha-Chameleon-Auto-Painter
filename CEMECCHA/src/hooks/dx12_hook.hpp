#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include <vector>

namespace ce::hooks
{
    using RenderFn = std::function<void()>;
    using InputFn = std::function<bool(void* hwnd, uint32_t msg, uintptr_t wparam, intptr_t lparam)>;
    bool install_dx12(RenderFn render, InputFn input);
    void uninstall_dx12();
    bool is_installed();
    bool readback_last_frame(std::vector<uint32_t>* out_pixels, int* out_w, int* out_h, uint32_t* out_fmt);
}
