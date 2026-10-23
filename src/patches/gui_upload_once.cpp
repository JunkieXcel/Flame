//
// Created by Alex on 22.10.2026.
//

#include "gui_upload_once.h"
#include "dk2/utils/AABB.h"
#include "dk2/CDefaultPlayerInterface.h"
#include "dk2/RtGuiView.h"
#include "dk2/components/CGameComponent.h"
#include "dk2/math/int_float.h"
#include "dk2_functions.h"
#include "dk2_globals.h"
#include <tools/flame_config.h>
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>


flame_config::define_flame_option<bool> o_guiUploadOnce(
    "flame:gui-upload-once", flame_config::OG_Config,
    "Copy each in-game text view (menus, tooltips, messages) to its textures at most once per frame\n"
    "instead of after every text that is printed into it",
    true
);

flame_config::define_flame_option<bool> o_guiDirtyTiles(
    "flame:gui-dirty-tiles", flame_config::OG_Config,
    "When a text view is copied to its textures, copy only the 128x32 tiles whose pixels changed\n"
    "since they were last copied (keeps a 64-bit hash of each tile to compare with)",
    true
);

flame_config::define_flame_option<bool> o_guiSkipBlankTiles(
    "flame:gui-skip-blank-tiles", flame_config::OG_Config,
    "Don't draw the 128x32 tiles of a text view that have nothing printed on them\n"
    "(needs flame:gui-dirty-tiles, which finds them while comparing)",
    true
);

flame_config::define_flame_option<bool> o_guiTrace(
    "flame:debug:gui-trace", flame_config::OG_Config,
    "Debug: once per second, write how often each in-game text view is uploaded and drawn per frame,\n"
    "and how long wiping, printing, comparing, uploading and drawing them takes, to flame\\gui-trace.log",
    false
);

namespace {

    // views with a deferred upload, in the order they were requested
    std::vector<std::pair<dk2::CDefaultPlayerInterface *, dk2::RtGuiView *>> g_dirty;
    bool g_uploadingNow = false;

    bool takeDirty(dk2::RtGuiView *view, dk2::CDefaultPlayerInterface **defplif) {
        for (auto it = g_dirty.begin(); it != g_dirty.end(); ++it) {
            if (it->second != view) continue;
            *defplif = it->first;
            g_dirty.erase(it);
            return true;
        }
        return false;
    }

    void uploadNow(dk2::CDefaultPlayerInterface *defplif, dk2::RtGuiView *view) {
        // view was released after the upload was deferred. its lpSurface still points to the freed buffer
        if (view->blocksBuf == nullptr || view->surf.lpSurface == nullptr) return;
        g_uploadingNow = true;
        defplif->createSurfacesForView_42CDF0(view);
        g_uploadingNow = false;
    }

    // ---- gui-dirty-tiles ----

    // what each tile held when it was last uploaded: a hash of its pixels instead of a copy of them.
    // a copy of every view doubles the ~111 MB the views take at 3840x2160, which a 32-bit DKII can't afford
    struct Shadow {
        void *lpSurface = nullptr;
        LONG lPitch = 0;
        uint32_t width_128blocks = 0;
        uint32_t height_32blocks = 0;
        uint32_t dwRGBBitCount = 0;
        size_t rowBytes = 0;  // of one tile
        std::vector<uint64_t> hashes;
        std::vector<int> ids;  // texture id each tile was uploaded to
        std::vector<uint8_t> uploaded;  // 1 = hashes and ids hold what the tile was last uploaded with
        std::vector<uint8_t> blank;  // 1 = tile was uploaded with nothing printed on it
    };

    std::map<dk2::RtGuiView *, Shadow> g_shadows;
    Shadow *g_curShadow = nullptr;  // shadow of the view being uploaded

    bool matches(const Shadow &shadow, dk2::RtGuiView *view) {
        return shadow.lpSurface == view->surf.lpSurface && shadow.lPitch == view->surf.lPitch &&
               shadow.width_128blocks == view->width_128blocks && shadow.height_32blocks == view->height_32blocks &&
               shadow.dwRGBBitCount == view->dwRGBBitCount;
    }

    uint32_t rotl32(uint32_t v, int r) {
        return (v << r) | (v >> (32 - r));
    }

