//
// Created by Alex on 22.10.2026.
//

#include "widescreen_fov.h"
#include "dk2/CBridge.h"
#include "dk2/CDefaultPlayerInterface.h"
#include "dk2/button/button_types.h"
#include "dk2/CCamera.h"
#include "dk2/CPCEngineInterface.h"
#include "dk2/MyWindow.h"
#include "dk2/utils/MyCameraState.h"
#include "dk2_functions.h"
#include "dk2_globals.h"
#include <tools/flame_config.h>
#include <dinput.h>
#include <cstdio>


flame_config::define_flame_option<bool> o_widescreenFov(
    "flame:widescreen-fov", flame_config::OG_Config,
    "Keep the 3D camera's vertical field of view and distance of a 4:3 reference resolution\n"
    "and widen only the horizontal view on wider screens (nothing stretched or cropped)",
    true
);

flame_config::define_flame_option<int> o_widescreenFovReferenceHeight(
    "flame:widescreen-fov-reference-height", flame_config::OG_Config,
    "Height of the 4:3 resolution whose camera view is kept by flame:widescreen-fov (e.g. 480, 600, 768)",
    600
);

namespace {

    // the loader redirects every reference to MyCameraState_575A00 to Flame,
    // the original body stays at its address and can still be called
    auto *const c_original_MyCameraState_575A00 = (void (__cdecl *)(dk2::MyCameraState *, float)) 0x00575A00;

    constexpr uint32_t c_cameraModeMap = 7;  // left untouched
    constexpr uint32_t c_cameraModePath = 0x12;

    // CPCEngineInterface::maybe_cameraFun builds the camera with
    //   focal ww = lens * 320 / 4096 (pixels, independent of the screen size)
    //   distance d = zoom * (-1/4096) * 640 / screenWidth along column 2 of the camera matrix
    // so the field of view widens with the screen width. Rebuild it as on a 4:3 screen of the reference height:
    //   ww' = ww * screenHeight / refHeight,  d' = zoom * (-1/4096) * 480 / refHeight
    void adjustNormalCamera(dk2::MyCameraState &state, dk2::CCamera &camera, int width, int height, int refHeight) {
        float zoom = (float) (int) camera.curZoomLevel * (-1.0f / 4096.0f);
        float distance = zoom * 640.0f / (float) width;
        float refDistance = zoom * 480.0f / (float) refHeight;
        float delta = refDistance - distance;
        // view direction is column 2 (camera space z = m[0][2]*x + m[1][2]*y + m[2][2]*z, see Mat3x3f::sub_594E10)
        state.v3f.x += delta * state.m.m[0][2];
        state.v3f.y += delta * state.m.m[1][2];
        state.v3f.z += delta * state.m.m[2][2];
        state.ww240 *= (float) height / (float) refHeight;
    }

    // the path/cutscene camera uses ww = screenWidth / (2 * tan(fov / 2)), a fixed horizontal view.
    // use the width of a 4:3 screen of the same height instead
    void adjustPathCamera(dk2::MyCameraState &state, int width, int height) {
        if (width * 3 <= height * 4) return;  // not wider than 4:3
        state.ww240 *= (float) height * 4.0f / 3.0f / (float) width;
    }

    // settings in cycling order: 480, 600, 768, off
    const int c_referenceHeights[] = {480, 600, 768};
    constexpr int c_stateCount = ARRAYSIZE(c_referenceHeights) + 1;
    constexpr int c_stateOff = ARRAYSIZE(c_referenceHeights);

    // direction: +1 forward, -1 back
    void stepSetting(int direction) {
        int state;
        if (!*o_widescreenFov) {
            state = c_stateOff;
        } else {
            int refHeight = *o_widescreenFovReferenceHeight;
            state = -1;
            int below = -1;  // last preset lower than a custom height
            for (int i = 0; i < ARRAYSIZE(c_referenceHeights); ++i) {
                if (c_referenceHeights[i] == refHeight) state = i;
                if (c_referenceHeights[i] < refHeight) below = i;
            }
            // a custom height steps to the neighbouring preset
            if (state < 0) state = direction > 0 ? below : below + 1;
        }
        state = ((state + direction) % c_stateCount + c_stateCount) % c_stateCount;
        if (state == c_stateOff) {
            o_widescreenFov.set(false);
        } else {
            o_widescreenFov.set(true);
            o_widescreenFovReferenceHeight.set(c_referenceHeights[state]);
        }
        // the config is saved by the game loop when flame_config::changed()
    }

    void formatSetting(wchar_t *buf, size_t count) {
        if (*o_widescreenFov) {
            swprintf(buf, count, L"%d", *o_widescreenFovReferenceHeight);
        } else {
            swprintf(buf, count, L"off");
        }
    }

    // dinput and window messages can both report the same press
    constexpr DWORD c_hotkeyRepeatMs = 250;
    DWORD g_lastHotkeyMs = 0;

    void onHotkey() {
        DWORD now = GetTickCount();
        if (g_lastHotkeyMs != 0 && now - g_lastHotkeyMs < c_hotkeyRepeatMs) return;
        g_lastHotkeyMs = now;

        stepSetting(1);

        char message[64];
        if (*o_widescreenFov) {
            snprintf(message, sizeof(message), "Widescreen FOV: 4:3 reference height %d", *o_widescreenFovReferenceHeight);
        } else {
            snprintf(message, sizeof(message), "Widescreen FOV: off");
        }
        printf("[widescreen-fov] %s\n", message);
        dk2::ProbablyConsole_instance.appendOutput(message);
    }

