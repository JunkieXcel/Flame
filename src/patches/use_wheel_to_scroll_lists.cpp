//
// Created by Alex on 28.10.2026.
//

#include "use_wheel_to_scroll_lists.h"
#include <Windows.h>
#include <dk2/button/CListBox.h>
#include <dk2/button/CVerticalSlider.h>
#include "dk2_globals.h"

namespace {

    constexpr int c_rowsPerNotch = 1;

    // high resolution wheels send fractions of a notch. scroll once a whole notch has built up
    int g_wheelRemainder = 0;
    dk2::CListBox *g_wheelList = nullptr;

    bool isA(dk2::CButton *btn, void **vftable) {
        return btn && *(void ***) btn == vftable;
    }

    // the list under the cursor, as DKII's CGuiManager::probably_isCursorAtButton finds buttons:
    // only open windows and visible buttons. the stored f45_containsCursor can be stale
    // (that function stops at the first hovered button), so the bounds are tested here.
    // a list's scroll position lives in its slider, the button created right after it
    dk2::CVerticalSlider *findSliderUnderCursor(dk2::CGuiManager &mgr, dk2::CListBox **outList) {
        if (!dk2::MyWindow_instance.dwWidth || !dk2::MyWindow_instance.dwHeight) return nullptr;
        // the GUI works in 2560x1920 whatever the screen size
        dk2::Pos2i pos;
        pos.x = mgr.mousePos.x * 2560 / (int) dk2::MyWindow_instance.dwWidth;
        pos.y = mgr.mousePos.y * 1920 / (int) dk2::MyWindow_instance.dwHeight;
        for (dk2::CWindow *win = mgr.windowListEnd.f5E_next; win && win != &mgr.windowListEnd; win = win->f5E_next) {
            if (!win->f44_isCurrent) continue;
            for (dk2::CButton *btn = win->f66_buttons; btn; btn = btn->f78_next) {
                if (!btn->f5D_isVisible || !isA(btn, dk2::CListBox::vftable)) continue;
                dk2::CButton *next = btn->f78_next;
                if (!isA(next, dk2::CVerticalSlider::vftable)) continue;
                if (!btn->containsPoint(&pos) && !(next->f5D_isVisible && next->containsPoint(&pos))) continue;
                *outList = (dk2::CListBox *) btn;
                return (dk2::CVerticalSlider *) next;
            }
        }
        return nullptr;
    }

}

bool patch::use_wheel_to_scroll_lists::scroll_list_under_cursor(int wheelDelta) {
    // in a game the Esc menu windows belong to the player interface, otherwise to the front end
    dk2::CGuiManager &mgr = dk2::CGameComponent_instance.gameSession.pPlayer
                            ? dk2::CDefaultPlayerInterface_instance.cgui_manager
                            : dk2::CFrontEndComponent_instance.cgui_manager;
    dk2::CListBox *list = nullptr;
    dk2::CVerticalSlider *slider = findSliderUnderCursor(mgr, &list);
    if (!slider) {
        g_wheelRemainder = 0;
        g_wheelList = nullptr;
        return false;
    }
    if (list != g_wheelList) {
        g_wheelRemainder = 0;
        g_wheelList = list;
    }
    g_wheelRemainder += wheelDelta;
    int notches = g_wheelRemainder / WHEEL_DELTA;  // rounds toward zero, the remainder keeps its sign
    g_wheelRemainder -= notches * WHEEL_DELTA;
    // wheel away from the user scrolls up, to the lower row numbers.
    // the slider's scroll-by keeps the top row within its range and moves the thumb
    if (notches) slider->v_fun_528FB0(-notches * c_rowsPerNotch);
    return true;
}