    // 64-bit hash of a tile's 32 rows: two different 32-bit hashes of the same dwords (an xxHash32 style one
    // in 4 lanes and a murmur3 style one), so a changed tile looks unchanged with a chance of about 2^-64.
    // 32-bit multiplies only: DKII is a 32-bit process. *blank is set when every pixel is the color
    // RtGuiView_42C9B0 wipes with, or zero as RtGuiView_sub_42CA70 clears with
    uint64_t hashTile(const char *pixels, int pitch, size_t rowBytes, uint32_t bpp, bool *blank) {
        const uint32_t P1 = 0x9E3779B1, P2 = 0x85EBCA77, P3 = 0xC2B2AE3D;
        const uint32_t C1 = 0xCC9E2D51, C2 = 0x1B873593;
        uint32_t a0 = P1 + P2, a1 = P2, a2 = 0, a3 = 0 - P1;
        uint32_t m = 0x2545F491;
        uint32_t first = *(const uint32_t *) pixels;
        uint32_t diff = 0;
        size_t rowDwords = rowBytes / 4;  // 128 or 64, a multiple of 4
        for (int y = 0; y < 32; ++y) {
            auto *px = (const uint32_t *) (pixels + y * pitch);
            for (size_t x = 0; x < rowDwords; x += 4) {
                uint32_t d0 = px[x], d1 = px[x + 1], d2 = px[x + 2], d3 = px[x + 3];
                diff |= (d0 ^ first) | (d1 ^ first) | (d2 ^ first) | (d3 ^ first);
                a0 = rotl32(a0 + d0 * P2, 13) * P1;
                a1 = rotl32(a1 + d1 * P2, 13) * P1;
                a2 = rotl32(a2 + d2 * P2, 13) * P1;
                a3 = rotl32(a3 + d3 * P2, 13) * P1;
                m = rotl32(m ^ (rotl32(d0 * C1, 15) * C2), 13) * 5 + 0xE6546B64;
                m = rotl32(m ^ (rotl32(d1 * C1, 15) * C2), 13) * 5 + 0xE6546B64;
                m = rotl32(m ^ (rotl32(d2 * C1, 15) * C2), 13) * 5 + 0xE6546B64;
                m = rotl32(m ^ (rotl32(d3 * C1, 15) * C2), 13) * 5 + 0xE6546B64;
            }
            m ^= (uint32_t) y * P3;  // rows apart from each other
        }
        *blank = diff == 0 && (first == 0 || first == patch::gui_upload_once::wipeColor(bpp));
        uint32_t h = rotl32(a0, 1) + rotl32(a1, 7) + rotl32(a2, 12) + rotl32(a3, 18);
        h ^= h >> 15; h *= P2; h ^= h >> 13; h *= P3; h ^= h >> 16;
        m ^= m >> 16; m *= 0x85EBCA6B; m ^= m >> 13; m *= 0xC2B2AE35; m ^= m >> 16;
        return ((uint64_t) h << 32) | m;
    }

    Shadow &shadowFor(dk2::RtGuiView *view) {
        Shadow &shadow = g_shadows[view];
        if (!matches(shadow, view)) {
            // view was (re)created with other pixels: everything must be uploaded again
            size_t tiles = view->width_128blocks * view->height_32blocks;
            shadow.lpSurface = view->surf.lpSurface;
            shadow.lPitch = view->surf.lPitch;
            shadow.width_128blocks = view->width_128blocks;
            shadow.height_32blocks = view->height_32blocks;
            shadow.dwRGBBitCount = view->dwRGBBitCount;
            shadow.rowBytes = 128 * (view->dwRGBBitCount / 8);
            shadow.hashes.assign(tiles, 0);
            shadow.ids.assign(tiles, 0);
            shadow.uploaded.assign(tiles, 0);
            shadow.blank.assign(tiles, 0);
        }
        return shadow;
    }

    // what is known about each tile of a view's pixels, from watching everything that writes them:
    // printText, the clears and sub_42C7D0 (no other code in DKII touches a view's pixels)
    enum TileState : uint8_t {
        TS_Unknown = 0,  // anything may have been printed on it
        TS_Wiped,        // every pixel is wipeColor()
        TS_Zero,         // every pixel is 0
    };

