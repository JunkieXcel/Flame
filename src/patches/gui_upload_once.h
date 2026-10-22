//
// Created by Alex on 22.10.2026.
//

#ifndef FLAME_GUI_UPLOAD_ONCE_H
#define FLAME_GUI_UPLOAD_ONCE_H


namespace dk2 {
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

    // upload the view now if it has a deferred upload (before its tiles are drawn)
    void flushView(dk2::CDefaultPlayerInterface *defplif, dk2::RtGuiView *view);
    // upload every view that still has a deferred upload (before the scene is drawn). ends the frame for gui-trace
    void beforeDrawScene();

}


#endif // FLAME_GUI_UPLOAD_ONCE_H
