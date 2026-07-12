#include "logger.hpp"

#include <Windows.h>
#include <cstdarg>
#include <ctime>

namespace
{
    FILE* g_console_out = nullptr;
    FILE* g_console_err = nullptr;
    std::mutex g_mtx;
    bool g_open = false;
}

namespace ce::log
{
    void open_console(const char* title)
    {
        if (g_open) return;
        if (!AllocConsole()) return;

        freopen_s(&g_console_out, "CONOUT$", "w", stdout);
        freopen_s(&g_console_err, "CONOUT$", "w", stderr);

        if (title) SetConsoleTitleA(title);

        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

        g_open = true;
    }

    void close_console()
    {
        if (!g_open) return;
        if (g_console_out) fclose(g_console_out);
        if (g_console_err) fclose(g_console_err);
        FreeConsole();
        g_open = false;
    }

    void raw(const char* level, const char* fmt, ...)
    {
        std::lock_guard<std::mutex> lk(g_mtx);

        std::time_t t = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &t);

        const char* color = "\x1b[37m";
        if (level[0] == 'E') color = "\x1b[91m";
        else if (level[0] == 'W') color = "\x1b[93m";
        else if (level[0] == 'D') color = "\x1b[90m";
        else if (level[0] == 'I') color = "\x1b[92m";

        std::printf("%s[%02d:%02d:%02d] [%s]\x1b[0m ", color, lt.tm_hour, lt.tm_min, lt.tm_sec, level);

        va_list ap;
        va_start(ap, fmt);
        std::vprintf(fmt, ap);
        va_end(ap);

        std::printf("\n");
        std::fflush(stdout);
    }
}