    struct Track {
        void *lpSurface = nullptr;
        LONG lPitch = 0;
        uint32_t width_128blocks = 0;
        uint32_t height_32blocks = 0;
        uint32_t dwRGBBitCount = 0;
        std::vector<uint8_t> state;    // TileState
        std::vector<uint8_t> written;  // 1 = pixels may have changed since the tile was last uploaded
        bool known = false;  // state was set by a whole-view clear since the track was (re)made
    };

    std::map<dk2::RtGuiView *, Track> g_tracks;
    Track *g_curTrack = nullptr;  // track of the view being uploaded

    Track &trackFor(dk2::RtGuiView *view) {
        Track &track = g_tracks[view];
        if (track.lpSurface != view->surf.lpSurface || track.lPitch != view->surf.lPitch ||
            track.width_128blocks != view->width_128blocks || track.height_32blocks != view->height_32blocks ||
            track.dwRGBBitCount != view->dwRGBBitCount) {
            size_t tiles = view->width_128blocks * view->height_32blocks;
            track.lpSurface = view->surf.lpSurface;
            track.lPitch = view->surf.lPitch;
            track.width_128blocks = view->width_128blocks;
            track.height_32blocks = view->height_32blocks;
            track.dwRGBBitCount = view->dwRGBBitCount;
            track.state.assign(tiles, TS_Unknown);
            track.written.assign(tiles, 1);
            track.known = false;  // a buffer at another place or of another size: its pixels are not the ones tracked
        }
        return track;
    }

    // forget a view's track and shadow. the pointers to them that are kept during an upload must not dangle
    void forget(dk2::RtGuiView *view) {
        if (auto it = g_tracks.find(view); it != g_tracks.end()) {
            if (g_curTrack == &it->second) g_curTrack = nullptr;
            g_tracks.erase(it);
        }
        if (auto it = g_shadows.find(view); it != g_shadows.end()) {
            if (g_curShadow == &it->second) g_curShadow = nullptr;
            g_shadows.erase(it);
        }
    }

    // fill the whole view, exactly as the original code does
    void fillView(dk2::RtGuiView *view, uint32_t color) {
        if (color == 0) {
            memset(view->blocksBuf, 0, view->blocksCount_bytes);  // as RtGuiView_sub_42CA70 and printText do
            return;
        }
        // as RtGuiView_42C9B0 does
        unsigned int bpp = view->surf.desc.dwRGBBitCount;
        unsigned int rowDwords = (view->width_128align * bpp) >> 5;
        unsigned int pitchDwords = (unsigned int) view->surf.lPitch >> 2;
        uint32_t *row = (uint32_t *) view->surf.lpSurface;
        for (int y = 0; y < (int) view->height_32align; ++y) {
            for (unsigned int x = 0; x < rowDwords; ++x) row[x] = color;
            row += pitchDwords;
        }
    }

    void fillTile(dk2::RtGuiView *view, unsigned int tx, unsigned int ty, uint32_t color) {
        unsigned int bpp = view->surf.desc.dwRGBBitCount;
        unsigned int rowDwords = (128 * bpp) >> 5;
        char *row = (char *) view->surf.lpSurface + ty * 32 * view->surf.lPitch + tx * 128 * (bpp / 8);
        for (int y = 0; y < 32; ++y) {
            auto *px = (uint32_t *) row;
            for (unsigned int x = 0; x < rowDwords; ++x) px[x] = color;
            row += view->surf.lPitch;
        }
    }

    bool tileIs(dk2::RtGuiView *view, unsigned int tx, unsigned int ty, uint32_t color) {
        unsigned int bpp = view->surf.desc.dwRGBBitCount;
        unsigned int rowDwords = (128 * bpp) >> 5;
        const char *row = (const char *) view->surf.lpSurface + ty * 32 * view->surf.lPitch + tx * 128 * (bpp / 8);
        for (int y = 0; y < 32; ++y) {
            auto *px = (const uint32_t *) row;
            for (unsigned int x = 0; x < rowDwords; ++x) {
                if (px[x] != color) return false;
            }
            row += view->surf.lPitch;
        }
        return true;
    }

