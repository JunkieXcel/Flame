//
// Created by Alex on 22.10.2026.
//

#ifndef FLAME_WIDESCREEN_FOV_H
#define FLAME_WIDESCREEN_FOV_H

#include <Windows.h>

typedef struct DIDEVICEOBJECTDATA DIDEVICEOBJECTDATA;

namespace patch::widescreen_fov {

    // F9 cycles the 4:3 reference height: 480 -> 600 -> 768 -> off -> 480
    void onKeyboard(DIDEVICEOBJECTDATA *data);
    void window_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

}


#endif // FLAME_WIDESCREEN_FOV_H
