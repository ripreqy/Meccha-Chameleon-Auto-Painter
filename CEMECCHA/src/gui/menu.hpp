#pragma once
#include <cstdint>

namespace ce::gui
{
    struct Menu
    {
        bool open = false;
        int toggle_key = 0x2D;
        bool prev_down = false;

        void tick_hotkey();
        void draw();
    };

    Menu& menu();
}