    // ---- gui-trace timers ----

    LONGLONG g_timerTicks[patch::gui_upload_once::GT_Count] = {};
    int g_timerStack[16];
    int g_timerDepth = 0;
    LONGLONG g_timerSegStart = 0;  // when the timer on top of the stack was (re)started

    LONGLONG qpc() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart;
    }

    // ---- gui-trace ----

    struct ViewStats {
        unsigned int requests = 0;
        unsigned int uploads = 0;
        unsigned int tiles = 0;
        unsigned int tilesSkipped = 0;
        unsigned long long bytes = 0;
        unsigned int tilesDrawn = 0;
        unsigned int tilesBlank = 0;  // not drawn
        unsigned int tilesUnwritten = 0;  // not compared: nothing wrote them since their last upload
        unsigned int tilesCleared = 0;  // filled by a clear
        unsigned int tilesClearSkipped = 0;  // not filled by a clear: they already hold its color
        unsigned int extentFallbacks = 0;  // printText without a usable extent: whole view marked written
        unsigned int missed = 0;  // verify: tiles the tracking was wrong about (fixed when found)
        unsigned int released = 0;  // clears, prints and uploads skipped: the view has no buffer
    };

    const std::map<size_t, const char *> c_viewNames = {
        {0x1409, "_eventMessageText"},
        {0x16E0, "_wonMpdText"},
        {0x1984, "_allyWindowText"},
        {0x1F45, "_chatMessageHistoryWindowText"},
        {0x21E0, "_multiplayerMessageWindowText"},
        {0x2487, "_imeText"},
        {0x2737, "_options"},
        {0x29D6, "_optionsListBox"},
        {0x2C71, "_optionsLoadSaveBox"},
        {0x2F0C, "_objectiveText"},
        {0x31A7, "_generalText"},
        {0x344A, "_firstFightText"},
        {0x36F1, "_playerInfoText"},
        {0x3B9C, "_playerNameText"},
        {0x3E73, "_followPathSubtitleTextBuffer"},
        {0x4116, "_kingOfTheHill"},
        {0x43B5, "_pointerInfo"},
        {0x4660, "_tooltip"},
    };

    FILE *g_log = nullptr;
    bool g_logFailed = false;
    DWORD g_runStart = 0;
    DWORD g_windowStart = 0;
    unsigned int g_frames = 0;
    std::map<dk2::RtGuiView *, ViewStats> g_stats;

    ViewStats &statsFor(dk2::RtGuiView *view) {
        return g_stats[view];
    }

    std::string viewName(dk2::RtGuiView *view) {
        size_t offs = (size_t) ((char *) view - (char *) &dk2::CDefaultPlayerInterface_instance);
        if (auto it = c_viewNames.find(offs); it != c_viewNames.end()) return it->second;
        char buf[32];
        snprintf(buf, sizeof(buf), "view_%p", view);
        return buf;
    }

    bool openLog() {
        if (g_log) return true;
        if (g_logFailed) return false;
        std::string path = std::string(dk2::fs_getExeDir()) + "flame\\gui-trace.log";
        g_log = fopen(path.c_str(), "a");
        if (!g_log) {
            g_logFailed = true;
            printf("[gui-trace] unable to open %s\n", path.c_str());
            return false;
        }
        char timeStr[64];
        time_t now = time(nullptr);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(g_log, "=== gui-trace run started %s (gui-upload-once=%d gui-dirty-tiles=%d gui-skip-blank-tiles=%d) ===\n",
                timeStr, (int) *o_guiUploadOnce, (int) *o_guiDirtyTiles, (int) *o_guiSkipBlankTiles);
        // the format text views are created with. tells how the wipe color's alpha is meant
        dk2::MySurfDesc &desc = dk2::MyCEngineSurfDesc_argb32_instance.desc;
        fprintf(g_log, "view format: bpp=%u R=%08X G=%08X B=%08X A=%08X wipe=%08X\n",
                desc.dwRGBBitCount, desc.dwRBitMask, desc.dwGBitMask, desc.dwBBitMask, desc.dwRGBAlphaBitMask,
                patch::gui_upload_once::wipeColor(desc.dwRGBBitCount));
        fflush(g_log);
        return true;
    }

    void writeStats(DWORD now) {
        if (!openLog()) return;
        float seconds = (float) (now - g_runStart) / 1000.0f;
        float fps = dk2ex::toFloat(dk2::CGameComponent_instance.fps);
        int paused = dk2::CGameComponent_instance.gameSession.inMenu != 0;
        float frames = g_frames ? (float) g_frames : 1.0f;
        ViewStats total;
        for (auto &[view, stats] : g_stats) {
            total.requests += stats.requests;
            total.uploads += stats.uploads;
            total.tiles += stats.tiles;
            total.tilesSkipped += stats.tilesSkipped;
            total.bytes += stats.bytes;
            total.tilesDrawn += stats.tilesDrawn;
            total.tilesBlank += stats.tilesBlank;
            total.tilesUnwritten += stats.tilesUnwritten;
            total.tilesCleared += stats.tilesCleared;
            total.tilesClearSkipped += stats.tilesClearSkipped;
            total.extentFallbacks += stats.extentFallbacks;
            total.missed += stats.missed;
            total.released += stats.released;
            fprintf(g_log, "t=%7.1fs fps=%6.1f paused=%d frames=%4u requests/frame=%6.2f uploads/frame=%6.2f tiles/frame=%8.1f skipped/frame=%8.1f (unwritten=%8.1f) KB/frame=%9.1f drawn/frame=%8.1f blank/frame=%8.1f cleared/frame=%8.1f clearSkipped/frame=%8.1f fallbacks=%u missed=%u released=%u  %s\n",
                    seconds, fps, paused, g_frames,
                    stats.requests / frames, stats.uploads / frames, stats.tiles / frames, stats.tilesSkipped / frames,
                    stats.tilesUnwritten / frames, stats.bytes / frames / 1024.0f, stats.tilesDrawn / frames,
                    stats.tilesBlank / frames, stats.tilesCleared / frames, stats.tilesClearSkipped / frames,
                    stats.extentFallbacks, stats.missed, stats.released, viewName(view).c_str());
        }
        fprintf(g_log, "t=%7.1fs fps=%6.1f paused=%d frames=%4u requests/frame=%6.2f uploads/frame=%6.2f tiles/frame=%8.1f skipped/frame=%8.1f (unwritten=%8.1f) KB/frame=%9.1f drawn/frame=%8.1f blank/frame=%8.1f cleared/frame=%8.1f clearSkipped/frame=%8.1f fallbacks=%u missed=%u released=%u  TOTAL\n",
                seconds, fps, paused, g_frames,
                total.requests / frames, total.uploads / frames, total.tiles / frames, total.tilesSkipped / frames,
                total.tilesUnwritten / frames, total.bytes / frames / 1024.0f, total.tilesDrawn / frames,
                total.tilesBlank / frames, total.tilesCleared / frames, total.tilesClearSkipped / frames,
                total.extentFallbacks, total.missed, total.released);
        // exclusive times: a timer stops while one nested in it runs (e.g. text excludes a wipe it does)
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        auto ms = [&](int timer) { return (double) g_timerTicks[timer] * 1000.0 / (double) freq.QuadPart / frames; };
        fprintf(g_log, "t=%7.1fs fps=%6.1f paused=%d frames=%4u ms/frame: wipe=%7.3f text=%7.3f compare=%7.3f upload=%7.3f draw=%7.3f verify=%7.3f  TIMINGS\n",
                seconds, fps, paused, g_frames,
                ms(patch::gui_upload_once::GT_Wipe), ms(patch::gui_upload_once::GT_Text),
                ms(patch::gui_upload_once::GT_Compare), ms(patch::gui_upload_once::GT_Upload),
                ms(patch::gui_upload_once::GT_Draw), ms(patch::gui_upload_once::GT_Verify));
        // memory: the views' own pixel buffers, and what gui-upload-once keeps about their tiles
        size_t viewBytes = 0, viewCount = 0, keptBytes = 0;
        for (auto &[view, track] : g_tracks) {
            if (patch::gui_upload_once::hasPixels(view)) {
                viewBytes += view->blocksCount_bytes;
                viewCount++;
            }
            keptBytes += track.state.capacity() + track.written.capacity();
        }
        for (auto &[view, shadow] : g_shadows) {
            keptBytes += shadow.hashes.capacity() * sizeof(uint64_t) + shadow.ids.capacity() * sizeof(int) +
                         shadow.uploaded.capacity() + shadow.blank.capacity();
        }
        MEMORYSTATUSEX mem{sizeof(mem)};
        GlobalMemoryStatusEx(&mem);
        fprintf(g_log, "t=%7.1fs views=%u viewBuffers=%.2fMB tracking=%.1fKB process: usedVA=%.0fMB freeVA=%.0fMB  MEMORY\n",
                seconds, (unsigned int) viewCount, viewBytes / 1048576.0, keptBytes / 1024.0,
                (mem.ullTotalVirtual - mem.ullAvailVirtual) / 1048576.0, mem.ullAvailVirtual / 1048576.0);
        fflush(g_log);
    }

    void resetWindow() {
        g_stats.clear();
        g_frames = 0;
        memset(g_timerTicks, 0, sizeof(g_timerTicks));
    }

    void traceFrame() {
        if (!*o_guiTrace) {
            resetWindow();
            g_windowStart = 0;
            return;
        }
        DWORD now = GetTickCount();
        if (g_runStart == 0) g_runStart = now;
        if (g_windowStart == 0) g_windowStart = now;
        g_frames++;
        if (now - g_windowStart >= 1000) {
            writeStats(now);
            resetWindow();
            g_windowStart = now;
        }
    }

}