    void __cdecl CButton_handleLeftClick_WidescreenFovSwitch(int arg1, int arg2, dk2::CFrontEndComponent *front) {
        stepSetting(1);
    }

    void __cdecl CButton_handleRightClick_WidescreenFovSwitch(int arg1, int arg2, dk2::CFrontEndComponent *front) {
        stepSetting(-1);
    }

    int printButtonText(dk2::CButton *btn, dk2::CDefaultPlayerInterface *defplif, const wchar_t *text, bool highlightOnHover) {
        if (btn->f5D_isVisible != 1) return 0;
        dk2::AABB tmp;
        dk2::Area4i pos = *(dk2::Area4i *) defplif->cgui_manager.scaleAabb_2560_1920(&tmp, (dk2::AABB *) &btn->pos);
        unsigned __int8 brightness = 0xFF;
        if (highlightOnHover && (!btn->f34_idxHigh || !btn->f45_containsCursor)) brightness = -56;
        dk2::Bgraf pixelMask {brightness, brightness, brightness, 0, 0};
        uint8_t textMb[128];
        dk2::UniToMb_convert((wchar_t *) text, textMb, sizeof(textMb));
        return defplif->printText(
            &defplif->_options,
            pos.x, pos.y, textMb,
            &pixelMask,
            0x11, 0, dk2::FontObj_3_instance, 1
        );
    }

    int __cdecl CClickButton_render_WidescreenFovTitle(dk2::CButton *btn, dk2::CDefaultPlayerInterface *defplif) {
        return printButtonText(btn, defplif, L"Widescreen camera", false);
    }

    int __cdecl CClickButton_render_WidescreenFovSwitch(dk2::CButton *btn, dk2::CDefaultPlayerInterface *defplif) {
        wchar_t setting[16];
        formatSetting(setting, ARRAYSIZE(setting));
        wchar_t text[64];
        swprintf(text, ARRAYSIZE(text), L"4:3 reference height: %s", setting);
        return printButtonText(btn, defplif, text, true);
    }

}

void patch::widescreen_fov::onKeyboard(DIDEVICEOBJECTDATA *data) {  // if dinput is used
    bool isPressed = (data->dwData & 0x80) != 0;
    if (data->dwOfs == DIK_F9 && isPressed) onHotkey();
}

void patch::widescreen_fov::window_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_KEYDOWN && wParam == VK_F9 && (lParam & (1 << 30)) == 0) onHotkey();
}

dk2::ButtonCfg patch::widescreen_fov::GraphicsOptions_WidescreenFovTitle_btn(dk2::Area4s a1, dk2::Area4s a2) {
    return {  // title "Widescreen camera"
        BT_CClickButton, 3, 0, NULL, NULL, 0, 0, 0x00000000, 0x00000000, 0,
        a1.x, a1.y, (uint16_t) a1.w, (uint16_t) a1.h,
        (uint16_t) a2.x, (uint16_t) a2.y, (uint16_t) a2.w, (uint16_t) a2.h,
        0, NULL, CClickButton_render_WidescreenFovTitle, 0x00000000, 0, 0x00000000, 0x00000000, 0
    };
}

dk2::ButtonCfg patch::widescreen_fov::GraphicsOptions_WidescreenFovSwitch_btn(dk2::Area4s a1, dk2::Area4s a2) {
    return {  // widescreen camera switch: left click forward, right click back
        BT_CClickButton, 3, 0, CButton_handleLeftClick_WidescreenFovSwitch, CButton_handleRightClick_WidescreenFovSwitch, 0, 0, 0x00000000, 0x00000000, 0,
        a1.x, a1.y, (uint16_t) a1.w, (uint16_t) a1.h,
        (uint16_t) a2.x, (uint16_t) a2.y, (uint16_t) a2.w, (uint16_t) a2.h,
        0, NULL, CClickButton_render_WidescreenFovSwitch, 0x00000000, 0, 0x00000000, 0x00000001, 21
    };
}

void __cdecl dk2::MyCameraState_575A00(MyCameraState *state, float a2) {
    CBridge *bridge = CPCEngineInterface_instance_start.pCBridge;
    int width = (int) MyWindow_instance.dwWidth;
    int height = (int) MyWindow_instance.dwHeight;
    if (!*o_widescreenFov || bridge == nullptr || width <= 0 || height <= 0) {
        c_original_MyCameraState_575A00(state, a2);
        return;
    }
    int refHeight = *o_widescreenFovReferenceHeight;
    if (refHeight <= 0) refHeight = 600;
    MyCameraState adjusted = *state;  // don't change the caller's copy
    CCamera &camera = bridge->camera;
    if (camera._mode == c_cameraModePath) {
        adjustPathCamera(adjusted, width, height);
    } else if (camera._mode != c_cameraModeMap) {
        adjustNormalCamera(adjusted, camera, width, height, refHeight);
    }
    c_original_MyCameraState_575A00(&adjusted, a2);
}
