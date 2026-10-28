//
// Created by Alex on 29.10.2026.
//

#ifndef FLAME_ESC_GOES_BACK_H
#define FLAME_ESC_GOES_BACK_H

#include <Windows.h>

namespace patch::esc_goes_back {

    // in the main menu, Esc presses the open screen's Back button, as a mouse click would
    void window_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

}

#endif //FLAME_ESC_GOES_BACK_H
