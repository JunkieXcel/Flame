//
// Created by Alex on 29.10.2026.
//

#include "unity_bridge.h"
#include <Windows.h>
#include <cstring>
#include <new>
#include <vector>
#include "dk2/CBridge.h"
#include "dk2/CCamera.h"
#include "dk2/CDefaultPlayerInterface.h"
#include "dk2/CWorld.h"
#include "dk2/world/map/MyMapElement.h"
#include "dk2/entities/data/MyTerrainDataObj.h"
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

    // what was last written to the block, to write the tiles again only when they change
    std::vector<patch::unity_bridge::Tile> g_writtenTiles;
    uint32_t g_writtenFlags[256] = {};
    int g_writtenWidth = 0;
    int g_writtenHeight = 0;
    // built every frame, then compared with the above
    std::vector<patch::unity_bridge::Tile> g_tiles;
    uint32_t g_flags[256] = {};

    // reads the map into g_tiles / g_flags. TS_Exported if the map looked whole and consistent
    patch::unity_bridge::TilesState readMap(bool inLevel, int &width, int &height) {
        using namespace patch::unity_bridge;
        width = height = 0;
        if (!inLevel) return TS_NotInLevel;
        dk2::CWorld *world = dk2::g_pCWorld;
        if (!world) return TS_NoWorld;
        dk2::CMap &map = world->cmap;
        if (!map.opened) return TS_MapNotOpened;
        if (map.world != world) return TS_WorldMismatch;
        // read once: the checks and the copy below use the same values
        int w = map.width;
        int h = map.height;
        dk2::MyMapElement *elements = map.mapElements;
        if (w <= 0 || h <= 0 || w > c_maxMapSide || h > c_maxMapSide) return TS_BadSize;
        if (!elements) return TS_NoTiles;

        g_tiles.resize((size_t) w * h);
        for (int i = 0; i < w * h; ++i) {
            const dk2::MyMapElement &e = elements[i];
            Tile &t = g_tiles[i];
            t.terrain = e.arr6DA4A8_idx;
            t.pad = 0;
            t.owner = (uint16_t) (e._playerIdFFF & 0xFFF);
            t.room = (uint16_t) (e._roomIdFFF & 0xFFF);
            t.pad2 = 0;
        }
        for (int i = 0; i < 256; ++i) {
            dk2::MyTerrainDataObj *terrain = dk2::g_MyTerrainDataObj_arr[i];
            g_flags[i] = terrain ? (uint32_t) terrain->_flags : 0;
        }
        width = w;
        height = h;
        return TS_Exported;
    }

    // called between the odd and even sequence, like the camera
    void writeTiles(patch::unity_bridge::Header &h, bool inLevel) {
        using namespace patch::unity_bridge;
        int width, height;
        TilesState state = readMap(inLevel, width, height);
        h.tilesState = state;
        if (state != TS_Exported && state != TS_NotInLevel) {
            // something looked inconsistent: keep the last whole map rather than export part of one
            h.tilesSkipped++;
            return;
        }
        size_t count = (size_t) width * height;
        bool same = width == g_writtenWidth && height == g_writtenHeight
                    && memcmp(g_flags, g_writtenFlags, sizeof(g_flags)) == 0
                    && (count == 0 || memcmp(g_tiles.data(), g_writtenTiles.data(), count * sizeof(Tile)) == 0);
        if (same) return;

        auto *base = (uint8_t *) &h;
        if (count) {
            memcpy(base + c_tilesOffset, g_tiles.data(), count * sizeof(Tile));
            memcpy(base + c_terrainFlagsOffset, g_flags, sizeof(g_flags));
            g_writtenTiles.assign(g_tiles.begin(), g_tiles.begin() + (ptrdiff_t) count);
            memcpy(g_writtenFlags, g_flags, sizeof(g_flags));
        } else {
            g_writtenTiles.clear();
        }
        g_writtenWidth = width;
        g_writtenHeight = height;
        h.mapWidth = width;
        h.mapHeight = height;
        h.tilesVersion++;
    }

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
    writeTiles(h, inLevel);
    dk2::CDefaultPlayerInterface *player = inLevel ? dk2::CGameComponent_instance.gameSession.pPlayer : nullptr;
    h.handValid = player ? 1 : 0;
    if (player) {
        dk2::ObjUnderHand &hand = player->_underHand;
        h.handX = (int32_t) hand.x;
        h.handY = hand.y;
        h.handFineX = hand.x_if12;
        h.handFineY = hand.y_if12;
        h.handType = (uint32_t) hand.type;
    }

    h.sequence.store(seq + 2, std::memory_order_release);  // even: done, publishes the writes above
}
