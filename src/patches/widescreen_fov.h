//
// Created by Alex on 22.10.2026.
//

#ifndef FLAME_WIDESCREEN_FOV_H
#define FLAME_WIDESCREEN_FOV_H

#include <Windows.h>
#include <dk2/button/CButton.h>
#include <dk2/utils/Area4s.h>

typedef struct DIDEVICEOBJECTDATA DIDEVICEOBJECTDATA;

namespace patch::widescreen_fov {

    // F9 cycles the 4:3 reference height: 480 -> 600 -> 768 -> off -> 480
    void onKeyboard(DIDEVICEOBJECTDATA *data);
    void window_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

    // Graphics Options entry: title label and switch (left click forward, right click back)
    dk2::ButtonCfg GraphicsOptions_WidescreenFovTitle_btn(dk2::Area4s a1, dk2::Area4s a2);
    dk2::ButtonCfg GraphicsOptions_WidescreenFovSwitch_btn(dk2::Area4s a1, dk2::Area4s a2);

}


#endif // FLAME_WIDESCREEN_FOV_H
