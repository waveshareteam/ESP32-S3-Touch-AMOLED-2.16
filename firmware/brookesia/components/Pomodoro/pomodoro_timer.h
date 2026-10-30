/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POMO_LONG_BREAK_EVERY 4
#define POMO_MIN_SECONDS 5

typedef enum {
    POMO_FOCUS = 0,
    POMO_SHORT_BREAK,
    POMO_LONG_BREAK,
    POMO_PHASE_MAX,
} pomo_phase_t;

typedef struct {
    pomo_phase_t phase;
    bool running;
    uint32_t remaining_ms;
    uint32_t total_ms;
    int focus_done;
    uint32_t finished_seq;
    pomo_phase_t finished;
} pomo_state_t;

esp_err_t pomo_init(void);
void pomo_get_state(pomo_state_t *state);
void pomo_start(void);
void pomo_pause(void);
void pomo_reset(void);
void pomo_skip(void);
void pomo_select(pomo_phase_t phase);
int pomo_get_seconds(pomo_phase_t phase);
void pomo_set_seconds(pomo_phase_t phase, int seconds);

#ifdef __cplusplus
}
#endif
