#pragma once

#include <string>
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace ce::log
{
    void open_console(const char* title);
    void close_console();

    void raw(const char* level, const char* fmt, ...);

    inline void info (const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); raw("INFO", "%s", b); }
    inline void warn (const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); raw("WARN", "%s", b); }
    inline void err (const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); raw("ERR", "%s", b); }
    inline void debug(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); raw("DBG", "%s", b); }
}
