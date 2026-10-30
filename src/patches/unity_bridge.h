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
    constexpr uint32_t c_version = 4;

    // fixed places in the block after the header
    constexpr uint32_t c_terrainFlagsOffset = 4096;  // uint32_t[256]: MyTerrainDataObj::_flags of each terrain type
    constexpr uint32_t c_tilesOffset = 8192;  // Tile[mapHeight][mapWidth], row by row (y outer, x inner)
    constexpr int c_maxMapSide = 512;

    enum Flags : uint32_t {
        F_CameraValid = 1 << 0,
        F_InLevel = 1 << 1,  // a level is played
        F_MainMenu3d = 1 << 2,  // the 3D main menu scene
    };

    // why the tiles of this frame were or were not exported
    enum TilesState : uint32_t {
        TS_Exported = 0,  // the tiles in the block are the current map
        TS_NotInLevel = 1,  // no level: the block holds no map (mapWidth = mapHeight = 0)
        // the states below skip the frame: the block keeps the last consistent map
        TS_NoWorld = 2,  // g_pCWorld is null
        TS_MapNotOpened = 3,  // CMap::opened is 0
        TS_WorldMismatch = 4,  // CMap::world doesn't point back to g_pCWorld
        TS_BadSize = 5,  // map width or height out of range
        TS_NoTiles = 6,  // CMap::mapElements is null
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
        int32_t mapWidth;  // size of the exported tile array, 0 when there is none
        int32_t mapHeight;
        uint32_t tilesVersion;  // +1 whenever the exported tiles or terrain flags change
        uint32_t tilesState;  // TilesState of this frame
        uint32_t tilesSkipped;  // frames skipped because the map looked inconsistent, since the game started
        uint32_t pad2;
        // what is under the hand cursor: CDefaultPlayerInterface::_underHand
        uint32_t handValid;  // 1 if the fields below were read this frame (in a level, player interface present)
        int32_t handX;  // ObjUnderHand::x, the map tile (assumed: the game passes it to tile functions)
        int32_t handY;  // ObjUnderHand::y
        int32_t handFineX;  // ObjUnderHand::x_if12, assumed 1/4096 of a tile
        int32_t handFineY;  // ObjUnderHand::y_if12
        uint32_t handType;  // ObjUnderHand::type, meaning unknown
    };

    struct Tile {
        uint8_t terrain;  // MyMapElement::arr6DA4A8_idx, the terrain type (index into g_MyTerrainDataObj_arr)
        uint8_t pad;
        uint16_t owner;  // MyMapElement::_playerIdFFF & 0xFFF
        uint16_t room;  // MyMapElement::_roomIdFFF & 0xFFF, 0 = no room
        uint16_t pad2;
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
    static_assert(offsetof(Header, tilesVersion) == 112);
    static_assert(offsetof(Header, tilesState) == 116);
    static_assert(offsetof(Header, tilesSkipped) == 120);
    static_assert(offsetof(Header, pad2) == 124);
    static_assert(offsetof(Header, handValid) == 128);
    static_assert(offsetof(Header, handX) == 132);
    static_assert(offsetof(Header, handY) == 136);
    static_assert(offsetof(Header, handFineX) == 140);
    static_assert(offsetof(Header, handFineY) == 144);
    static_assert(offsetof(Header, handType) == 148);
    static_assert(sizeof(Header) == 152);
    static_assert(sizeof(Tile) == 8);
    static_assert(sizeof(Header) <= c_terrainFlagsOffset);
    static_assert(c_terrainFlagsOffset + 256 * sizeof(uint32_t) <= c_tilesOffset);
    static_assert(c_tilesOffset + c_maxMapSide * c_maxMapSide * sizeof(Tile) <= c_mappingSize);

    // called once per frame from engine_drawScene. does nothing unless flame:unity-bridge is set
    void write_frame();

}

#endif //FLAME_UNITY_BRIDGE_H
