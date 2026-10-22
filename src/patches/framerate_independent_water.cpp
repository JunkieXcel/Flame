//
// Created by Alex on 22.10.2026.
//

#include "framerate_independent_water.h"
#include "dk2/CPCEngineInterface.h"
#include "patches/logging.h"
#include <tools/flame_config.h>
#include <Windows.h>
#include <chrono>


flame_config::define_flame_option<bool> o_framerateIndependentWater(
    "flame:framerate-independent-water", flame_config::OG_Config,
    "Animate water/lava waves by real time instead of once per drawn frame\n"
    "Speed matches the game running with limit-tps=60 (one wave step per 16 ms)",
    true
);

namespace {

    // the dynamic height fields (water, lava) use CPCEngineInterface::updateCount as wave phase.
    // updateCount is incremented once per drawn frame and is also used to free unused mesh caches,
    // so instead of changing it we swap in our own counter while a height field is being built

    // dk2 advances the waves once per frame. with limit-tps=60 a frame is 16 ms
    constexpr int64_t c_stepUs = 16000;
    // don't let a stutter advance the waves more than 4 steps at once
    constexpr int64_t c_maxSteps = 4;

    bool g_waveInitialized = false;
    uint32_t g_waveCount = 0;
    int64_t g_waveLastUs = 0;
    int64_t g_waveAccumulatorUs = 0;

    uint32_t waveCount(dk2::CPCEngineInterface *engine) {
        int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (!g_waveInitialized) {
            // continue from the current wave phase so the waves don't jump
            g_waveInitialized = true;
            g_waveCount = engine->updateCount;
            g_waveLastUs = now;
            g_waveAccumulatorUs = 0;
            return g_waveCount;
        }
        int64_t elapsed = now - g_waveLastUs;
        g_waveLastUs = now;
        if (elapsed < 0) elapsed = 0;
        g_waveAccumulatorUs += elapsed;
        int64_t steps = g_waveAccumulatorUs / c_stepUs;
        if (steps > c_maxSteps) {
            steps = c_maxSteps;
            g_waveAccumulatorUs = 0;
        } else {
            g_waveAccumulatorUs -= steps * c_stepUs;
        }
        g_waveCount += (uint32_t) steps;
        return g_waveCount;
    }

    // replaces engine->updateCount with the wave counter for the lifetime of the scope
    struct WaveCountScope {
        dk2::CPCEngineInterface *engine;
        uint32_t frameCount;
        bool active;

        explicit WaveCountScope(dk2::CPCEngineInterface *engine) : engine(engine), frameCount(engine->updateCount) {
            active = *o_framerateIndependentWater;
            if (!active) {
                g_waveInitialized = false;  // resync when switched back on
                return;
            }
            engine->updateCount = waveCount(engine);
        }
        ~WaveCountScope() {
            if (active) engine->updateCount = frameCount;
        }
    };

    // vtable slots are __thiscall (this in ecx, callee cleans the stack).
    // __fastcall with an unused edx argument has the same layout

    int __fastcall createDynamicHeightField1(
            dk2::CPCEngineInterface *self, void *,
            uint32_t a2, uint32_t a3, int a4, int *a5, int a6, int a7, uint8_t *a8, int a9) {
        WaveCountScope scope(self);
        return self->createDynamicHeightField1(a2, a3, a4, a5, a6, a7, a8, a9);
    }

    int __fastcall createDynamicHeightField2(
            dk2::CPCEngineInterface *self, void *,
            uint32_t a2, int a3, int a4, int a5, int a6, int a7, uint8_t *a8, int a9) {
        WaveCountScope scope(self);
        return self->createDynamicHeightField2(a2, a3, a4, a5, a6, a7, a8, a9);
    }

    int __fastcall createDynamicHeightField3(
            dk2::CPCEngineInterface *self, void *,
            uint32_t a2, uint32_t a3, int a4, int a5, int a6, int a7, uint8_t *a8, int a9) {
        WaveCountScope scope(self);
        return self->createDynamicHeightField3(a2, a3, a4, a5, a6, a7, a8, a9);
    }

}

void patch::framerate_independent_water::init() {
    // CPCEngineInterface vtable slots 0xF8, 0xFC, 0x100 (v_fF8/v_fFC/v_f100_createDynamicHeightField1..3).
    // CBridge::createMeshes calls the height field builders only through these slots
    void **slots = &dk2::CPCEngineInterface::vftable[0xF8 / sizeof(void *)];
    DWORD oldProtect;
    if (!VirtualProtect(slots, 3 * sizeof(void *), PAGE_READWRITE, &oldProtect)) {
        patch::log::err("framerate_independent_water: VirtualProtect failed %d", GetLastError());
        return;
    }
    slots[0] = (void *) &createDynamicHeightField1;
    slots[1] = (void *) &createDynamicHeightField2;
    slots[2] = (void *) &createDynamicHeightField3;
    VirtualProtect(slots, 3 * sizeof(void *), oldProtect, &oldProtect);
}
