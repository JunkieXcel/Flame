//
// Created by Alex on 29.10.2026.
//

#include "esc_goes_back.h"
#include <dk2/button/CClickButton.h>
#include "dk2_functions.h"
#include "dk2_globals.h"
#include "dk2/gui/main/main_layout.h"

namespace {

    // screens whose only way back is their OK button
    bool okIsBack(uint32_t windowId) {
        switch (windowId) {
            case MWID_Options:
            case MWID_Extras:
            case MWID_Credits:
            case MWID_ViewMovies:
            case MWID_TodaysTopKeepers:
                return true;
            default:
                return false;
        }
    }

    bool isBackButton(dk2::CWindow *win, dk2::CButton *btn) {
        if (!btn->f5D_isVisible || *(void ***) btn != dk2::CClickButton::vftable) return false;
        void *render = (void *) btn->f24_renderFun;
        if (render == (void *) dk2::CClickButton_renderExitBtn) return true;
        return render == (void *) dk2::CClickButton_renderApplyBtn && okIsBack(win->f40_id);
    }

    // the first open window with a Back button, walking the windows as
    // CGuiManager::probably_isCursorAtButton does
    dk2::CButton *findBackButton(dk2::CGuiManager &mgr) {
        for (dk2::CWindow *win = mgr.windowListEnd.f5E_next; win && win != &mgr.windowListEnd; win = win->f5E_next) {
            if (!win->f44_isCurrent) continue;
            for (dk2::CButton *btn = win->f66_buttons; btn; btn = btn->f78_next) {
                if (isBackButton(win, btn)) return btn;
            }
        }
        return nullptr;
    }

    // what CClickButton::handleClick does when the button is clicked
    void press(dk2::CButton *btn, dk2::CFrontEndComponent *frontend) {
        dk2::CWindow *win = btn->f51_pWindow;
        dk2::CGuiManager *mgr = win->f4A_c_gui_manager;
        if (btn->f49_leftClickHandler) {
            mgr->pbtn_A0 = btn;
            btn->f49_leftClickHandler(btn->f63_clickHandler_arg1, btn->f67_clickHandler_arg2,
                                      (dk2::CDefaultPlayerInterface *) frontend);
        }
        if (btn->f59__isExitOnClick) win->f44_isCurrent = 0;
        if (btn->f55__nextWindowIdOnClick) {
            dk2::CWindow *next = mgr->findGameWindowById(btn->f55__nextWindowIdOnClick);
            if (next) next->f44_isCurrent = 1;
        }
    }

}

void patch::esc_goes_back::window_proc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg != WM_KEYDOWN || wParam != VK_ESCAPE) return;
    if (lParam & (1 << 30)) return;  // held down: only the first press goes back
    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0) return;
    // the 3D main menu runs inside the game loop, so a player alone does not mean a level is played
    if (!dk2::MyResources_instance.gameCfg.useFe3d && dk2::CGameComponent_instance.gameSession.pPlayer) return;
    if (dk2::g_pMovieRenderer) return;  // the movie player takes Esc to skip the movie
    dk2::CFrontEndComponent &frontend = dk2::CFrontEndComponent_instance;
    // WM_KEYDOWN comes before the WM_CHAR with which CGuiManager::onWmChar cancels typing,
    // so typing is still on here: leave this Esc to cancel it
    if (frontend.cgui_manager.inputFieldTypingText) return;
    if (frontend.f30C1A) return;  // Options -> Control waits for a key to bind
    if (dk2::CButton *btn = findBackButton(frontend.cgui_manager)) press(btn, &frontend);
}
