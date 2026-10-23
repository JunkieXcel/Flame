//
// Created by Alex on 23.10.2026.
//

#include "main_menu_load_game.h"
#include <dk2/gui/main/main_layout.h>
#include <tools/flame_config.h>


flame_config::define_flame_option<bool> o_mainMenuLoadGame(
    "flame:main-menu-load-game", flame_config::OG_Config,
    "Add Load Game to the main menu, under Single Player Game, and make room for it by spacing\n"
    "the main menu buttons a little closer. Its back button returns to the main menu",
    false
);

namespace {

    bool g_openedFromMainMenu = false;

}

bool patch::main_menu_load_game::enabled() {
    return *o_mainMenuLoadGame;
}

void patch::main_menu_load_game::onLoadGameOpened(int fromWindowId) {
    g_openedFromMainMenu = fromWindowId == MWID_Main;
}

bool patch::main_menu_load_game::backToMainMenu() {
    bool fromMainMenu = g_openedFromMainMenu;
    g_openedFromMainMenu = false;
    return fromMainMenu;
}