bool patch::gui_upload_once::deferUpload(dk2::CDefaultPlayerInterface *defplif, dk2::RtGuiView *view) {
    if (g_uploadingNow) return false;
    if (*o_guiTrace) statsFor(view).requests++;
    if (!*o_guiUploadOnce) return false;
    for (auto &[_, dirtyView] : g_dirty) {
        if (dirtyView == view) return true;  // already scheduled
    }
    g_dirty.emplace_back(defplif, view);
    return true;
}

void patch::gui_upload_once::beginUpload(dk2::RtGuiView *view) {
    if (!hasPixels(view)) {
        // not laid out as sub_42C7D0 makes it: nothing tracked or compared can be trusted, upload every tile
        forget(view);
        g_curTrack = nullptr;
        g_curShadow = nullptr;
        return;
    }
    g_curTrack = &trackFor(view);
    if (!*o_guiDirtyTiles) {
        if (!g_shadows.empty()) g_shadows.clear();  // free the copies, and don't trust them if re-enabled
        g_curShadow = nullptr;
        return;
    }
    g_curShadow = &shadowFor(view);
}

bool patch::gui_upload_once::tileChanged(dk2::RtGuiView *view, unsigned int idx, int id, const char *pixels, int pitch) {
    Shadow *shadow = g_curShadow;
    if (shadow == nullptr || idx >= shadow->ids.size()) return true;
    bool known = shadow->uploaded[idx] && shadow->ids[idx] == id;
    bool blank;
    if (known && g_curTrack && idx < g_curTrack->written.size() && !g_curTrack->written[idx]) {
        // nothing wrote it since it was uploaded
        if (!*o_guiTrace) return false;
        ScopedTimer timer(GT_Verify);
        ViewStats &stats = statsFor(view);
        if (hashTile(pixels, pitch, shadow->rowBytes, shadow->dwRGBBitCount, &blank) == shadow->hashes[idx]) {
            stats.tilesUnwritten++;
            return false;
        }
        stats.missed++;  // something wrote it without being tracked: upload it anyway
    }
    ScopedTimer timer(GT_Compare);
    uint64_t hash = hashTile(pixels, pitch, shadow->rowBytes, shadow->dwRGBBitCount, &blank);
    if (known && hash == shadow->hashes[idx]) return false;
    shadow->hashes[idx] = hash;
    shadow->ids[idx] = id;
    shadow->uploaded[idx] = 1;
    shadow->blank[idx] = blank;
    return true;
}

