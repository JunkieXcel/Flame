//
// Created by Alex on 23.10.2026.
//

#ifndef FLAME_MAIN_MENU_LOAD_GAME_H
#define FLAME_MAIN_MENU_LOAD_GAME_H

#include <cstdint>

namespace patch::main_menu_load_game {

    // flame:main-menu-load-game. read when the main menu layout is built, once per run
    bool enabled();

    // called by CButton_handleLeftClick_538000 when Load Game is opened (action 7), with the window it is opened from
    void onLoadGameOpened(int fromWindowId);

    // called by CButton_handleLeftClick_changeMenu when Load Game is left with its back button (command 19).
    // true if it was opened from the main menu: go back there instead of to Single Player
    bool backToMainMenu();

}


#endif // FLAME_MAIN_MENU_LOAD_GAME_H
