/* SPDX-License-Identifier: MIT */
#include "litho_scoreboard.h"

#include <string.h>

static uint64_t work_pure(uint64_t phase, uint64_t cycle, uint64_t salt)
{
    uint64_t h = 0xcbf29ce484222325ull;
    h = litho_fnv1a64(h, phase);
    h = litho_fnv1a64(h, cycle);
    h = litho_fnv1a64(h, salt);
    return h;
}

static uint64_t work_serial(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xA11ull);
}

static uint64_t work_control(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xC0u);
}

static uint64_t work_a(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xA0ull);
}

static uint64_t work_b(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xB0ull);
}

static uint64_t work_c(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return work_pure(phase, cycle, 0xC1ull);
}

static void load_events(struct litho_hs *hs)
{
    uint64_t tick;

    for (tick = 0; tick < 32; tick++) {
        if ((tick % 3) == 0) {
            (void)litho_hs_post_event(hs, tick, (uint32_t)(tick * 7u + 1u),
                                      (uint32_t)(tick + 11u));
        }
    }
}

int litho_scoreboard_setup(struct litho_hs *hs, int workers)
{
    struct litho_hs_config cfg;
    struct litho_hs_comp_desc d;

    litho_hs_default_config(&cfg);
    cfg.worker_threads = workers;
    cfg.phases_per_cycle = 4;
    cfg.phase_budget_ns = 2000000ull;
    cfg.watchdog_ceiling_ns = LITHO_HS_DEFAULT_CEILING_NS;
    cfg.ledger = true;
    if (litho_hs_init(hs, &cfg) != 0) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "serial";
    d.cadence = LITHO_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    if (litho_hs_register(hs, &d, work_serial, NULL) == UINT32_MAX) {
        return -1;
    }
    memset(&d, 0, sizeof(d));
    d.name = "control";
    d.cadence = LITHO_CADENCE_EVERY_PHASE;
    d.affine = true;
    d.tick_critical = true;
    if (litho_hs_register(hs, &d, work_control, NULL) == UINT32_MAX) {
        return -1;
    }
    memset(&d, 0, sizeof(d));
    d.name = "worker-a";
    d.cadence = LITHO_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    if (litho_hs_register(hs, &d, work_a, NULL) == UINT32_MAX) {
        return -1;
    }
    memset(&d, 0, sizeof(d));
    d.name = "worker-b";
    d.cadence = LITHO_CADENCE_HALF_CYCLE;
    d.tick_critical = true;
    if (litho_hs_register(hs, &d, work_b, NULL) == UINT32_MAX) {
        return -1;
    }
    memset(&d, 0, sizeof(d));
    d.name = "worker-c";
    d.cadence = LITHO_CADENCE_CYCLE_HEAD;
    d.tick_critical = true;
    if (litho_hs_register(hs, &d, work_c, NULL) == UINT32_MAX) {
        return -1;
    }
    load_events(hs);
    return litho_hs_start(hs);
}

uint64_t litho_scoreboard_run(struct litho_hs *hs)
{
    uint64_t phase, cycle;

    for (phase = 0; phase < LITHO_SCORE_PHASES; phase++) {
        cycle = phase / 4ull;
        if (!litho_hs_run_phase(hs, phase, cycle, 0)) {
            return 0;
        }
    }
    return litho_hs_ledger_hash(hs);
}