const uint8_t *patch::gui_upload_once::blankTiles(dk2::RtGuiView *view) {
    if (!*o_guiSkipBlankTiles || !*o_guiDirtyTiles || !hasPixels(view)) return nullptr;
    auto it = g_shadows.find(view);
    if (it == g_shadows.end() || !matches(it->second, view)) return nullptr;
    return it->second.blank.data();
}

void patch::gui_upload_once::onDraw(dk2::RtGuiView *view, unsigned int tilesDrawn, unsigned int tilesBlank) {
    if (!*o_guiTrace) return;
    ViewStats &stats = statsFor(view);
    stats.tilesDrawn += tilesDrawn;
    stats.tilesBlank += tilesBlank;
}

uint32_t patch::gui_upload_once::wipeColor(uint32_t bpp) {
    // as RtGuiView_42C9B0 computes it: black with the alpha top bit, per pixel at 32bpp or per pixel pair at 16bpp
    return bpp == 16 ? 0x80008000 : 0x80000000;
}

patch::gui_upload_once::ScopedTimer::ScopedTimer(GuiTimer timer) {
    active = *o_guiTrace && g_timerDepth < (int) std::size(g_timerStack);
    if (!active) return;
    LONGLONG now = qpc();
    if (g_timerDepth > 0) g_timerTicks[g_timerStack[g_timerDepth - 1]] += now - g_timerSegStart;  // pause the outer timer
    g_timerStack[g_timerDepth++] = timer;
    g_timerSegStart = now;
}

