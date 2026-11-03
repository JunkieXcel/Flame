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
    constexpr uint32_t c_mappingSize = 8 * 1024 * 1024;  // room for tiles and creatures later
    constexpr char c_magic[4] = {'F', 'L', 'U', 'B'};
    constexpr uint32_t c_version = 13;

    // fixed places in the block after the header
    constexpr uint32_t c_terrainFlagsOffset = 4096;  // uint32_t[256]: MyTerrainDataObj::_flags of each terrain type
    constexpr uint32_t c_terrainLightsOffset = 5120;  // TerrainLight[256]: the light each terrain type gives a tile
    constexpr uint32_t c_tilesOffset = 8192;  // Tile[mapHeight][mapWidth], row by row (y outer, x inner)
    constexpr int c_maxMapSide = 512;
    constexpr uint32_t c_lightsOffset = 3 * 1024 * 1024;  // Light[lightsCount]
    constexpr uint32_t c_maxLights = 1024;
    constexpr uint32_t c_maxListWalk = 65536;  // a longer light list is taken as broken
    constexpr uint32_t c_objectsOffset = 3 * 1024 * 1024 + 512 * 1024;  // Object[objectsCount]
    constexpr uint32_t c_maxObjects = 4096;  // the size of the game's thing table, so every object fits
    constexpr uint32_t c_bendOffset = 4 * 1024 * 1024;  // TileBend[mapHeight][mapWidth], row by row like the tiles
    constexpr uint32_t c_maxBendTiles = 32768;  // a bigger map gets no TileBend table (BS_MapTooBig)

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

    // why the lights of this frame were or were not exported
    enum LightsState : uint32_t {
        LS_Exported = 0,  // lightsCount lights of the list (all of them unless lightsInList > c_maxLights)
        LS_NotInLevel = 1,  // no level: lightsCount = 0
        LS_TooLong = 2,  // the list didn't end within c_maxListWalk steps (broken or looped): lightsCount = 0
    };

    // why the objects of this frame were or were not exported
    enum ObjectsState : uint32_t {
        OS_Exported = 0,  // objectsCount objects: every CObject in the game's thing table
        OS_NotInLevel = 1,  // no level: objectsCount = 0
    };

    // why the TileBend table of this frame is what it is
    enum BendState : uint32_t {
        BS_Exported = 0,  // the table is the current map's (tiles the engine hasn't built yet are not known)
        BS_NotInLevel = 1,  // no level: bendTiles = 0
        BS_NoMap = 2,  // no tiles exported yet: bendTiles = 0
        BS_MapTooBig = 3,  // mapWidth * mapHeight > c_maxBendTiles: bendTiles = 0
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
        // the 3D view in screen pixels, from g_camState. MyCameraState_575A00 sets trg to the midpoint
        // of leftRight and topBottom, so trg is the picture centre the projection uses
        float viewLeftRight[2];  // g_camState.leftRight
        float viewTopBottom[2];  // g_camState.topBottom
        float viewCentre[2];  // g_camState.trg
        int32_t renderLeft;  // g_sc_renderLeft: where the 3D view starts on the screen
        int32_t renderTop;  // g_sc_renderTop
        int32_t sceneWidth;  // g_sc_sceneWidth: the whole screen
        int32_t sceneHeight;  // g_sc_sceneHeight
        // the game's lights: CBridge::light_list, copied every frame (they flicker and pulse)
        uint32_t lightsCount;  // Light records at c_lightsOffset
        uint32_t lightsInList;  // lights in the game's list, may be more than lightsCount
        uint32_t lightsState;  // LightsState of this frame
        // g_vec_760A98: the grey light every static mesh, sprite and height field starts from, 0..255 scale.
        // sub_5747E0 sets it to Ambient Light option / 4096 (clamped 0..0.5) * 255; 0 until then
        float ambient[3];
        // words at 6F29F0/2/4 (no name yet): sub_441CD0 uses them instead of the terrain's colour
        // for tiles marked TL_OtherColour
        int16_t tileLightOtherColour[3];
        uint16_t pad3;
        // the engine's bent tile shapes: TileBend records at c_bendOffset
        uint32_t bendVersion;  // +1 whenever the TileBend table changes
        uint32_t bendTiles;  // tiles whose TileBend is known
        uint32_t bendState;  // BendState of this frame
        uint32_t bendClamped;  // moves of this frame that didn't fit an int16 and were clamped (expected 0)
        // the game's objects (beds, chickens, gold ...): Object records at c_objectsOffset, copied every frame
        uint32_t objectsCount;  // Object records: the things in sceneObjects whose class is CObject (by vtable)
        uint32_t objectsTyped;  // things whose CThing::fE_type is 2 (expected = objectsCount: a check that 2 = object)
        uint32_t objectsState;  // ObjectsState of this frame
        uint32_t objectsMismatch;  // CObjects whose fE_type is not 2 (expected 0)
    };

    // Tile::light: the one steady light the engine gives a tile (CBridge::sub_441CD0, a temporary
    // scene effect passed to maybeAddLight, never in CBridge::light_list). 0 = none
    enum TileLight : uint8_t {
        TL_TerrainMask = 0x7F,  // index of the terrain type whose TerrainLight it is, chosen by
                                // MyMapElement::sub_452DC0 (e.g. 38 "Claim Scan" on some rock)
        TL_OtherColour = 0x80,  // colour from Header::tileLightOtherColour instead of the terrain's
    };

    // MyTerrainDataObj light fields
    struct TerrainLight {
        int16_t colour[3];  // f1AF/f1B1/f1B3, 0..511 like the CLight colours
        int16_t pad;
        int32_t height;  // f1BF: the light's height above the tile, assumed 1/4096 of a tile
    };

    // one CLight. units and colour scale assumed, check in game
    struct Light {
        int32_t pos[3];  // CBridgeThing::fD2_pos, assumed 1/4096 of a tile like the camera
        int16_t colour[3];  // CLight +0x111/113/115: current colour, after flicker or pulse
        int16_t baseColour[3];  // CLight +0x11F/121/123: the colour CLight::init was given
        uint32_t radius;  // CLight +0x117, CLight::init caps it at 0x2780
        uint32_t flags;  // CLight +0x134, 0x4 = pulsing (CLight's tick animates the colour); other bits unknown
        uint8_t strength;  // CLight +0x145: the engine multiplies the light by strength / 128 (maybeAddLight)
        // v13: 1 = a moving light: CBridgeThing +0xE4 (header name field_E0) != 0, so CBridge::createMeshes gives it to
        // the engine as a moving light (createMesh_light); 0 = steady (CBridge::fun_43C310). Objects light moving and
        // flickering lights per vertex by facing, steady ones evenly (CEngineDynamicMesh, read 2026-11-03)
        uint8_t moving;
        uint8_t pad[2];
    };

    // Tile::wallPanels: which of a room tile's full-width wall panels use the room's second panel piece
    // (fC1[8], e.g. the Hatchery's decorated "Hatchery1") instead of the first (fC1[7], plain plaster).
    // CBridge::sub_444F10 decides it from CRoom::roomWallAlignmentMapX/Y (+0x92/+0x96, -1 = never):
    enum WallPanels : uint8_t {
        WP_NorthSouth = 0x1,  // the N and S walls: alignX >= 0 and (x + alignX) even
        WP_EastWest = 0x2,    // the E and W walls: alignY >= 0 and (y + alignY) odd
    };

    // How the 3D engine bends a tile (read 2026-11-02). Every static mesh vertex goes through
    // Obj58EF60::sub_58F030 (Obj57AD20::constructor): a blend of the 8 nearest control points of a grid every half
    // tile, at 5 heights (engine z -2.5, -2, -1.5, -1, -0.5; up is -z). Each tile owns 2 x 2 columns of 5 points at
    // engine (x + i/2, y + j/2, -2.5 + k/2), i, j = 0..1, k = 0..4, which CPCEngineInterface::sub_5A3590 moves a little
    // (from the map position and the neighbours) when the engine first builds the tile. They live in the engine's tile
    // cache (g_NewObj571B3B_hashTable, NewObj571B3B::ptr3C, 20 Vec3f, point index (2i + j) * 5 + k).
    // TileBend holds each point's move away from that regular place, in 1/4096 of a tile
    struct TileBend {
        int16_t move[20][3];  // [(2i + j) * 5 + k][engine x, y, z]
        uint8_t known;  // 1 once the engine has built the tile's points (kept until the level ends or the map size changes)
        uint8_t pad[7];
    };

    // one CObject from the game's thing table (sceneObjects / sceneObjectsPresent). Read 2026-11-03, units assumed
    struct Object {
        int32_t pos[3];  // CThing::f16_pos, assumed 1/4096 of a tile with up = +z like a CLight's position
        uint16_t tagId;  // CTag::f0_tagId, its slot in the thing table
        uint16_t owner;  // CThing::f24_playerId
        uint16_t direction;  // CPhysicalThing::fF0_direction, assumed 0..2047 for a full turn (unchecked)
        uint8_t typeId;  // CObject::typeId: the object type (Objects.kwd), names checked later
        uint8_t animationId;  // CRenderInfo::animationId
        uint32_t flags;  // CObject::flags, meaning unknown
        uint32_t stateId;  // CObject::stateId
        int32_t currentFrame;  // CRenderInfo::currentFrame
        uint32_t mapWhoType;  // CThing::f12_mapWhoType, meaning unknown (maybe: on the map / in a hand)
        int16_t floorHeight;  // CThing::f22_floorHeight
        uint16_t pad;
    };

    struct Tile {
        uint8_t terrain;  // MyMapElement::arr6DA4A8_idx, the terrain type (index into g_MyTerrainDataObj_arr)
        uint8_t light;  // TileLight
        uint16_t owner;  // MyMapElement::_playerIdFFF & 0xFFF
        uint16_t room;  // MyMapElement::_roomIdFFF & 0xFFF, 0 = no room
        uint8_t wallPanels;  // WallPanels, 0 for tiles that aren't in a room
        uint8_t pad2;
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
    static_assert(offsetof(Header, viewLeftRight) == 152);
    static_assert(offsetof(Header, viewTopBottom) == 160);
    static_assert(offsetof(Header, viewCentre) == 168);
    static_assert(offsetof(Header, renderLeft) == 176);
    static_assert(offsetof(Header, renderTop) == 180);
    static_assert(offsetof(Header, sceneWidth) == 184);
    static_assert(offsetof(Header, sceneHeight) == 188);
    static_assert(offsetof(Header, lightsCount) == 192);
    static_assert(offsetof(Header, lightsInList) == 196);
    static_assert(offsetof(Header, lightsState) == 200);
    static_assert(offsetof(Header, ambient) == 204);
    static_assert(offsetof(Header, tileLightOtherColour) == 216);
    static_assert(offsetof(Header, bendVersion) == 224);
    static_assert(offsetof(Header, bendTiles) == 228);
    static_assert(offsetof(Header, bendState) == 232);
    static_assert(offsetof(Header, bendClamped) == 236);
    static_assert(offsetof(Header, objectsCount) == 240);
    static_assert(offsetof(Header, objectsTyped) == 244);
    static_assert(offsetof(Header, objectsState) == 248);
    static_assert(offsetof(Header, objectsMismatch) == 252);
    static_assert(sizeof(Header) == 256);
    static_assert(offsetof(Object, tagId) == 12);
    static_assert(offsetof(Object, owner) == 14);
    static_assert(offsetof(Object, direction) == 16);
    static_assert(offsetof(Object, typeId) == 18);
    static_assert(offsetof(Object, animationId) == 19);
    static_assert(offsetof(Object, flags) == 20);
    static_assert(offsetof(Object, stateId) == 24);
    static_assert(offsetof(Object, currentFrame) == 28);
    static_assert(offsetof(Object, mapWhoType) == 32);
    static_assert(offsetof(Object, floorHeight) == 36);
    static_assert(sizeof(Object) == 40);
    static_assert(sizeof(TileBend) == 128);
    static_assert(offsetof(TileBend, known) == 120);
    static_assert(sizeof(Tile) == 8);
    static_assert(offsetof(Tile, light) == 1);
    static_assert(offsetof(Tile, wallPanels) == 6);
    static_assert(offsetof(TerrainLight, height) == 8);
    static_assert(sizeof(TerrainLight) == 12);
    static_assert(offsetof(Light, colour) == 12);
    static_assert(offsetof(Light, baseColour) == 18);
    static_assert(offsetof(Light, radius) == 24);
    static_assert(offsetof(Light, flags) == 28);
    static_assert(offsetof(Light, strength) == 32);
    static_assert(offsetof(Light, moving) == 33);
    static_assert(sizeof(Light) == 36);
    static_assert(sizeof(Header) <= c_terrainFlagsOffset);
    static_assert(c_terrainFlagsOffset + 256 * sizeof(uint32_t) <= c_terrainLightsOffset);
    static_assert(c_terrainLightsOffset + 256 * sizeof(TerrainLight) <= c_tilesOffset);
    static_assert(c_tilesOffset + c_maxMapSide * c_maxMapSide * sizeof(Tile) <= c_lightsOffset);
    static_assert(c_lightsOffset + c_maxLights * sizeof(Light) <= c_objectsOffset);
    static_assert(c_objectsOffset + c_maxObjects * sizeof(Object) <= c_bendOffset);
    static_assert(c_bendOffset + c_maxBendTiles * sizeof(TileBend) <= c_mappingSize);

    // called once per frame from engine_drawScene. does nothing unless flame:unity-bridge is set
    void write_frame();

}

#endif //FLAME_UNITY_BRIDGE_H
