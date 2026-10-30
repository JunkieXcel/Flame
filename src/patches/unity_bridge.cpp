//
// Created by Alex on 29.10.2026.
//

#include "unity_bridge.h"
#include <Windows.h>
#include <cstring>
#include <new>
#include "dk2/CBridge.h"
#include "dk2/CCamera.h"
#include "dk2/CWorld.h"
#include "dk2/utils/MyCameraState.h"
#include "dk2_globals.h"
#include "patches/logging.h"
#include <tools/flame_config.h>

flame_config::define_flame_option<bool> o_unityBridge(
    "flame:unity-bridge", flame_config::OG_Config,
    "Write the 3D camera into the shared memory \"Local\\FlameUnityBridge\" every frame,\n"
    "for the Unity project in D:\\Dev\\DK2-Unity to follow",
    false
);

namespace {

    HANDLE g_mapping = NULL;
    patch::unity_bridge::Header *g_header = nullptr;
    bool g_failed = false;

    bool open() {
        using namespace patch::unity_bridge;
        g_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, c_mappingSize, c_mappingName);
        if (!g_mapping) {
            patch::log::err("unity-bridge: CreateFileMapping failed: %d", GetLastError());
            return false;
        }
        void *view = MapViewOfFile(g_mapping, FILE_MAP_WRITE, 0, 0, c_mappingSize);
        if (!view) {
            patch::log::err("unity-bridge: MapViewOfFile failed: %d", GetLastError());
            CloseHandle(g_mapping);
            g_mapping = NULL;
            return false;
        }
        // a new mapping is zeroed. start the sequence even, then publish magic and version
        g_header = new (view) Header{};
        g_header->version = c_version;
        memcpy(g_header->magic, c_magic, sizeof(c_magic));
        g_header->sequence.store(0, std::memory_order_release);
        patch::log::dbg("unity-bridge: writing the camera to %s", c_mappingName);
        return true;
    }

}

void patch::unity_bridge::write_frame() {
    if (!*o_unityBridge || g_failed) return;
    if (!g_header && !open()) {
        g_failed = true;
        return;
    }
    Header &h = *g_header;
    uint32_t seq = h.sequence.load(std::memory_order_relaxed);
    h.sequence.store(seq + 1, std::memory_order_relaxed);  // odd: writing
    // keeps the data writes below after the odd sequence
    std::atomic_thread_fence(std::memory_order_release);

    dk2::MyCameraState &state = dk2::g_camState;
    dk2::CCamera &camera = dk2::CBridge_instance.camera;
    bool mainMenu3d = dk2::MyResources_instance.gameCfg.useFe3d != 0;
    bool inLevel = !mainMenu3d && dk2::CGameComponent_instance.gameSession.pPlayer;
    h.frame++;
    h.timeMs = GetTickCount();
    h.flags = F_CameraValid | (inLevel ? F_InLevel : 0) | (mainMenu3d ? F_MainMenu3d : 0);
    h.pos[0] = state.v3f.x;
    h.pos[1] = state.v3f.y;
    h.pos[2] = state.v3f.z;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) h.rot[r * 3 + c] = state.m.m[r][c];
    }
    h.focal = state.ww240;
    h.viewWidth = dk2::g_sc_renderWidth;
    h.viewHeight = dk2::g_sc_renderHeight;
    h.worldPos[0] = camera.arrx3__worldCameraPos.x;
    h.worldPos[1] = camera.arrx3__worldCameraPos.y;
    h.worldPos[2] = camera.arrx3__worldCameraPos.z;
    h.zoom = camera.curZoomLevel;
    h.direction = camera.direction;
    h.mapWidth = inLevel && dk2::g_pCWorld ? dk2::g_pCWorld->cmap.width : 0;
    h.mapHeight = inLevel && dk2::g_pCWorld ? dk2::g_pCWorld->cmap.height : 0;

    h.sequence.store(seq + 2, std::memory_order_release);  // even: done, publishes the writes above
}
