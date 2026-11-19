/* SPDX-License-Identifier: MIT */
#ifndef LITHO_SCOREBOARD_H
#define LITHO_SCOREBOARD_H

#include "litho_handshake.h"

#define LITHO_SCORE_PHASES 128

int      litho_scoreboard_setup(struct litho_hs *hs, int workers);
uint64_t litho_scoreboard_run(struct litho_hs *hs);

#endif
