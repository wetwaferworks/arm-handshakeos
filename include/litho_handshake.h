/* SPDX-License-Identifier: MIT */
#ifndef LITHO_HANDSHAKE_H
#define LITHO_HANDSHAKE_H

/*
 * LithoOS Timing Handshake (pure C11)
 *
 * Three pillars only: Time Serialization, control coupling, event coupling.
 * A fourth has not been received. This is not the Thread Handshake.
 *
 * Shape: one clock thread publishes phase windows; N worker threads pull.
 * The default seating is 3+1 (three workers and the clock).
 *
 * Four Gemstone beats in every phase: publish, pull, barrier, commit.
 *
 * Missed-window policy: WAIT (the phase extends; nothing is skipped).
 * Watchdog policy: FREEZE (default 250 ms). last_freeze names the offender;
 * reset_after_freeze bumps the generation so stale in-flight work dies.
 *
 * Workers do not decide when to run. They pull a consistent snapshot.
 * Commit folds a hash-chained ledger in component-id order, so the hash
 * is about the work and its order, never about which thread ran it.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#define LITHO_HS_MAX_WORKERS     8
#define LITHO_HS_MAX_COMPS       32
#define LITHO_HS_MAX_EVENTS      256
#define LITHO_HS_NAME_MAX        64
#define LITHO_HS_SAMPLES_MAX     8192
#define LITHO_HS_HIST_BINS       12
#define LITHO_HS_DEFAULT_CEILING_NS 250000000ull  /* 250 ms */
#define LITHO_HS_ALL_GROUPS      0xFFFFFFFFu

typedef enum {
    LITHO_CADENCE_EVERY_PHASE = 0,
    LITHO_CADENCE_HALF_CYCLE  = 1,
    LITHO_CADENCE_CYCLE_HEAD  = 2
} litho_cadence_t;

typedef enum {
    LITHO_BEAT_IDLE    = 0,
    LITHO_BEAT_PUBLISH = 1,
    LITHO_BEAT_PULL    = 2,
    LITHO_BEAT_BARRIER = 3,
    LITHO_BEAT_COMMIT  = 4
} litho_beat_t;

struct litho_published {
    uint64_t seq;
    uint64_t generation;
    uint64_t phase;
    uint64_t cycle;
    uint32_t group;
    uint64_t open_stamp_ns;
    bool     due_work;
};

struct litho_freeze {
    uint32_t component_id;
    char     component_name[LITHO_HS_NAME_MAX];
    uint64_t phase;
    uint64_t cycle;
    uint64_t open_stamp_ns;
    uint64_t trip_stamp_ns;
    uint64_t elapsed_ns;
    uint64_t ceiling_ns;
};

struct litho_hs_comp_desc {
    const char    *name;
    litho_cadence_t cadence;
    uint32_t       phase_divider; /* 0 = cadence decides; nonzero wins */
    uint32_t       phase_offset;
    bool           affine;        /* runs on the clock thread */
    bool           tick_critical; /* participates in barrier + watchdog */
    uint32_t       group;         /* 0 = every runPhase group */
};

/* Return value is folded into the ledger at commit, in component-id order. */
typedef uint64_t (*litho_hs_work_fn)(uint64_t phase, uint64_t cycle, void *user);

struct litho_hs_config {
    int      phases_per_cycle;     /* 1..64, default 4 */
    int      worker_threads;       /* 0 = 3 (the 3+1 seating) */
    uint64_t phase_budget_ns;      /* 0 = overrun accounting off */
    uint64_t watchdog_ceiling_ns;  /* 0 = 250 ms */
    uint32_t offender_threshold;   /* default 3 */
    bool     ledger;               /* default true */
};

struct litho_ctrl_event {
    uint64_t seq;
    uint64_t tick;
    uint32_t code;
    uint32_t value;
};

struct litho_comp_stats {
    char     name[LITHO_HS_NAME_MAX];
    uint64_t fires;
    uint64_t overruns;
    uint64_t max_work_ns;
    uint64_t max_overrun_ns;
    bool     repeat_offender;
};

struct litho_jitter_stats {
    uint64_t count;
    uint64_t period_ns;
    uint64_t ticks_done;     /* committed phases */
    uint64_t denied;
    uint64_t overruns;       /* phases whose wall time exceeded the budget */
    int64_t  p50_ns;
    int64_t  p95_ns;
    int64_t  p99_ns;
    int64_t  max_ns;
    int64_t  min_ns;
    int64_t  mean_ns;
    uint32_t hist[LITHO_HS_HIST_BINS];
};

struct litho_hs_stats {
    uint64_t phases;
    uint64_t overrun_phases;
    uint64_t overrun_ns_total;
    uint64_t barrier_wait_ns_total;
    uint64_t freezes;
    uint32_t n_comps;
    struct litho_comp_stats comps[LITHO_HS_MAX_COMPS];
    struct litho_jitter_stats jitter;
};

