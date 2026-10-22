//
// Created by Alex on 22.10.2026.
//

#include "dk2/CBridge.h"
#include "dk2/CCamera.h"
#include "dk2/CPCEngineInterface.h"
#include "dk2/MyWindow.h"
#include "dk2/utils/MyCameraState.h"
#include "dk2_functions.h"
#include "dk2_globals.h"
#include <tools/flame_config.h>


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
