//
// Created by DiaLight on 4/1/2025.
//

#include <dk2_functions.h>
#include <memory>
#include <vector>
#include <dk2/button/button_types.h>
#include <dk2/gui/ButtonCfg.h>
#include <dk2/gui/WindowCfg.h>
#include <patches/gui/main/main_menu_load_game.h>
#include "main_layout.h"

namespace {
    std::vector<dk2::ButtonCfg> buttons;
    std::unique_ptr<dk2::WindowCfg> window;

    // the click buttons' y and height. with flame:main-menu-load-game seven buttons fit
    // between the title (ends at 472) and the version string (starts at 1740) on a 164 pitch
    struct MainButtonsY {
        int16_t singlePlayer, loadGame, multiplayer, myPetDungeon, options, extras, quit;
        uint16_t h;
    };
    constexpr MainButtonsY c_original {492, 0, 672, 852, 1112, 1292, 1568, 172};
    constexpr MainButtonsY c_withLoadGame {492, 656, 820, 984, 1196, 1360, 1572, 156};
}

dk2::WindowCfg *Main_Main_layout() {
    if (window) return window.get();
    buttons.emplace_back() = {   // dungeon Keeper 2 title
        BT_CTextBox, 590, 0, NULL, NULL, 0, 0, 0x00000000, 0x00000000, 0,
        628, 0, 1304, 472, 0, 0, 0, 0, 0, NULL, dk2::CButton_render_5367D0, NULL, 0, 0x00000000, 0x00000000, 0
    };
    buttons.emplace_back() = { // Warning: you are very low on a hard drive space
        BT_CTextBox, 688, 0, NULL, NULL, 0, 0, 0x00000000, 0x00000000, 0,
        0, 1740, 2560, 160, 0, 0, 0, 0, 0, NULL, dk2::CButton_render_537CA0, NULL, 0, 0x00000000, 0x00000000, 0
    };
    buttons.emplace_back() = { // version string
        BT_CTextBox, 689, 0, NULL, NULL, 0, 0, 0x00000000, 0x00000000, 0,
        0, 1740, 2560, 160, 0, 0, 0, 0, 0, NULL, dk2::CTextBox_renderVersion, NULL, 0, 0x00000000, 0x00000000, 0
    };

    bool loadGame = patch::main_menu_load_game::enabled();
    const MainButtonsY &y = loadGame ? c_withLoadGame : c_original;
    buttons.emplace_back() = { // Single Player game
        BT_CClickButton, 1, 0, dk2::CButton_handleLeftClick_5394E0, NULL, 0, 0, MAKELONG(0x0001, MWID_SinglePlayer), 0x00000000, 0,
        628, y.singlePlayer, 1304, y.h, 0, 0, 1304, y.h, 0, NULL, dk2::CClickButton_render_532670, NULL, 71, 0x00000000, 0x00040002, 32
    };
    buttons.emplace_back() = { // Multiplayer game
        BT_CClickButton, 2, 0, dk2::CButton_handleLeftClick_538000, NULL, 0, 0, 0x00000001, 0x00010002, 0,
        628, y.multiplayer, 1304, y.h, 0, 0, 1304, y.h, 0, NULL, dk2::CClickButton_render_532670, NULL, 72, 00000001, 0x00040002, 32
    };
    buttons.emplace_back() = { // My Pet Dungeon
        BT_CClickButton, 10, 0, dk2::CButton_handleLeftClick_538000, NULL, 0, 0, 0x00000001, 0x0002000B, 0,
        628, y.myPetDungeon, 1304, y.h, 0, 0, 1304, y.h, 0, NULL, dk2::CClickButton_render_532670, NULL, 1675, 00000002, 0x00040002, 32
    };
    buttons.emplace_back() = { // Options
        BT_CClickButton, 3, 0, dk2::CButton_handleLeftClick_538000, NULL, 0, 0, 0x00000001, 0x00030005, 0,
        628, y.options, 1304, y.h, 0, 0, 1304, y.h, 0, NULL, dk2::CClickButton_render_532670, NULL, 94, 00000003, 0x00040002, 32
    };
    buttons.emplace_back() = { // Extras
        BT_CClickButton, 4, 0, dk2::CButton_handleLeftClick_538000, NULL, 0, 0, 0x00000001, 0x00040006, 0,
        628, y.extras, 1304, y.h, 0, 0, 1304, y.h, 0, NULL, dk2::CClickButton_render_532670, NULL, 73, 00000004, 0x00040002, 32
    };
    buttons.emplace_back() = { // Quit
        BT_CClickButton, 5, 0, dk2::CButton_handleLeftClick_538000, NULL, 0, 0, 0x00000001, 0x00050008, 0,
        628, y.quit, 1304, y.h, 0, 0, 1304, y.h, 0, NULL, dk2::CClickButton_render_532670, NULL, 28, 00000005, 0x00040002, 32
    };
    if (loadGame) {
        // Load Game, shown under Single Player. last in the list so it is baked as button 6 and the others
        // keep their indices (the front end has no keyboard navigation that follows the list order).
        // action 7 as Single Player's Load Game: CButton_handleLeftClick_538000 hides the main menu and opens it
        buttons.emplace_back() = {
            BT_CClickButton, 7, 0, dk2::CButton_handleLeftClick_538000, NULL, 0, 0, 0x00000001, 0x00060007, 0,
            628, y.loadGame, 1304, y.h, 0, 0, 1304, y.h, 0, NULL, dk2::CClickButton_render_532670, NULL, 145, 00000006, 0x00040002, 32
        };
    }

    buttons.emplace_back() = EngOfButtonList;

    window = std::make_unique<dk2::WindowCfg>();
    *window = {
        MWID_Main, 0, 0, 0, 0, 2560, 1920, 0, 0, 2560, 1920, 0, NULL, NULL, 0,
        0, 0, 0, 0, 0, buttons.data(), 1
    };
    return window.get();
}

