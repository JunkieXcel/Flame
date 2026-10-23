//
// Created by Alex on 23.10.2026.
//

#ifndef FLAME_GUI_SCALE_H
#define FLAME_GUI_SCALE_H

#include <cstdint>

namespace dk2 {
    struct MySurface;
}

namespace patch::gui_scale {

    // flame:gui-scale. called by CDefaultPlayerInterface::fun_402D00 in place of loadFontByScreenWidth.
    // returns false when the option is off: the caller then loads the fonts as the original does
    bool loadInGameFonts(uint32_t width, uint32_t height);

    // pixels a glyph may reach out of the box renderText reports, for gui-dirty-tiles.
    // grows with the in-game fonts loaded by loadInGameFonts
    int glyphOverhang();

    // called by CFrontEndComponent::launchGame each time the front end is shown, once its screen is prepared and
    // before its buttons are baked. picks the nine front-end fonts for the menu size, or puts back the ones
    // loadFonts loaded (option off, 640x480, not enough address space). the in-game users of these fonts
    // (loading screen, outro, CGameComponent messages) keep what was picked here
    void loadFrontEndFonts(uint32_t width, uint32_t height);

    // called by CFrontEndComponent::launchGame for each list box: the row height of g_CListBox_itemHeights
    // is in pixels of 640x480. with flame:gui-scale it grows with the menu height, as the list box does
    int listItemHeight(int itemHeight, uint32_t menuHeight);

    // the x of a column in a front-end list row, in pixels of 640x480 from the row's left edge.
    // with flame:gui-scale it grows with the menu width, as the row does
    int listColumnX(int x, uint32_t menuWidth);

    // called by CFrontEndComponent::launchGame each time the front end is shown. the list renderers still in
    // DKII (load game, map selects, my pet dungeon/custom campaign levels, control keys) place their columns at
    // constants in pixels of 640x480: rewrites those constants for the menu size, or back to the originals
    void scaleListColumns(uint32_t menuWidth, uint32_t menuHeight);

    // called by the front-end bakes (bakeButton, renderButtonsText_5329A0) after they allocate the surface of a
    // baked menu, which grows with the fonts. logs its size and the free address space. false if it wasn't
    // allocated: the original memsets it unchecked
    bool checkBakedSurface(dk2::MySurface &surf, unsigned int x16Idx);

}


#endif // FLAME_GUI_SCALE_H
