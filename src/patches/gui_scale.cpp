//
// Created by Alex on 23.10.2026.
//

#include "gui_scale.h"
#include "dk2/MyLangObj.h"
#include "dk2/text/render/FontObj.h"
#include "dk2/utils/Bgraf.h"
#include "dk2_functions.h"
#include "dk2_globals.h"
#include "patches/logging.h"
#include <tools/flame_config.h>
#include <algorithm>


flame_config::define_flame_option<bool> o_guiScale(
    "flame:gui-scale", flame_config::OG_Config,
    "Pick the in-game fonts (menus, panels, tooltips, messages) by the screen size, as DKII does up to 1024x768.\n"
    "DKII knows only the widths 400, 512, 640, 800 and 1024: any other width, 1920 or 3840 too, gets the 640x480 fonts.\n"
    "Uses the bigger fonts DKII ships (up to 72 px) but never picked. The front end menus are not changed",
    false
);

namespace {

    // the in-game fonts in Data\Text\Default\residx.dat: FONT_INGAME<size>.bf4, ids in order of size
    struct FontSize {
        int size;
        int id;
    };
    constexpr FontSize c_fonts[] = {
        {7, 0x444}, {8, 0x445}, {9, 0x446}, {10, 0x447}, {12, 0x448}, {14, 0x449}, {16, 0x44A}, {18, 0x44B},
        {20, 0x44C}, {22, 0x44D}, {24, 0x44E}, {26, 0x44F}, {28, 0x450}, {36, 0x451}, {48, 0x452}, {72, 0x453},
    };

    int sizeOf(int id) {
        for (auto &f : c_fonts) {
            if (f.id == id) return f.size;
        }
        return 0;
    }

    // loadFontByScreenWidth asks widthToFontId for base sizes 10, 14 and 18, and widthToFontId adds 4 at 1024 wide.
    // the GUI is laid out in 2560x1920 units and stretched to the screen (CGuiManager::scaleAabb_2560_1920),
    // so a box is (width / 1024) x (height / 768) times as big as at 1024x768. scale the 1024x768 size by the
    // smaller of the two, so the text fits the box both ways, and take the biggest font that isn't bigger.
    // never smaller than what the original picks
    int fontId(uint32_t baseSize, uint32_t width, uint32_t height) {
        int original = dk2::widthToFontId(baseSize, width);
        float scale = (std::min)((float) width / 1024.0f, (float) height / 768.0f);
        float wanted = (float) (baseSize + 4) * scale;
        int id = c_fonts[0].id;
        for (auto &f : c_fonts) {
            if ((float) f.size <= wanted) id = f.id;
        }
        return (std::max)(id, original);
    }

    int g_largestSize = 0;  // of the fonts loadInGameFonts loaded. 0 = the original ones

    // as loadFontByScreenWidth does for one font, with the font id given
    bool loadFont(dk2::FontObj &fontObj, dk2::MyTextFont **pFont, int id) {
        int status;
        if (*dk2::MyLangObj_lang_instance.resolveFont(&status, pFont, id) < 0) return false;
        if (*fontObj.setFont(&status, *pFont) < 0) return false;
        fontObj.reset_f10(&status);
        dk2::Bgraf white{0xFF, 0xFF, 0xFF, 0xFF, 0};
        fontObj.setColor(&status, &white);
        return true;
    }

}

bool patch::gui_scale::loadInGameFonts(uint32_t width, uint32_t height) {
    g_largestSize = 0;
    if (!*o_guiScale) return false;
    // the MyTextFont each font holds. not declared in the dkii api
    auto **pFont3 = (dk2::MyTextFont **) 0x00741560;
    auto **pFont2 = (dk2::MyTextFont **) 0x00741564;
    auto **pFont1 = (dk2::MyTextFont **) 0x00741568;
    struct {
        dk2::FontObj &fontObj;
        dk2::MyTextFont **pFont;
        uint32_t baseSize;
    } fonts[] = {
        {dk2::FontObj_3_instance, pFont3, 10},
        {dk2::FontObj_2_instance, pFont2, 14},
        {dk2::FontObj_1_instance, pFont1, 18},
    };
    for (auto &font : fonts) {
        int original = dk2::widthToFontId(font.baseSize, width);
        int id = fontId(font.baseSize, width, height);
        if (id != original && !loadFont(font.fontObj, font.pFont, id)) {
            // a language whose residx.dat doesn't have the bigger fonts
            patch::log::err("gui-scale: font %X not loaded, using the original %X", id, original);
            id = original;
        }
        if (id == original && !loadFont(font.fontObj, font.pFont, id)) {
            patch::log::err("gui-scale: font %X not loaded", id);
            continue;
        }
        g_largestSize = (std::max)(g_largestSize, sizeOf(id));
        patch::log::dbg("gui-scale: %ux%u font %u -> %d px (original %d px)",
                        width, height, font.baseSize, sizeOf(id), sizeOf(original));
    }
    return true;
}

int patch::gui_scale::glyphOverhang() {
    // 2 px was enough for the original fonts (up to 22 px). the overhang of a glyph grows with its size
    return (std::max)(2, g_largestSize / 8);
}