patch::gui_upload_once::ScopedTimer::~ScopedTimer() {
    if (!active) return;
    LONGLONG now = qpc();
    g_timerTicks[g_timerStack[--g_timerDepth]] += now - g_timerSegStart;
    g_timerSegStart = now;  // resume the outer timer
}

void patch::gui_upload_once::endUpload(dk2::RtGuiView *view, unsigned int tilesUploaded, unsigned int tilesSkipped) {
    if (g_curTrack) std::fill(g_curTrack->written.begin(), g_curTrack->written.end(), 0);  // textures are up to date
    g_curTrack = nullptr;
    g_curShadow = nullptr;
    if (!*o_guiTrace) return;
    ViewStats &stats = statsFor(view);
    stats.uploads++;
    stats.tiles += tilesUploaded;
    stats.tilesSkipped += tilesSkipped;
    stats.bytes += (unsigned long long) tilesUploaded * 128 * 32 * (view->dwRGBBitCount / 8);
}

void patch::gui_upload_once::onViewCreated(dk2::RtGuiView *view) {
    // its textures and pixels are new, even if the buffer got the same address and size as before.
    // a deferred upload would copy the old texts to the new textures
    onViewReleased(view);
}

void patch::gui_upload_once::onViewReleased(dk2::RtGuiView *view) {
    forget(view);
    // a deferred upload would read the freed buffer
    for (auto it = g_dirty.begin(); it != g_dirty.end();) {
        if (it->second == view) it = g_dirty.erase(it);
        else ++it;
    }
}

bool patch::gui_upload_once::hasPixels(dk2::RtGuiView *view) {
    if (view->blocksBuf == nullptr || view->surf.lpSurface != view->blocksBuf) return false;
    unsigned int bpp = view->dwRGBBitCount;
    return bpp == view->surf.desc.dwRGBBitCount && (bpp == 16 || bpp == 32) &&
           view->width_128align == view->width_128blocks * 128 && view->height_32align == view->height_32blocks * 32 &&
           view->surf.lPitch == (LONG) (view->width_128align * (bpp / 8)) &&
           view->blocksCount_bytes == view->height_32align * (uint32_t) view->surf.lPitch;
}

