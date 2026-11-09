//
// Created by Alex on 29.10.2026.
//

#include "unity_bridge.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>
#include "dk2/CBridge.h"
#include "dk2/CCamera.h"
#include "dk2/CDefaultPlayerInterface.h"
#include "dk2/CLight.h"
#include "dk2/CWorld.h"
#include "dk2/NewObj571B3B.h"
#include "dk2/engine/CWorldEntry.h"
#include "dk2/entities/CObject.h"
#include "dk2/entities/CRoom.h"
#include "dk2/world/map/MyMapElement.h"
#include "dk2/entities/data/MyTerrainDataObj.h"
#include "dk2/utils/MyCameraState.h"
#include "dk2_globals.h"
#include "dk2_functions.h"
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
    patch::unity_bridge::TerrainLight g_writtenTerrainLights[256] = {};
    int g_writtenWidth = 0;
    int g_writtenHeight = 0;
    // built every frame, then compared with the above
    std::vector<patch::unity_bridge::Tile> g_tiles;
    uint32_t g_flags[256] = {};
    patch::unity_bridge::TerrainLight g_terrainLights[256] = {};

    // the bent tile shapes, kept between frames: the engine's cache only holds the tiles drawn in the last frame
    // (NewObj571B3B_sub_576010 frees the others every frame), so a tile keeps its last known points here until the
    // engine builds them again. Reset when the level ends or the map size changes
    std::vector<patch::unity_bridge::TileBend> g_bend;
    int g_bendWidth = 0;
    int g_bendHeight = 0;
    bool g_bendDirty = false;  // g_bend differs from the block

    // the tile's steady light, decided the way CBridge::sub_441CD0 does
    uint8_t tileLight(dk2::MyMapElement &e, int x, int y) {
        using namespace patch::unity_bridge;
        dk2::MyTerrainDataObj *terrain = nullptr;
        if (!e.sub_452DC0(x, y, &terrain) || !terrain) return 0;
        // sub_452DC0 may hand back another terrain than the tile's own (Claim Scan): find its index
        int idx = e.arr6DA4A8_idx;
        if (dk2::g_MyTerrainDataObj_arr[idx] != terrain) {
            idx = -1;
            for (int i = 0; i < 128; ++i) {
                if (dk2::g_MyTerrainDataObj_arr[i] == terrain) { idx = i; break; }
            }
            if (idx < 0) return 0;
        }
        uint8_t light = (uint8_t) (idx & TL_TerrainMask);
        // sub_441CD0: map element byte +9 bit 0x20 clear, +0xB bit 0x40 set, terrain f185 == 0
        bool bit9 = (((uint16_t) e._playerIdFFF >> 8) & 0x20) != 0;
        bool bitB = (e.flags_and_burnLevel & 0x40) != 0;
        if (!bit9 && bitB && terrain->f185 == 0) light |= TL_OtherColour;
        return light;
    }

    // the index of a terrain in g_MyTerrainDataObj_arr, or -1
    int terrainIndex(dk2::MyTerrainDataObj *terrain) {
        if (!terrain) return -1;
        for (int i = 0; i < 128; ++i) {
            if (dk2::g_MyTerrainDataObj_arr[i] == terrain) return i;
        }
        return -1;
    }

    // the terrain DK2 draws the tile as, decided the way CBridge::sub_43E320 does (src/dk2/CBridge.cpp): a tile not
    // yet seen (map element bit 0x2000 clear), outside a reveal area and with the camera not in mode 2/10/11 is drawn
    // as "Claim Scan" (38) if +0xB bit 0x40 is set and its own terrain lacks flag 0x1000000, else as "Fog Of War" (27).
    // fogTerrain / scanTerrain: the indices of those two terrains (-1 if unknown); cameraReveals: CCamera_sub_44DB70
    // whether DK2 draws the tile normally (fun_43C310 0x43CA72..0x43CA9F, sub_43E320): seen (map element bit 0x2000),
    // or the camera shows everything (CCamera_sub_44DB70), or the tile is in a temporary reveal area
    bool shownNormally(dk2::MyMapElement &e, int x, int y, bool cameraReveals) {
        if ((e._playerIdFFF & 0x2000) != 0 || cameraReveals) return true;
        // CBridge vtable +0xDC (v_fDC) = 441B00, called directly
        return dk2::CBridge_instance.fun_441B00(x, y) != 0;
    }

    uint8_t drawnTerrain(dk2::MyMapElement &e, int x, int y, int fogTerrain, int scanTerrain, bool cameraReveals) {
        uint8_t own = e.arr6DA4A8_idx;
        if (shownNormally(e, x, y, cameraReveals)) return own;
        if ((e.flags_and_burnLevel & 0x40) != 0) {
            dk2::MyTerrainDataObj *terrain = dk2::g_MyTerrainDataObj_arr[own];
            if (terrain && (terrain->_flags & 0x1000000) != 0) return own;
            return scanTerrain >= 0 ? (uint8_t) scanTerrain : own;
        }
        return fogTerrain >= 0 ? (uint8_t) fogTerrain : own;
    }

    // which full wall panels of a room tile use the second panel piece, the way CBridge::sub_444F10 picks them
    uint8_t wallPanels(dk2::MyMapElement &e, int x, int y) {
        using namespace patch::unity_bridge;
        if ((e._roomIdFFF & 0xFFF) == 0) return 0;
        dk2::CRoom *room = e.getRoom();  // a lookup in the game's object table, may be null
        if (!room) return 0;
        uint8_t panels = 0;
        int alignX = room->roomWallAlignmentMapX;
        int alignY = room->roomWallAlignmentMapY;
        if (alignX >= 0 && ((x + alignX) & 1) == 0) panels |= WP_NorthSouth;
        if (alignY >= 0 && ((y + alignY) & 1) == 1) panels |= WP_EastWest;
        return panels;
    }

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

        // the two terrains unseen tiles are drawn as, looked up the way sub_43E320 does
        int fogTerrain = terrainIndex(world->v_f70_508DD0_getTerrainDataObj(27));
        int scanTerrain = terrainIndex(world->v_f70_508DD0_getTerrainDataObj(38));
        bool cameraReveals = dk2::CCamera_sub_44DB70(dk2::CBridge_instance.camera._mode) != 0;

        g_tiles.resize((size_t) w * h);
        for (int i = 0; i < w * h; ++i) {
            dk2::MyMapElement &e = elements[i];
            Tile &t = g_tiles[i];
            t.terrain = e.arr6DA4A8_idx;
            t.light = tileLight(e, i % w, i / w);
            t.owner = (uint16_t) (e._playerIdFFF & 0xFFF);
            t.room = (uint16_t) (e._roomIdFFF & 0xFFF);
            t.wallPanels = wallPanels(e, i % w, i / w);
            t.drawnTerrain = drawnTerrain(e, i % w, i / w, fogTerrain, scanTerrain, cameraReveals);
            // CBridge::fun_43C310 (0x43CB44) skips sub_441CD0, the tile light, for a tile that is neither seen, revealed
            // nor shown by the camera and has no +0xB bit 0x40 - exactly the tiles drawn as "Fog Of War"
            if (fogTerrain >= 0 && t.drawnTerrain == fogTerrain && t.terrain != fogTerrain) t.light = 0;
        }
        for (int i = 0; i < 256; ++i) {
            dk2::MyTerrainDataObj *terrain = dk2::g_MyTerrainDataObj_arr[i];
            g_flags[i] = terrain ? (uint32_t) terrain->_flags : 0;
            TerrainLight &l = g_terrainLights[i];
            l = {};
            if (terrain) {
                l.colour[0] = terrain->f1AF;
                l.colour[1] = terrain->f1B1;
                l.colour[2] = terrain->f1B3;
                l.height = terrain->f1BF;
            }
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
                    && memcmp(g_terrainLights, g_writtenTerrainLights, sizeof(g_terrainLights)) == 0
                    && (count == 0 || memcmp(g_tiles.data(), g_writtenTiles.data(), count * sizeof(Tile)) == 0);
        if (same) return;

        auto *base = (uint8_t *) &h;
        if (count) {
            memcpy(base + c_tilesOffset, g_tiles.data(), count * sizeof(Tile));
            memcpy(base + c_terrainFlagsOffset, g_flags, sizeof(g_flags));
            memcpy(base + c_terrainLightsOffset, g_terrainLights, sizeof(g_terrainLights));
            g_writtenTiles.assign(g_tiles.begin(), g_tiles.begin() + (ptrdiff_t) count);
            memcpy(g_writtenFlags, g_flags, sizeof(g_flags));
            memcpy(g_writtenTerrainLights, g_terrainLights, sizeof(g_terrainLights));
        } else {
            g_writtenTiles.clear();
        }
        g_writtenWidth = width;
        g_writtenHeight = height;
        h.mapWidth = width;
        h.mapHeight = height;
        h.tilesVersion++;
    }

    // the moves of one tile's 20 control points away from their regular places. false if the engine hasn't built them
    bool readBend(dk2::NewObj571B3B &o, int x, int y, patch::unity_bridge::TileBend &out, uint32_t &clamped) {
        if (!o.ptr3C) return false;
        auto *p = (const float *) o.ptr3C;  // Vec3f[20]
        out = {};
        out.known = 1;
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                for (int k = 0; k < 5; ++k) {
                    int point = (2 * i + j) * 5 + k;
                    float regular[3] = {(float) x + 0.5f * (float) i, (float) y + 0.5f * (float) j, -2.5f + 0.5f * (float) k};
                    for (int c = 0; c < 3; ++c) {
                        float move = std::round((p[point * 3 + c] - regular[c]) * 4096.0f);
                        if (!(move >= -32768.0f && move <= 32767.0f)) {  // also catches NaN
                            clamped++;
                            move = move > 0 ? 32767.0f : -32768.0f;
                        }
                        out.move[point][c] = (int16_t) move;
                    }
                }
            }
        }
        return true;
    }

    // copies the bent tile shapes from the engine's tile cache. called between the odd and even sequence, after
    // writeTiles (it uses the exported map size)
    void writeBend(patch::unity_bridge::Header &h, bool inLevel) {
        using namespace patch::unity_bridge;
        h.bendClamped = 0;
        int w = g_writtenWidth;
        int hh = g_writtenHeight;
        BendState state = !inLevel ? BS_NotInLevel
                        : (w <= 0 || hh <= 0) ? BS_NoMap
                        : (size_t) w * hh > c_maxBendTiles ? BS_MapTooBig
                        : BS_Exported;
        h.bendState = state;
        if (state != BS_Exported) {
            if (!g_bend.empty() || h.bendTiles != 0) {
                g_bend.clear();
                g_bendWidth = g_bendHeight = 0;
                h.bendTiles = 0;
                h.bendVersion++;
            }
            return;
        }
        if (w != g_bendWidth || hh != g_bendHeight || g_bend.empty()) {
            g_bend.assign((size_t) w * hh, TileBend{});
            g_bendWidth = w;
            g_bendHeight = hh;
            g_bendDirty = true;
        }
        // the cache: 64 x 64 chains, [x & 63][y & 63] (NewObj571B3B_add), linked by +0x14. Every entry in it is
        // live: the engine unlinks an entry before freeing it (NewObj571B3B_sub_576010, NewObj571B3B_clear), and
        // sets ptr3C to 0 when it frees the points (sub_571DA0, freeSmthInLoc/Area). NewObj571B3B::f4 is not a
        // level: it is g_ddSceneSessionId, which onDdSceneInitialized adds 1 to every drawn frame
        uint32_t clamped = 0;
        uint32_t steps = 0;
        TileBend bend;
        // read by address: the header's [64][64] declaration doesn't link (the export is typed differently)
        auto *table = (dk2::NewObj571B3B *const *) 0x760B90;  // g_NewObj571B3B_hashTable
        for (int slot = 0; slot < 64 * 64; ++slot) {
            for (dk2::NewObj571B3B *o = table[slot]; o; o = o->prev) {
                if (++steps > c_maxListWalk) goto walked;  // a broken chain: keep what we have
                int x = o->a2;
                int y = o->a3;
                if (x < 0 || y < 0 || x >= w || y >= hh) continue;
                if (!readBend(*o, x, y, bend, clamped)) continue;
                TileBend &dst = g_bend[(size_t) y * w + x];
                if (memcmp(&dst, &bend, sizeof(TileBend)) != 0) {
                    dst = bend;
                    g_bendDirty = true;
                }
            }
        }
        walked:
        h.bendClamped = clamped;
        if (!g_bendDirty) return;
        memcpy((uint8_t *) &h + c_bendOffset, g_bend.data(), g_bend.size() * sizeof(TileBend));
        uint32_t known = 0;
        for (const TileBend &b : g_bend) known += b.known;
        h.bendTiles = known;
        h.bendVersion++;
        g_bendDirty = false;
    }

    // copies the game's light list. called between the odd and even sequence, like the camera
    // the "moving" field the engine checks at +0xE4 (CBridge::createMeshes 43CF7E, CBridge::fun_43C310 43C6DA)
    static_assert(offsetof(dk2::CBridgeThing, field_E0) == 0xE4);

    // which tile's effect list each scene effect is linked into: CBridge +0x326 is a grid (dword 1 = uint16_t heads,
    // 2 = width, 3 = height, read by sub_4435E0) of sceneEffects indices (& 0x1FFF), chained through CBridgeThing +6
    // (fun_43C310 0x43CAA4..0x43CB42). Value = y * width + x of the map, or absent
    std::unordered_map<const void *, int> g_effectTile;

    // fills g_effectTile; false if the grid or map doesn't look usable (then no light gets a tile)
    bool readEffectTiles(int &mapWidth, int &mapHeight) {
        g_effectTile.clear();
        mapWidth = mapHeight = 0;
        dk2::CWorld *world = dk2::g_pCWorld;
        if (!world || !world->cmap.opened || !world->cmap.mapElements) return false;
        int w = world->cmap.width, h = world->cmap.height;
        const uint32_t *grid = dk2::CBridge_instance.dword_arr_x4_objStart;
        auto *heads = (const uint16_t *) grid[1];
        if (!heads || (int) grid[2] != w || (int) grid[3] != h) return false;
        for (int i = 0; i < w * h; ++i) {
            uint16_t index = heads[i] & 0x1FFF;
            for (int steps = 0; index != 0 && steps < 4096; ++steps) {
                dk2::CBridgeThing *thing = dk2::sceneEffects[index];
                if (!thing) break;
                g_effectTile[thing] = i;
                index = (uint16_t) thing->field_2;  // +6: the next effect on the same tile
            }
        }
        mapWidth = w;
        mapHeight = h;
        return true;
    }

    void writeLights(patch::unity_bridge::Header &h, bool inLevel) {
        using namespace patch::unity_bridge;
        h.lightsCount = 0;
        h.lightsInList = 0;
        if (!inLevel) {
            h.lightsState = LS_NotInLevel;
            return;
        }
        int linkWidth = 0, linkHeight = 0;
        bool linked = readEffectTiles(linkWidth, linkHeight);
        dk2::MyMapElement *elements = linked ? dk2::g_pCWorld->cmap.mapElements : nullptr;
        bool cameraReveals = dk2::CCamera_sub_44DB70(dk2::CBridge_instance.camera._mode) != 0;
        auto *out = (Light *) ((uint8_t *) &h + c_lightsOffset);
        uint32_t count = 0;
        uint32_t inList = 0;
        for (dk2::CLight *light = dk2::CBridge_instance.light_list; light; light = light->f13c_next) {
            if (++inList > c_maxListWalk) {
                h.lightsState = LS_TooLong;
                return;
            }
            if (count >= c_maxLights) continue;  // keep counting the rest
            Light &l = out[count++];
            l.pos[0] = light->fD2_pos.x;
            l.pos[1] = light->fD2_pos.y;
            l.pos[2] = light->fD2_pos.z;
            l.colour[0] = light->word_111;
            l.colour[1] = light->word_113;
            l.colour[2] = light->word_115;
            l.baseColour[0] = light->word_11f;
            l.baseColour[1] = light->word_121;
            l.baseColour[2] = light->word_123;
            l.radius = (uint32_t) light->dword_117;
            l.flags = (uint32_t) light->dword_134;
            l.strength = (uint8_t) light->eos;  // a char in the header, but the engine reads it unsigned
            l.moving = light->field_E0 != 0 ? 1 : 0;  // the field at +0xE4 (the header's names are 4 lower)
            const dk2::CRenderInfo &ri = light->renderInfo;
            uint8_t meshFlags = 0;
            if (ri.pWorldEntry) {
                meshFlags |= LM_HasMesh;
                if (ri.pWorldEntry->u.weB_flags & 1) meshFlags |= LM_Flame;
            }
            if (ri._flags2 & 1) meshFlags |= LM_Drawn;
            l.linkTile[0] = l.linkTile[1] = -1;
            if (linked) {
                auto found = g_effectTile.find(static_cast<dk2::CBridgeThing *>(light));
                if (found != g_effectTile.end()) {
                    int x = found->second % linkWidth, y = found->second / linkWidth;
                    l.linkTile[0] = (int16_t) x;
                    l.linkTile[1] = (int16_t) y;
                    if (!shownNormally(elements[found->second], x, y, cameraReveals)) meshFlags |= LM_UnseenTile;
                }
            }
            l.meshFlags = meshFlags;
            l.direction = light->fDE_angle;
            l.meshOffset[0] = ri.f51;
            l.meshOffset[1] = ri.f53;
            l.meshOffset[2] = ri.f55;
        }
        h.lightsCount = count;
        h.lightsInList = inList;
        h.lightsState = LS_Exported;
    }

    // copies every object in the game's thing table. called between the odd and even sequence, like the camera
    void writeObjects(patch::unity_bridge::Header &h, bool inLevel) {
        using namespace patch::unity_bridge;
        h.objectsCount = 0;
        h.objectsTyped = 0;
        h.objectsMismatch = 0;
        if (!inLevel) {
            h.objectsState = OS_NotInLevel;
            return;
        }
        auto *out = (Object *) ((uint8_t *) &h + c_objectsOffset);
        uint32_t count = 0;
        uint32_t typed = 0;
        uint32_t mismatch = 0;
        // the table also holds players, rooms and other tags that aren't things: only CObjects are read,
        // recognised by their vtable (nothing derives from CObject). Slot 0 is never used
        for (int tag = 1; tag < 4096; ++tag) {
            if (!dk2::sceneObjectsPresent[tag]) continue;
            dk2::CTag *t = dk2::sceneObjects[tag];
            if (!t) continue;
            if (t->getVtbl() != dk2::CObject::vftable) continue;
            auto *o = (dk2::CObject *) t;
            if (o->fE_type == 2) typed++;
            else mismatch++;
            if (count >= c_maxObjects) continue;
            Object &r = out[count++];
            r.pos[0] = o->f16_pos.x;
            r.pos[1] = o->f16_pos.y;
            r.pos[2] = o->f16_pos.z;
            r.tagId = o->f0_tagId;
            r.owner = o->f24_playerId;
            r.direction = o->fF0_direction;
            r.typeId = o->typeId;
            r.animationId = o->renderInfo.animationId;
            r.flags = o->flags;
            r.stateId = o->stateId;
            r.currentFrame = o->renderInfo.currentFrame;
            r.mapWhoType = o->f12_mapWhoType;
            r.floorHeight = o->f22_floorHeight;
            r.pad = 0;
            r.goldHeld = (o->typeId >= 1 && o->typeId <= 3) ? o->unionData.l40_gold.goldHeld : 0;
            r.baseColour[0] = (uint8_t) o->renderInfo.x48;
            r.baseColour[1] = (uint8_t) o->renderInfo.y49;
            r.baseColour[2] = (uint8_t) o->renderInfo.z4A;
            r.pad2 = 0;
            r.lastFrame = o->renderInfo.lastFrame;
        }
        h.objectsCount = count;
        h.objectsTyped = typed;
        h.objectsMismatch = mismatch;
        h.objectsState = OS_Exported;
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
    h.viewLeftRight[0] = state.leftRight.x;
    h.viewLeftRight[1] = state.leftRight.y;
    h.viewTopBottom[0] = state.topBottom.x;
    h.viewTopBottom[1] = state.topBottom.y;
    h.viewCentre[0] = state.trg.x;
    h.viewCentre[1] = state.trg.y;
    h.renderLeft = dk2::g_sc_renderLeft;
    h.renderTop = dk2::g_sc_renderTop;
    h.sceneWidth = dk2::g_sc_sceneWidth;
    h.sceneHeight = dk2::g_sc_sceneHeight;
    h.worldPos[0] = camera.arrx3__worldCameraPos.x;
    h.worldPos[1] = camera.arrx3__worldCameraPos.y;
    h.worldPos[2] = camera.arrx3__worldCameraPos.z;
    h.zoom = camera.curZoomLevel;
    h.direction = camera.direction;
    writeTiles(h, inLevel);
    writeLights(h, inLevel);
    writeBend(h, inLevel);
    writeObjects(h, inLevel);
    h.ambient[0] = dk2::g_vec_760A98.x;
    h.ambient[1] = dk2::g_vec_760A98.y;
    h.ambient[2] = dk2::g_vec_760A98.z;
    for (int c = 0; c < 3; ++c) h.tileLightOtherColour[c] = ((const int16_t *) 0x6F29F0)[c];
    h.goldPerTile = *(const int32_t *) 0x6F25A4;  // no names for these globals yet
    h.goldPerTileHeart = *(const int32_t *) 0x6F25A8;
    h.tickPercent256 = dk2::CGameComponent_instance.gameSession.tickPercent256;
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
