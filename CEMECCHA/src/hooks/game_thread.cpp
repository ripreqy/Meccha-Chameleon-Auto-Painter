#include "game_thread.hpp"

#include "../core/logger.hpp"

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
    struct Task
    {
        std::function<void()> fn;
        std::atomic<bool>* done = nullptr;
    };

    std::mutex g_mtx;
    std::deque<Task> g_queue;
    std::atomic<HWND> g_hwnd{ nullptr };
    std::atomic<DWORD> g_game_thread_id{ 0 };
}

namespace ce::hooks
{
    static void seh_invoke(std::function<void()>& fn) noexcept
    {
        __try { if (fn) fn(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void game_thread_pump()
    {
        std::deque<Task> local;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            local.swap(g_queue);
        }
        for (auto& t : local)
        {
            seh_invoke(t.fn);
            if (t.done) t.done->store(true);
        }
    }

    bool run_on_game_thread(std::function<void()> task, bool wait, uint32_t timeout_ms)
    {
        HWND hwnd = g_hwnd.load();

        if (!hwnd)
        {

            ce::log::warn("[gt] dispatcher not wired to a HWND yet — task dropped");
            return false;
        }

        auto done_flag = std::make_shared<std::atomic<bool>>(false);

        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_queue.push_back({ std::move(task), done_flag.get() });
        }
        PostMessageW(hwnd, kGameThreadDispatchMsg, 0, 0);

        if (!wait) return true;

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!done_flag->load())
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                ce::log::warn("[gt] task timed out after %u ms", timeout_ms);
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    void _set_game_thread_hwnd(HWND hwnd)
    {
        g_hwnd.store(hwnd);
        g_game_thread_id.store(GetWindowThreadProcessId(hwnd, nullptr));
    }
}

extern "C" void ce_gt_set_hwnd(void* hwnd)
{
    ce::hooks::_set_game_thread_hwnd(static_cast<HWND>(hwnd));
}
