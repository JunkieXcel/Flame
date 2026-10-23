//
// Created by Alex on 23.10.2026.
//

#include "gui_scale.h"
#include "dk2/MyLangObj.h"
#include "dk2/MySurface.h"
#include "dk2/text/render/FontObj.h"
#include "dk2/utils/Bgraf.h"
#include "dk2_functions.h"
#include "dk2_globals.h"
#include "patches/logging.h"
#include <tools/flame_config.h>
#include <Windows.h>
#include <algorithm>
#include <climits>


flame_config::define_flame_option<bool> o_guiScale(
    "flame:gui-scale", flame_config::OG_Config,
    "Pick the in-game fonts (menus, panels, tooltips, messages) by the screen size, as DKII does up to 1024x768.\n"
    "DKII knows only the widths 400, 512, 640, 800 and 1024: any other width, 1920 or 3840 too, gets the 640x480 fonts.\n"
    "Uses the bigger fonts DKII ships (up to 72 px) but never picked.\n"
    "Also picks the front end fonts (main menu, load/save, multiplayer, campaign) by the menu size (flame:menu-res),\n"
    "which DKII made for 640x480 only, and makes the rows of its lists as much taller",
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

namespace {

    // the nine front-end fonts as loadFonts loads them, smallest first. FONT_FRONTEND<size>.bf4 has the same bytes
    // as FONT_INGAME<size>.bf4, so a bigger size is taken from c_fonts
    struct FrontEndFont {
        dk2::FontObj &fontObj;
        dk2::MyTextFont *&pFont;  // holds a reference, released by releaseFonts
        int size;
        int originalId;
    };
    FrontEndFont c_frontEndFonts[] = {
        {dk2::g_FontObj1_instance, dk2::g_pMyFontText1, 10, 0x454},
        {dk2::g_FontObj2_instance, dk2::g_pMyFontText2, 12, 0x455},
        {dk2::g_FontObj3_instance, dk2::g_pMyFontText3, 14, 0x456},
        {dk2::g_FontObj4_instance, dk2::g_pMyFontText4, 16, 0x44A},
        {dk2::g_FontObj5_instance, dk2::g_pMyFontText5, 18, 0x44B},
        {dk2::g_FontObj6_instance, dk2::g_pMyFontText6, 20, 0x457},
        {dk2::g_FontObj7_instance, dk2::g_pMyFontText7, 24, 0x44E},
        {dk2::g_FontObj8_instance, dk2::g_pMyFontText8, 28, 0x458},
        {dk2::g_FontObj9_instance, dk2::g_pMyFontText9, 36, 0x459},
    };
    constexpr int c_frontEndFontCount = sizeof(c_frontEndFonts) / sizeof(c_frontEndFonts[0]);

    // the id each front-end font holds now. loadFonts loaded the original ones
    int g_frontEndIds[c_frontEndFontCount] = {0x454, 0x455, 0x456, 0x44A, 0x44B, 0x457, 0x44E, 0x458, 0x459};

    // the baked menus grow with the fonts (bakeButton allocates them without checking). below this much free
    // address space the front end keeps its original fonts
    constexpr DWORDLONG c_minFreeAddressSpace = 256ull * 1024 * 1024;

    // the biggest shipped size that isn't bigger than size
    int snapSize(float size) {
        int snapped = c_fonts[0].size;
        for (auto &f : c_fonts) {
            if ((float) f.size <= size) snapped = f.size;
        }
        return snapped;
    }

    int idOf(int size) {
        for (auto &f : c_fonts) {
            if (f.size == size) return f.id;
        }
        return 0;
    }

    // the front end was made for 640x480 and is stretched as the in-game GUI is, so scale its sizes by the smaller
    // of width / 640 and height / 480. the shipped sizes thin out at the top (28, 36, 48, 72), so scaled and rounded
    // down some would land on the same font. walk down from the biggest and keep each one smaller than the one above
    // it: the nine stay apart, the small ones give way. never smaller than the original size
    void frontEndSizes(uint32_t width, uint32_t height, int *sizes) {
        float scale = (std::max)(1.0f, (std::min)((float) width / 640.0f, (float) height / 480.0f));
        int above = INT_MAX;
        for (int i = c_frontEndFontCount - 1; i >= 0; --i) {
            int original = c_frontEndFonts[i].size;
            int size = snapSize((float) original * scale);
            if (size >= above) size = snapSize((float) (above - 1));
            sizes[i] = (std::max)(size, original);  // the originals are apart already, so this keeps them apart
            above = sizes[i];
        }
    }

    void releaseFont(dk2::MyTextFont *font) {
        using release_t = int (__thiscall *)(dk2::MyTextFont *);  // MyTextFont::v_release, vftable slot 0
        ((release_t) (*(void ***) font)[0])(font);
    }

    // as loadFonts does for one font, and without leaking the font it held before
    bool setFrontEndFont(FrontEndFont &font, int id) {
        int status;
        dk2::MyTextFont *newFont = nullptr;
        if (*dk2::MyLangObj_lang_instance.resolveFont(&status, &newFont, id) < 0 || newFont == nullptr) return false;
        if (*font.fontObj.setFont(&status, newFont) < 0) {  // releases what the FontObj held, adds a reference
            releaseFont(newFont);
            return false;
        }
        dk2::Bgraf white{0xFF, 0xFF, 0xFF, 0xFF, 0};
        font.fontObj.setColor(&status, &white);
        font.fontObj.checkFlag8(&status);
        dk2::MyTextFont *oldFont = font.pFont;
        font.pFont = newFont;
        if (oldFont) releaseFont(oldFont);
        return true;
    }

}

void patch::gui_scale::loadFrontEndFonts(uint32_t width, uint32_t height) {
    int sizes[c_frontEndFontCount];
    for (int i = 0; i < c_frontEndFontCount; ++i) sizes[i] = c_frontEndFonts[i].size;
    if (*o_guiScale) {
        MEMORYSTATUSEX mem{sizeof(mem)};
        GlobalMemoryStatusEx(&mem);
        if (mem.ullAvailVirtual < c_minFreeAddressSpace) {
            patch::log::err("gui-scale: only %lluMB address space free, front end keeps its original fonts",
                            mem.ullAvailVirtual / (1024 * 1024));
        } else {
            frontEndSizes(width, height, sizes);
        }
    }
    for (int i = 0; i < c_frontEndFontCount; ++i) {
        FrontEndFont &font = c_frontEndFonts[i];
        int id = sizes[i] == font.size ? font.originalId : idOf(sizes[i]);
        if (id == g_frontEndIds[i]) continue;  // shown again at the same size: keep it
        if (!setFrontEndFont(font, id)) {
            // a language whose residx.dat doesn't have the bigger fonts
            patch::log::err("gui-scale: front-end font %X not loaded, using the original %X", id, font.originalId);
            id = font.originalId;
            if (id == g_frontEndIds[i] || !setFrontEndFont(font, id)) continue;
        }
        g_frontEndIds[i] = id;
        patch::log::dbg("gui-scale: menu %ux%u front-end font %d -> %d px", width, height, font.size, sizes[i]);
    }
}

bool patch::gui_scale::checkBakedSurface(dk2::MySurface &surf, unsigned int x16Idx) {
    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);
    unsigned int kb = (unsigned int) ((int64_t) surf.size.h * surf.lPitch / 1024);
    if (surf.lpSurface == nullptr) {
        patch::log::err("gui-scale: baked menu %u (%dx%d, %u KB) not allocated, %lluMB address space free",
                        x16Idx, surf.size.w, surf.size.h, kb, mem.ullAvailVirtual / (1024 * 1024));
        return false;
    }
    patch::log::dbg("gui-scale: baked menu %u %dx%d %u KB, %lluMB address space free",
                    x16Idx, surf.size.w, surf.size.h, kb, mem.ullAvailVirtual / (1024 * 1024));
    return true;
}

