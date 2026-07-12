#pragma once

#include <functional>
#include <cstdint>

namespace ce::hooks
{
    constexpr uint32_t kGameThreadDispatchMsg = 0x8000 + 42; // WMA
    void game_thread_pump();
    bool run_on_game_thread(std::function<void()> task, bool wait = true, uint32_t timeout_ms = 2000);
}
