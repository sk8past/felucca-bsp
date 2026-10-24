/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Peter Whitney */
/* SMK-37 input HAL stub (system part, Task 2).
 *
 * PURPOSE: This file's primary job in the system part is to pull in
 * fm1_flash_map.h so that the SMK-37 flash address constants are defined
 * BEFORE fm1_flash.h (chip/) sees them via unity.c's include order:
 *
 *   unity.c includes:
 *     bsp/boards/smk37/fm1_input.h   ← this file  (board, first)
 *       └─ fm1_flash_map.h            ← overrides FL_DATA_LO, FL_PLAIN_WINDOW_LO …
 *     bsp/chip/fm1_flash.h            ← respects #ifndef guards (chip, second)
 *
 * The full hardware implementation (key matrix 4×74HC595 on SPI2-C,
 * pad LEDs, faders, wheels) is Task 3 (board part).  Until then these
 * stubs let the device toolchain compile a skeleton binary.
 *
 * SMK-37 matrix wiring (from docs/01-hardware.md):
 *   4×74HC595 SPI2 group C: CLK PA3, MOSI PA4, latch PA1
 *   Row returns: PA2 PA5 PA6 PA7 PA9 PH6 PH8 PH9
 *   CRITICAL: PA9 PH6 PH8 PH9 are INPUTS — never drive as outputs.
 *             PH8 MUST NEVER be UART RX (TRS MIDI lost on SMK).
 *   37 piano keys (F2–C6), 16 pads, 18 buttons, 8 encoders (K1-K8),
 *   4 faders, pitch wheel, mod wheel.
 */
#pragma once
#include <stdint.h>

/* ---- flash map override (MUST come before fm1_flash.h) ---- */
#include "fm1_flash_map.h"

/* The remaining board-layer headers are included here so unity.c's single
 * inclusion of fm1_input.h pulls in the full board layer. */
#include "fm1_gpio.h"
#include "fm1_time.h"
#include "fm1_adc.h"

/* ---- matrix / key layout constants ----
 * Task 3 will update FM1_NKEY, FM1_NCOL, FM1_NENC to match the SMK-37
 * hardware.  The FM-1 numbers are used as placeholders so the shared app
 * code compiles unchanged against this stub. */
#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_INPUT_ANALOG
#define FM1_INPUT_ANALOG(col, rowbit, v, driven) ((void)0)
#endif
#ifndef FM1_INPUT_DRIVE
#define FM1_INPUT_DRIVE(col, rowbit) 0
#endif
#ifndef FM1_INPUT_PROBE
#define FM1_INPUT_PROBE(col, rowbit, v, pressed) ((void)0)
#endif
#ifndef FM1_INPUT_NOW
#define FM1_INPUT_NOW() fm1_ticks()
#endif
#ifndef FM1_LED_DIM_NS
#define FM1_LED_DIM_NS 3200u
#endif
#ifndef FM1_LED_DIM_LO_NS
#define FM1_LED_DIM_LO_NS 1600u
#endif
#ifndef FM1_LED_DIM_DIV
#define FM1_LED_DIM_DIV 1u
#endif
#define FM1_LED_BREATH_FRAMES 1024u
#ifndef FM1_LED_BREATH_PK
#define FM1_LED_BREATH_PK 154u
#endif
#ifndef FM1_LED_BREATH_PK_LO
#define FM1_LED_BREATH_PK_LO 77u
#endif
#ifndef FM1_LED_MID_N
#define FM1_LED_MID_N 8u
#endif
#ifndef FM1_LED_MID_N_LO
#define FM1_LED_MID_N_LO 12u
#endif
#ifndef FM1_LED_TICK_US
#define FM1_LED_TICK_US 100u
#endif
#ifndef FM1_LED_TRACE
#define FM1_LED_TRACE(rowmask) ((void)0)
#endif
#ifndef FM1_SR_LATCH_TRACE
#define FM1_SR_LATCH_TRACE() ((void)0)
#endif
#define FM1_DEB_PRESS   2u
#define FM1_DEB_RELEASE 8u
#define FM1_INPUT_LAT   1
#define FM1_SETTLE_US   10u
#define FM1_REST_FRAMES 900u
#define FM1_ENC_GO      20u

/* TODO Task 3: update these for the SMK-37 layout.
 * SMK-37 will need: NCOL = 16 (4×595), NKEY = 37+pads+buttons, NENC = 8 */
#define FM1_NCOL 11u    /* placeholder: matches FM-1, Task 3 will correct */
#define FM1_NKEY 41u    /* placeholder */
#define FM1_NENC 8u     /* SMK has 8 encoders (K1-K8) vs FM-1's 7 */

enum { FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1 };
#ifndef FM1_INPUT_LED_OK
#define FM1_INPUT_LED_OK(col, rowbit) 1
#endif