int patch::gui_scale::listItemHeight(int itemHeight, uint32_t menuHeight) {
    if (!*o_guiScale || menuHeight <= 480) return itemHeight;
    // rows follow the list box, which is stretched by menuHeight / 480 (CGuiManager::scaleAabb_2560_1920)
    return (int) ((int64_t) itemHeight * menuHeight / 480);
}

int patch::gui_scale::listColumnX(int x, uint32_t menuWidth) {
    if (!*o_guiScale || menuWidth <= 640) return x;
    // rows follow the list box, which is stretched by menuWidth / 640 (CGuiManager::scaleAabb_2560_1920)
    return (int) ((int64_t) x * menuWidth / 640);
}

namespace {

    // a constant in a DKII list renderer: an offset from the row's top left, in pixels of 640x480.
    // found by disassembling them, at the immediate or displacement of the instruction in the comment
    struct ListConstant {
        uintptr_t address;
        int size;  // 1: signed imm8, 4: imm32/disp32
        int32_t original;
        bool vertical;
    };
    constexpr ListConstant c_listConstants[] = {
        // CListBox_LoadGame_SaveList_render: name, date and time columns
        {0x0054F406, 1, 0x1B, false},  // 0054F404 add ebx, 0x1b      name column, after the slot number
        {0x0054F428, 4, 0xFD, false},  // 0054F427 mov ebp, 0xfd      name end
        {0x0054F4EF, 4, 0xFD, false},  // 0054F4EE mov edi, 0xfd      date start
        {0x0054F4F4, 4, 0x170, false},  // 0054F4F3 mov ebp, 0x170    date end
        {0x0054F508, 4, 0x170, false},  // 0054F507 mov edi, 0x170    time start
        {0x0054F50D, 4, 0x1E8, false},  // 0054F50C mov ebp, 0x1e8    time end
        // CButton_render_540BC0: multiplayer map select
        {0x00540E4A, 4, 0xAF, false},  // 00540E49 add eax, 0xaf
        {0x00540EE7, 4, 0x93, false},  // 00540EE5 lea edx, [eax + 0x93]
        {0x00540EEC, 4, 0xE2, false},  // 00540EEB add eax, 0xe2
        {0x00540F9A, 4, 0xDA, false},  // 00540F98 lea ecx, [eax + 0xda]
        {0x00540F9F, 4, 0x13D, false},  // 00540F9E add eax, 0x13d
        // CButton_render_5410C0: skirmish map select, the same code
        {0x0054134A, 4, 0xAF, false},  // 00541349 add eax, 0xaf
        {0x005413E7, 4, 0x93, false},  // 005413E5 lea edx, [eax + 0x93]
        {0x005413EC, 4, 0xE2, false},  // 005413EB add eax, 0xe2
        {0x0054149A, 4, 0xDA, false},  // 00541498 lea ecx, [eax + 0xda]
        {0x0054149F, 4, 0x13D, false},  // 0054149E add eax, 0x13d
        // CButton_render_542110: my pet dungeon other levels, custom campaign
        {0x00542365, 4, 0xAF, false},  // 00542364 add eax, 0xaf
        {0x005423F4, 4, 0xDA, false},  // 005423F2 lea ecx, [eax + 0xda]
        {0x005423F9, 4, 0x13D, false},  // 005423F8 add eax, 0x13d
        // CListBox_render_54BE30: options, control keys. 0x12 is the height of a row's text box
        {0x0054C0C1, 4, 0x130, false},  // 0054C0C0 add eax, 0x130
        {0x0054C0D0, 1, 0x12, true},  // 0054C0CE add ecx, 0x12
        {0x0054C132, 4, 0x132, false},  // 0054C130 lea ecx, [eax + 0x132]
        {0x0054C137, 4, 0x1C6, false},  // 0054C136 add eax, 0x1c6
        {0x0054C14D, 1, 0x12, true},  // 0054C14B add ecx, 0x12
        {0x0054C1A8, 1, 0x12, true},  // 0054C1A6 add eax, 0x12
        {0x0054C1AB, 4, 0x1C6, false},  // 0054C1A9 add ecx, 0x1c6
    };
    constexpr int c_listConstantCount = sizeof(c_listConstants) / sizeof(c_listConstants[0]);