void patch::gui_upload_once::clearView(dk2::RtGuiView *view, uint32_t color) {
    bool verify = *o_guiTrace;
    if (view->blocksBuf == nullptr) {
        // released or never created. the original would memset a null blocksBuf (RtGuiView_sub_42CA70, printText)
        // or fill the freed buffer lpSurface still points to (RtGuiView_42C9B0)
        forget(view);
        if (verify) statsFor(view).released++;
        return;
    }
    if (!hasPixels(view)) {
        // not laid out as sub_42C7D0 makes it: do what the original does, and track nothing
        forget(view);
        fillView(view, color);
        return;
    }
    Track &track = trackFor(view);
    uint8_t state = color == 0 ? TS_Zero : TS_Wiped;
    unsigned int cleared = 0;
    unsigned int skipped = 0;
    unsigned int missed = 0;
    if (!*o_guiDirtyTiles || !track.known) {
        // option off, or what the tiles hold is unknown (first clear since the view was created, resized or
        // reallocated): fill the whole view as the original does. after it, every tile is known
        fillView(view, color);
        for (size_t idx = 0; idx < track.state.size(); ++idx) {
            if (!track.known || track.state[idx] != state) track.written[idx] = 1;  // else pixels didn't change
            track.state[idx] = state;
        }
        track.known = true;
        cleared = (unsigned int) track.state.size();
    } else {
        // fill only the tiles that may hold something else. the result is the same as filling the whole view
        for (unsigned int ty = 0; ty < view->height_32blocks; ++ty) {
            for (unsigned int tx = 0; tx < view->width_128blocks; ++tx) {
                unsigned int idx = tx + view->width_128blocks * ty;
                if (track.state[idx] == state) {
                    if (verify) {
                        ScopedTimer timer(GT_Verify);
                        if (!tileIs(view, tx, ty, color)) {
                            missed++;  // something wrote it without being tracked: fill it anyway
                            fillTile(view, tx, ty, color);
                            track.written[idx] = 1;
                            continue;
                        }
                    }
                    skipped++;
                    continue;
                }
                fillTile(view, tx, ty, color);
                track.state[idx] = state;
                track.written[idx] = 1;
                cleared++;
            }
        }
    }
    if (verify) {
        ViewStats &stats = statsFor(view);
        stats.tilesCleared += cleared;
        stats.tilesClearSkipped += skipped;
        stats.missed += missed;
    }
}

void patch::gui_upload_once::onPrinted(dk2::RtGuiView *view, const dk2::AABB *extent) {
    if (!hasPixels(view)) {
        forget(view);  // whatever was tracked no longer says what the pixels hold
        return;
    }
    Track &track = trackFor(view);
    unsigned int tx0 = 0, ty0 = 0;
    unsigned int tx1 = view->width_128blocks;  // exclusive
    unsigned int ty1 = view->height_32blocks;
    bool usable = extent && extent->minX <= extent->maxX && extent->minY <= extent->maxY &&
                  !(extent->minX == 0 && extent->minY == 0 && extent->maxX == 0 && extent->maxY == 0);
    if (usable) {
        // a few pixels more for glyphs that reach out of the laid out box. inclusive or exclusive max, both covered
        const int pad = 2;
        int minX = (std::max)(extent->minX - pad, 0);
        int minY = (std::max)(extent->minY - pad, 0);
        int maxX = extent->maxX + pad;
        int maxY = extent->maxY + pad;
        tx0 = (std::min)((unsigned int) minX / 128, tx1);
        ty0 = (std::min)((unsigned int) minY / 32, ty1);
        if (maxX < 0 || maxY < 0) {
            tx1 = tx0;  // all left of or above the view: nothing to mark
        } else {
            tx1 = (std::min)((unsigned int) maxX / 128 + 1, tx1);
            ty1 = (std::min)((unsigned int) maxY / 32 + 1, ty1);
        }
    } else if (*o_guiTrace) {
        statsFor(view).extentFallbacks++;
    }
    for (unsigned int ty = ty0; ty < ty1; ++ty) {
        for (unsigned int tx = tx0; tx < tx1; ++tx) {
            unsigned int idx = tx + view->width_128blocks * ty;
            track.state[idx] = TS_Unknown;
            track.written[idx] = 1;
        }
    }
}

void patch::gui_upload_once::flushView(dk2::CDefaultPlayerInterface *defplif, dk2::RtGuiView *view) {
    dk2::CDefaultPlayerInterface *owner;
    if (takeDirty(view, &owner)) uploadNow(owner, view);
}

void patch::gui_upload_once::beforeDrawScene() {
    // views whose tiles were not drawn through sub_42CEE0 this frame, or that got new text after it
    auto dirty = std::move(g_dirty);
    g_dirty.clear();
    for (auto &[defplif, view] : dirty) uploadNow(defplif, view);
    traceFrame();
}
