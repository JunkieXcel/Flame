//
// Created by Alex on 22.10.2026.
//

#ifndef FLAME_GUI_UPLOAD_ONCE_H
#define FLAME_GUI_UPLOAD_ONCE_H

#include <cstdint>

namespace dk2 {
    struct AABB;
    struct CDefaultPlayerInterface;
    struct RtGuiView;
}

namespace patch::gui_upload_once {

    // called by CDefaultPlayerInterface::createSurfacesForView_42CDF0.
    // returns true if the upload was deferred and must not be done now
    bool deferUpload(dk2::CDefaultPlayerInterface *defplif, dk2::RtGuiView *view);
    // called by createSurfacesForView_42CDF0 when it really uploads the view, before its first tile
    void beginUpload(dk2::RtGuiView *view);
    // flame:gui-dirty-tiles. returns false if the 128x32 tile at idx has the same pixels as when it was
    // last uploaded to texture id, so uploading it again can be skipped
    bool tileChanged(dk2::RtGuiView *view, unsigned int idx, int id, const char *pixels, int pitch);
    // called by createSurfacesForView_42CDF0 after its last tile
    void endUpload(dk2::RtGuiView *view, unsigned int tilesUploaded, unsigned int tilesSkipped);
    // called by sub_42C7D0 when the view's buffer and textures are (re)created. forgets its shadow copy
    void onViewCreated(dk2::RtGuiView *view);

    // flame:gui-skip-blank-tiles. per tile, 1 if it was last uploaded with nothing printed on it, so drawing it
    // can be skipped. nullptr if unknown (option off, or the view's pixels were never compared)
    const uint8_t *blankTiles(dk2::RtGuiView *view);
    // called by sub_42CEE0 after drawing the view's tiles
    void onDraw(dk2::RtGuiView *view, unsigned int tilesDrawn, unsigned int tilesBlank);

    // the color RtGuiView_42C9B0 fills a view with, as a dword
    uint32_t wipeColor(uint32_t bpp);

    // fill the view with color (0 or wipeColor) as RtGuiView_42C9B0 / RtGuiView_sub_42CA70 / printText do.
    // with flame:gui-dirty-tiles, fills only the tiles not known to hold that color already
    void clearView(dk2::RtGuiView *view, uint32_t color);
    // called by printText after it rendered text into the view. extent is the box renderText reported,
    // nullptr if unknown. marks the tiles under it as written, so they are compared at the next upload
    void onPrinted(dk2::RtGuiView *view, const dk2::AABB *extent);

    // gui-trace: time spent per frame. exclusive: a timer is paused while a timer nested in it runs
    enum GuiTimer {
        GT_Wipe,     // RtGuiView_42C9B0 / RtGuiView_sub_42CA70
        GT_Text,     // CDefaultPlayerInterface::printText
        GT_Compare,  // gui-dirty-tiles compare and copy
        GT_Upload,   // CBridge::v_f68_paintSurfToMapId
        GT_Draw,     // CDefaultPlayerInterface::sub_42CEE0
        GT_Verify,   // gui-trace only: checking that tiles skipped as unwritten or already clear really are
        GT_Count
    };
    struct ScopedTimer {
        explicit ScopedTimer(GuiTimer timer);
        ~ScopedTimer();
        bool active;
    };

    // upload the view now if it has a deferred upload (before its tiles are drawn)
    void flushView(dk2::CDefaultPlayerInterface *defplif, dk2::RtGuiView *view);
    // upload every view that still has a deferred upload (before the scene is drawn). ends the frame for gui-trace
    void beforeDrawScene();

}


#endif // FLAME_GUI_UPLOAD_ONCE_H