    // what each constant holds now: the original until scaleListColumns rewrites it
    int32_t g_listConstantValues[c_listConstantCount] = {};
    bool g_listConstantsChecked = false;
    bool g_listConstantsUsable = false;

    int32_t readConstant(const ListConstant &c) {
        if (c.size == 1) return *(const int8_t *) c.address;
        return *(const int32_t *) c.address;
    }

}

void patch::gui_scale::scaleListColumns(uint32_t menuWidth, uint32_t menuHeight) {
    if (!g_listConstantsChecked) {
        // the code must be the DKII it was found in. if one constant is not, leave them all
        g_listConstantsChecked = true;
        g_listConstantsUsable = true;
        for (int i = 0; i < c_listConstantCount; ++i) {
            g_listConstantValues[i] = readConstant(c_listConstants[i]);
            if (g_listConstantValues[i] != c_listConstants[i].original) {
                patch::log::err("gui-scale: list column constant at %08X is %X, not %X. list columns are not scaled",
                                c_listConstants[i].address, g_listConstantValues[i], c_listConstants[i].original);
                g_listConstantsUsable = false;
            }
        }
    }
    if (!g_listConstantsUsable) return;
    for (int i = 0; i < c_listConstantCount; ++i) {
        const ListConstant &c = c_listConstants[i];
        int32_t value = c.vertical ? listItemHeight(c.original, menuHeight) : listColumnX(c.original, menuWidth);
        if (c.size == 1 && value > INT8_MAX) {
            patch::log::err("gui-scale: list column constant at %08X needs %d, more than a byte holds", c.address, value);
            value = INT8_MAX;
        }
        if (value == g_listConstantValues[i]) continue;
        if (readConstant(c) != g_listConstantValues[i]) {
            patch::log::err("gui-scale: list column constant at %08X was changed by something else", c.address);
            continue;
        }
        DWORD oldProtect;
        if (!VirtualProtect((void *) c.address, c.size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            patch::log::err("gui-scale: VirtualProtect %08X failed %d", c.address, GetLastError());
            continue;
        }
        if (c.size == 1) *(int8_t *) c.address = (int8_t) value;
        else *(int32_t *) c.address = value;
        VirtualProtect((void *) c.address, c.size, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (void *) c.address, c.size);
        g_listConstantValues[i] = value;
    }
    patch::log::dbg("gui-scale: menu %ux%u list columns x%.2f", menuWidth, menuHeight,
                    (float) listColumnX(640, menuWidth) / 640.0f);
}