/* TODO Task 3: replace with the real SMK-37 keymap (4×595, 8-row returns).
 * Placeholder mirrors the FM-1 shape so app code that indexes [row][col]
 * compiles without change. */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
/* TODO Task 3: SMK encoder wiring (K1-K8 on the 595 chain). */
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0,0,1,0},{2,0,3,0},{8,1,9,1},{8,0,9,0},{6,0,7,0},{4,0,5,0},{0,5,1,5},{2,5,3,5},
};

/* ---- fm1_in: main state (same shape as FM-1 so app code compiles) ---- */
static volatile struct {
    uint32_t notes;              /* debounced note-key bits */
    uint32_t buttons;            /* debounced button bits */
    uint32_t pressed, released;  /* button edges since fm1_input_edges() */
    uint32_t notes_pressed;      /* note-key press edges since fm1_input_note_edges() */
    uint8_t  raw[FM1_NCOL];
    uint8_t  cnt[FM1_NKEY];
    uint32_t note_t0[37];        /* SMK has 37 piano keys */
    uint8_t  enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t  enc_rest[FM1_NENC];
    uint16_t enc_still[FM1_NENC];
    int8_t   enc_sub[FM1_NENC];
    int16_t  enc_steps[FM1_NENC];
    int8_t   enc_dir[FM1_NENC];
    uint32_t enc_dfr[FM1_NENC];
    uint32_t frames;
} fm1_in;

static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[FM1_NCOL];
static uint8_t fm1_led_breath[FM1_NCOL];
static uint8_t fm1_led_mid[FM1_NCOL];

/* ---- fm1_an: ADC state ---- */
static volatile struct {
    uint16_t con;                /* FM1_ADC_CON; 0 = analog off */
    uint8_t  drive, probe;
    uint8_t  pend, col, rowbit;
    uint8_t  driven, probing;
    uint8_t  phase;
    uint16_t master;             /* virtual MASTER (K1 encoder accumulates here) */
    uint16_t faders[4];          /* SMK: 4 faders → CC16-19 */
    uint16_t timeouts;
} fm1_an;

/* ---- scan diagnostics (same layout as FM-1) ---- */
#define FM1_GAP_BINS 8u
static volatile struct {
    uint32_t last, gap_max, gap_hist[FM1_GAP_BINS];
    uint32_t on[FM1_NCOL], on_max[FM1_NCOL];
    uint32_t cost_sum, cost_max, ticks;
    uint32_t enc_moves[FM1_NENC], enc_lost[FM1_NENC];
    uint32_t press_n, press_sum, press_max;
    uint32_t kb_n, kb_sum, kb_max, dac_sum, dac_max;
} fm1_in_diag;

/* ---- fm1_led_anim: power-on sweep (same interface) ---- */
#include "fm1_led_anim.h"

/* ---- API: stubs — replaced by real SPI+595 driver in Task 3 ---- */

static void fm1_input_init(void)
{
    /* TODO Task 3: configure SPI2 group C (CLK PA3, MOSI PA4, latch PA1),
     * PA2/PA5/PA6/PA7 as inputs with pull-ups, PA9/PH6/PH8/PH9 as inputs
     * (NEVER drive these as outputs — they are matrix row returns).
     * Configure the 4 fader ADC channels and wheels. */
}

static void fm1_input_tick(void)
{
    /* TODO Task 3: advance one column of the 4×74HC595 matrix, debounce,
     * sample the ADC row if pending, service the faders once per frame. */
}

static void fm1_input_scan(void)
{
    /* TODO Task 3: blocking full-frame scan (used in polling / host mode). */
    FM1_INPUT_IDLE();
}

static void fm1_input_edges(uint32_t *pressed, uint32_t *released)
{
    /* IRQ-safe: atomically swap button edge fields. */
    uint32_t f;
    (void)f;
    /* TODO Task 3: use irq_save/restore or atomic ops. */
    *pressed  = fm1_in.pressed;  fm1_in.pressed  = 0;
    *released = fm1_in.released; fm1_in.released = 0;
}

static void fm1_input_note_edges(uint32_t *pressed)
{
    *pressed = fm1_in.notes_pressed; fm1_in.notes_pressed = 0;
}

static int16_t fm1_enc_take(uint32_t n)
{
    int16_t v = fm1_in.enc_steps[n];
    fm1_in.enc_steps[n] = 0;
    return v;
}

/* LED helpers (same signatures as FM-1; Task 3 maps to the SMK pad LEDs). */
static inline void fm1_led_key(uint32_t id, uint8_t *arr, uint32_t on)
{
    /* TODO Task 3: map piano key id to 595 column/row. */
    (void)id; (void)arr; (void)on;
}
static inline void fm1_led_btn(uint32_t id, uint8_t *arr, uint32_t on)
{
    (void)id; (void)arr; (void)on;
}
static inline void fm1_led_dim_level(uint32_t level)
{
    (void)level;
}