/* Internal component slot. Callers must not write these fields. */
struct litho_hs_slot {
    struct litho_hs_comp_desc desc;
    litho_hs_work_fn fn;
    void    *user;
    int      shard;
    char     name[LITHO_HS_NAME_MAX];
    uint32_t id;
    _Atomic uint64_t start_stamp_ns;
    _Atomic uint64_t duration_ns;
    _Atomic uint64_t note;
    _Atomic uint32_t started;
    _Atomic uint32_t finished;
    uint64_t fires;
    uint64_t overruns;
    uint64_t max_work_ns;
    uint64_t max_overrun_ns;
    uint64_t cycle_work_ns;
    bool     repeat_offender;
};

struct litho_hs {
    struct litho_hs_config cfg;
    uint64_t ceiling_ns;
    int      worker_count;
    int      n_comps;
    int      running;
    int      stop;
    struct litho_hs_slot comps[LITHO_HS_MAX_COMPS];

    /* Publication (clock thread writes, workers seqlock-read). */
    _Atomic uint64_t pub_seq;        /* seqlock: odd = write in progress */
    uint64_t pub_generation;
    uint64_t pub_phase;
    uint64_t pub_cycle;
    uint32_t pub_group;
    uint64_t pub_open_stamp_ns;
    uint64_t pub_logical_seq;        /* published phase sequence (even, >= 1) */

    _Atomic uint64_t generation;
    _Atomic uint32_t beat;
    _Atomic uint32_t frozen;
    _Atomic uint64_t commit_epoch;
    _Atomic uint64_t ledger_hash;
    _Atomic uint64_t monatomic;
    _Atomic uint32_t arrivals;
    _Atomic uint32_t expected;
    _Atomic uint32_t publish_word;   /* futex word for workers */
    _Atomic uint32_t barrier_word;   /* futex word for the clock thread */
    _Atomic uint32_t stop_word;

    struct litho_freeze last_freeze;

    /* Control coupling: stamped (seq, tick) stream, drained in PRE. */
    uint32_t n_events;
    uint32_t events_applied;
    struct litho_ctrl_event events[LITHO_HS_MAX_EVENTS];
    uint64_t control_fold;
    uint64_t event_fold;

    /* Jitter of phase open-to-commit (WAIT-extended duration). */
    uint32_t sample_count;
    int64_t  samples[LITHO_HS_SAMPLES_MAX];
    uint64_t overrun_phases;
    uint64_t overrun_ns_total;
    uint64_t barrier_wait_ns_total;
    uint64_t freeze_count;
    uint64_t phases_done;

    /* Worker threads (pthread, but the handshake itself has no mutex). */
    void    *worker_threads[LITHO_HS_MAX_WORKERS];
    uint64_t shard_seen[LITHO_HS_MAX_WORKERS];
    /* Boot args for worker threads (pointer stays valid for the thread's life). */
    struct {
        struct litho_hs *hs;
        uint32_t         shard;
    } boot[LITHO_HS_MAX_WORKERS];
};

void litho_hs_default_config(struct litho_hs_config *cfg);
int  litho_hs_init(struct litho_hs *hs, const struct litho_hs_config *cfg);
void litho_hs_fini(struct litho_hs *hs);

uint32_t litho_hs_register(struct litho_hs *hs,
                           const struct litho_hs_comp_desc *desc,
                           litho_hs_work_fn fn, void *user);

int  litho_hs_start(struct litho_hs *hs);
void litho_hs_stop(struct litho_hs *hs);

/* Clock thread: publish, wait at the barrier (WAIT / FREEZE), commit. */
bool litho_hs_run_phase(struct litho_hs *hs, uint64_t phase, uint64_t cycle, uint32_t group);

bool litho_hs_poll_phase(const struct litho_hs *hs, uint32_t shard,
                         uint64_t seen_seq, struct litho_published *out);
bool litho_hs_can_tick(const struct litho_hs *hs, uint32_t shard, uint64_t *phase_out);

bool litho_hs_frozen(const struct litho_hs *hs);
void litho_hs_last_freeze(const struct litho_hs *hs, struct litho_freeze *out);
void litho_hs_reset_after_freeze(struct litho_hs *hs);

uint64_t    litho_hs_commit_epoch(const struct litho_hs *hs);
uint64_t    litho_hs_ledger_hash(const struct litho_hs *hs);
uint64_t    litho_hs_monatomic(const struct litho_hs *hs);
litho_beat_t litho_hs_beat(const struct litho_hs *hs);
int         litho_hs_worker_count(const struct litho_hs *hs);

void litho_hs_stats(const struct litho_hs *hs, struct litho_hs_stats *out);
void litho_hs_jitter(const struct litho_hs *hs, struct litho_jitter_stats *out);

/* Control coupling: stamp events before start, or from the clock thread. */
int  litho_hs_post_event(struct litho_hs *hs, uint64_t tick, uint32_t code, uint32_t value);
/* Event coupling helper used at commit: apply matured events in (tick, seq) order. */
uint64_t litho_hs_apply_matured(struct litho_hs *hs, uint64_t cycle);

const char *litho_beat_name(litho_beat_t beat);
const char *litho_hist_bin_label(unsigned bin);
uint64_t    litho_now_ns(void);
uint64_t    litho_fnv1a64(uint64_t hash, uint64_t word);

#endif /* LITHO_HANDSHAKE_H */
