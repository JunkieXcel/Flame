//
// Created by Alex on 23.10.2026.
//

#ifndef FLAME_GUI_SCALE_H
#define FLAME_GUI_SCALE_H

#include <cstdint>

namespace patch::gui_scale {

    // flame:gui-scale. called by CDefaultPlayerInterface::fun_402D00 in place of loadFontByScreenWidth.
    // returns false when the option is off: the caller then loads the fonts as the original does
    bool loadInGameFonts(uint32_t width, uint32_t height);

    // pixels a glyph may reach out of the box renderText reports, for gui-dirty-tiles.
    // grows with the in-game fonts loaded by loadInGameFonts
    int glyphOverhang();

}


#endif // FLAME_GUI_SCALE_H
