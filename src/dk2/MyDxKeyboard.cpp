//
// Created by DiaLight on 26.07.2024.
//
#include "dk2/MyDxKeyboard.h"
#include "dk2/ControlKeysUpdater.h"
#include "dk2/MouseRgbDxAction.h"
#include "patches/show_wireframe.h"
#include "patches/widescreen_fov.h"
#include "dk2_functions.h"
#include <tools/flame_config.h>

extern flame_config::define_flame_option<bool> o_keepRunningUnfocused;


int dk2::MyDxKeyboard::processKeyboardData(int a2) {
    int result = a2 - 1;
    // in windowed mode the keyboard is read in the background, so with keep-running-unfocused
    // the game would react to typing in other windows. drop key presses while another window
    // is in front; keep releases so a key held while switching away doesn't stay down
    bool dropPresses = *o_keepRunningUnfocused && GetForegroundWindow() != getHWindow();
    for (DIDEVICEOBJECTDATA *i = this->f2C_pdevObjArr; a2; --a2) {
        if (dropPresses && (i->dwData & 0x80)) {
            ++i;
            result = a2 - 1;
            continue;
        }
        MouseRgbDxAction *action = this->listKb.getOrCreateUnhandled();
        char f4_dwData = i->dwData;
        int f8_dwTimeStamp = i->dwTimeStamp;
        int f0_dwOfs = (unsigned __int8) i->dwOfs;
        action->btnPressFlags = ((unsigned int) i->dwData >> 4) & 8;
        action->pos.x = 0;
        action->KeyCode_F0toF3 = f0_dwOfs;
        action->pos.y = 0;
        action->data = f4_dwData;
        action->timestamp = f8_dwTimeStamp;
        action->isNotHandled = 1;
        this->f8_pcontrolkeys->v_fun4_keyboard((DxAction *) action);
        // hook::DIRECT_INPUT_KEYBOARD_DATA
        patch::show_wireframe::onKeyboard(i);
        patch::widescreen_fov::onKeyboard(i);
        ++i;
        result = a2 - 1;
    }
    return result;
}
