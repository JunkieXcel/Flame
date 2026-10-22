//
// Created by Alex on 22.10.2026.
//

#include "gui_upload_once.h"
#include "dk2/CDefaultPlayerInterface.h"
#include "dk2/RtGuiView.h"
#include "dk2/components/CGameComponent.h"
#include "dk2/math/int_float.h"
#include "dk2_functions.h"
#include "dk2_globals.h"
#include <tools/flame_config.h>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
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
    "since they were last copied (keeps a copy of each view's pixels in memory to compare with)",
    true
);

flame_config::define_flame_option<bool> o_guiTrace(
    "flame:debug:gui-trace", flame_config::OG_Config,
    "Debug: once per second, write how often each in-game text view is uploaded per frame\n"
    "to flame\\gui-trace.log",
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
        if (view->surf.lpSurface == nullptr) return;  // view was released
        g_uploadingNow = true;
        defplif->createSurfacesForView_42CDF0(view);
        g_uploadingNow = false;
    }

    // ---- gui-dirty-tiles ----

    // the pixels of each tile as they were last uploaded, 128x32 contiguous per tile
    struct Shadow {
        void *lpSurface = nullptr;
        LONG lPitch = 0;
        uint32_t width_128blocks = 0;
        uint32_t height_32blocks = 0;
        uint32_t dwRGBBitCount = 0;
        size_t tileBytes = 0;
        std::vector<char> pixels;
        std::vector<int> ids;  // texture id each tile was uploaded to. 0 = never uploaded
    };

    std::map<dk2::RtGuiView *, Shadow> g_shadows;
    Shadow *g_curShadow = nullptr;  // shadow of the view being uploaded

    Shadow &shadowFor(dk2::RtGuiView *view) {
        Shadow &shadow = g_shadows[view];
        if (shadow.lpSurface != view->surf.lpSurface || shadow.lPitch != view->surf.lPitch ||
            shadow.width_128blocks != view->width_128blocks || shadow.height_32blocks != view->height_32blocks ||
            shadow.dwRGBBitCount != view->dwRGBBitCount) {
            // view was (re)created with other pixels: everything must be uploaded again
            size_t tiles = view->width_128blocks * view->height_32blocks;
            shadow.lpSurface = view->surf.lpSurface;
            shadow.lPitch = view->surf.lPitch;
            shadow.width_128blocks = view->width_128blocks;
            shadow.height_32blocks = view->height_32blocks;
            shadow.dwRGBBitCount = view->dwRGBBitCount;
            shadow.tileBytes = 128 * 32 * (view->dwRGBBitCount / 8);
            shadow.pixels.assign(tiles * shadow.tileBytes, 0);
            shadow.ids.assign(tiles, 0);
        }
        return shadow;
    }

    // ---- gui-trace ----

    struct ViewStats {
        unsigned int requests = 0;
        unsigned int uploads = 0;
        unsigned int tiles = 0;
        unsigned int tilesSkipped = 0;
        unsigned long long bytes = 0;
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
        fprintf(g_log, "=== gui-trace run started %s (gui-upload-once=%d gui-dirty-tiles=%d) ===\n",
                timeStr, (int) *o_guiUploadOnce, (int) *o_guiDirtyTiles);
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
            fprintf(g_log, "t=%7.1fs fps=%6.1f paused=%d frames=%4u requests/frame=%6.2f uploads/frame=%6.2f tiles/frame=%8.1f skipped/frame=%8.1f KB/frame=%9.1f  %s\n",
                    seconds, fps, paused, g_frames,
                    stats.requests / frames, stats.uploads / frames, stats.tiles / frames, stats.tilesSkipped / frames,
                    stats.bytes / frames / 1024.0f, viewName(view).c_str());
        }
        fprintf(g_log, "t=%7.1fs fps=%6.1f paused=%d frames=%4u requests/frame=%6.2f uploads/frame=%6.2f tiles/frame=%8.1f skipped/frame=%8.1f KB/frame=%9.1f  TOTAL\n",
                seconds, fps, paused, g_frames,
                total.requests / frames, total.uploads / frames, total.tiles / frames, total.tilesSkipped / frames,
                total.bytes / frames / 1024.0f);
        fflush(g_log);
    }

    void traceFrame() {
        if (!*o_guiTrace) {
            g_stats.clear();
            g_frames = 0;
            g_windowStart = 0;
            return;
        }
        DWORD now = GetTickCount();
        if (g_runStart == 0) g_runStart = now;
        if (g_windowStart == 0) g_windowStart = now;
        g_frames++;
        if (now - g_windowStart >= 1000) {
            writeStats(now);
            g_stats.clear();
            g_frames = 0;
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
    char *copy = &shadow->pixels[idx * shadow->tileBytes];
    size_t rowBytes = shadow->tileBytes / 32;
    bool changed = shadow->ids[idx] != id;
    for (int y = 0; y < 32 && !changed; ++y) {
        changed = memcmp(copy + y * rowBytes, pixels + y * pitch, rowBytes) != 0;
    }
    if (!changed) return false;
    for (int y = 0; y < 32; ++y) {
        memcpy(copy + y * rowBytes, pixels + y * pitch, rowBytes);
    }
    shadow->ids[idx] = id;
    return true;
}

void patch::gui_upload_once::endUpload(dk2::RtGuiView *view, unsigned int tilesUploaded, unsigned int tilesSkipped) {
    g_curShadow = nullptr;
    if (!*o_guiTrace) return;
    ViewStats &stats = statsFor(view);
    stats.uploads++;
    stats.tiles += tilesUploaded;
    stats.tilesSkipped += tilesSkipped;
    stats.bytes += (unsigned long long) tilesUploaded * 128 * 32 * (view->dwRGBBitCount / 8);
}

void patch::gui_upload_once::onViewCreated(dk2::RtGuiView *view) {
    // its textures are new, even if the buffer got the same address and size as before
    g_shadows.erase(view);
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
