//
// Created by Alex on 22.10.2026.
//
#include "dk2/CRenderInfo.h"
#include "dk2/engine/CWorldEntry.h"
#include "tools/flame_config.h"
#include <chrono>
#include <intrin.h>


flame_config::define_flame_option<bool> o_framerateIndependentTorches(
    "flame:framerate-independent-torches", flame_config::OG_Config,
    "Advance the shared wall torch/candle flame animation by real time instead of once per drawn frame\n"
    "Speed matches the game running with limit-tps=60 (one step per 16 ms)",
    true
);

namespace {

    // CBridge::fun_43B220 steps the shared torch flame (CBridge+0x3BC) once per drawn frame
    // with `call CRenderInfo::fun_4B3AD0` at 0043B42A
    void *const c_bridgeTorchStepReturn = (void *) (0x0043B42A + 5);

    // dk2 steps the flame once per frame. with limit-tps=60 a frame is 16 ms
    constexpr int64_t c_stepUs = 16000;
    // don't let a stutter run more than 4 steps at once
    constexpr int c_maxSteps = 4;

    int64_t g_torchLastUs = 0;
    int64_t g_torchAccumulatorUs = 0;

    int torchStepsThisFrame() {
        int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t elapsed = g_torchLastUs ? now - g_torchLastUs : c_stepUs;
        g_torchLastUs = now;
        if (elapsed < 0) elapsed = 0;
        g_torchAccumulatorUs += elapsed;
        int steps = (int) (g_torchAccumulatorUs / c_stepUs);
        if (steps > c_maxSteps) {
            g_torchAccumulatorUs = 0;
            return c_maxSteps;
        }
        g_torchAccumulatorUs -= steps * c_stepUs;
        return steps;
    }

    void advanceAnimation(dk2::CRenderInfo *self) {
        self->fun_4B3B40();
        if (self->pWorldEntry->u.typeA == 6)
            self->fun_4B3C10();
        dk2::CWorldEntry *worldEntry = self->pWorldEntry;
        if ((worldEntry->u.weB_flags & 4) != 0 && worldEntry->u.typeA != 6) {
            self->_rangVal1 = self->_randVal2;
            self->_randVal2 += self->f40;
            int end = self->f3C;
            if (self->_randVal2 >= end) {
                if ((self->f4C & 2) != 0) {
                    self->_randVal2 = end;
                    ++self->f26;
                    self->f40 = 0;
                    return;
                }
                self->_randVal2 -= end;
            }
        }
        ++self->f26;
    }

}


// advance animation by one step
int dk2::CRenderInfo::fun_4B3AD0() {
    if (this->pWorldEntry == nullptr) return 0;
    if (*o_framerateIndependentTorches && _ReturnAddress() == c_bridgeTorchStepReturn) {
        int steps = torchStepsThisFrame();
        for (int i = 0; i < steps; ++i)
            advanceAnimation(this);
        return 0;
    }
    advanceAnimation(this);
    return 0;  // every caller ignores the return value
}
