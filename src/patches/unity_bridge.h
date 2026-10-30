//
// Created by Alex on 29.10.2026.
//

#ifndef FLAME_UNITY_BRIDGE_H
#define FLAME_UNITY_BRIDGE_H

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace patch::unity_bridge {

    // named shared memory read by the Unity project (D:\Dev\DK2-Unity, FlameBridge.cs).
    // the layout is pinned on both sides: change it together with FlameBridge.cs and bump c_version
    constexpr const char *c_mappingName = "Local\\FlameUnityBridge";
    constexpr uint32_t c_mappingSize = 4 * 1024 * 1024;  // room for tiles and creatures later
    constexpr char c_magic[4] = {'F', 'L', 'U', 'B'};
    constexpr uint32_t c_version = 2;

    enum Flags : uint32_t {
        F_CameraValid = 1 << 0,
        F_InLevel = 1 << 1,  // a level is played
        F_MainMenu3d = 1 << 2,  // the 3D main menu scene
    };

#pragma pack(push, 1)
    struct Header {
        char magic[4];
        uint32_t version;
        // odd while a frame is written. a reader copies the data between two equal, even reads
        std::atomic<uint32_t> sequence;
        uint32_t frame;  // +1 for every drawn frame
        uint32_t timeMs;  // GetTickCount when the frame was written
        uint32_t flags;  // Flags
        float pos[3];  // render camera position, g_camState.v3f
        float rot[9];  // render camera matrix, g_camState.m, row by row. column 2 is the view direction
        float focal;  // focal length in pixels, g_camState.ww240
        int32_t viewWidth;  // 3D view size in pixels
        int32_t viewHeight;
        int32_t worldPos[3];  // logical camera position, CCamera::arrx3__worldCameraPos
        uint32_t zoom;  // CCamera::curZoomLevel
        uint16_t direction;  // CCamera::direction
        uint16_t pad;
        int32_t mapWidth;  // in tiles, 0 outside a level
        int32_t mapHeight;
    };
#pragma pack(pop)

    static_assert(sizeof(std::atomic<uint32_t>) == 4 && std::atomic<uint32_t>::is_always_lock_free);
    static_assert(offsetof(Header, magic) == 0);
    static_assert(offsetof(Header, version) == 4);
    static_assert(offsetof(Header, sequence) == 8);
    static_assert(offsetof(Header, frame) == 12);
    static_assert(offsetof(Header, timeMs) == 16);
    static_assert(offsetof(Header, flags) == 20);
    static_assert(offsetof(Header, pos) == 24);
    static_assert(offsetof(Header, rot) == 36);
    static_assert(offsetof(Header, focal) == 72);
    static_assert(offsetof(Header, viewWidth) == 76);
    static_assert(offsetof(Header, viewHeight) == 80);
    static_assert(offsetof(Header, worldPos) == 84);
    static_assert(offsetof(Header, zoom) == 96);
    static_assert(offsetof(Header, direction) == 100);
    static_assert(offsetof(Header, pad) == 102);
    static_assert(offsetof(Header, mapWidth) == 104);
    static_assert(offsetof(Header, mapHeight) == 108);
    static_assert(sizeof(Header) == 112);

    // called once per frame from engine_drawScene. does nothing unless flame:unity-bridge is set
    void write_frame();

}

#endif //FLAME_UNITY_BRIDGE_H
