/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Peter Whitney */
/* SMK-37 input HAL: key/button/encoder matrix, RGB pad LEDs, muxed ADC.
 *
 * PURPOSE: This file's first job is to pull in fm1_flash_map.h so that the
 * SMK-37 flash-address constants are defined BEFORE fm1_flash.h (chip/) sees
 * them via unity.c's include order:
 *
 *   unity.c includes:
 *     bsp/boards/smk37/fm1_input.h   ← this file  (board, first)
 *       └─ fm1_flash_map.h            ← overrides FL_DATA_LO, FL_PLAIN_WINDOW_LO …
 *     bsp/chip/fm1_flash.h            ← respects #ifndef guards (chip, second)
 *
 * Hardware (docs/01-hardware.md, all verified on a real SMK-37 Pro):
 *
 *   Column drivers: 4 x 74HC595, SPI2 group C (IOMAP_CON1 bits 17..16 = 10)
 *     CLK PA3, MOSI PA4, latch PA1, OE PA0 (held low)
 *   32-bit column word (little-endian DMA):
 *     bits 31..24  columns  0..7  select, active low (bit 24 = col 0)
 *     bits 23..16  columns  8..15 select, active low (bit 16 = col 8)
 *     bits  5..3   mux address (shared by all three ADC mux channels)
 *     bits  15..0  button LED lines, active low (except bits 5..3 = mux)
 *   Row returns (inputs with pull-ups, low = contact closed):
 *     row 0 = PA2, row 1 = PA5, row 2 = PA6, row 3 = PA7
 *     row 4 = PA9, row 5 = PH6, row 6 = PH8, row 7 = PH9
 *     CRITICAL: PA9, PH6, PH8, PH9 are INPUTS — NEVER drive as outputs.
 *     PH8 MUST NEVER be UART RX (FM1B_TRS=0 forced for SMK builds).
 *
 *   ADC mux addresses (bits 5..3 of the 595 word):
 *     mux A ch1 PA8:  addr 0..7 → pads 1,2,3,0,4,7,5,6  (hardware pad index)
 *     mux B ch8 PC9:  addr 0..7 → pads 9,10,11,8,12,15,13,14
 *     mux C ch2 PA10: addr 0..7 → fader1,fader2,fader3,fader0,pitch,mod,pedal,battery
 *     ADC timing: CON = 0x400D | (ch<<8), bit4 enable, bit6 start; ~40 us per conv.
 *     NOTE: the FM-1's slow ADC timing (FM1_ADC_CON_SDK) never finishes in one tick.
 *
 *   Keybed: 37 velocity keys, two contacts each (c1 closes first, c2 near bottom).
 *     Velocity: T = 10 + elapsed 0.1ms between c1 and c2. x = 1023 - T.
 *     5-point curve from calibration record (USRTRIM 0xC1000).
 *
 *   Buttons: 18 on rows 5 and 6. Encoders K1..K8 on row 7.
 *
 *   RGB pad LEDs: 16 x WS2812-class on SPI1 group B (IOMAP_CON1 bit4=1), PC10.
 *     GRB order, 420 bytes: 36 zero bytes + 16 x 24 bytes. Baud 8 (~6.67 MHz).
 *     Chain slot mapping: pad 0→slot 0, 1→15, 2→14 … 7→9, 8→1, 9→2 … 15→8.
 *     PC9 (group B CLK) stays analog — NEVER configure as SPI CLK.
 *     Flush after LCD DMA finishes and PC8 (CS) is high. PC10 low between frames.
 *
 *   Button LEDs: 19 single-colour on the 595's low 16 bits (active low), two commons.
 *     Common driver: USB2_PHY pad GPIO register 0x16A0C, bit17=group0, bit18=group1, low=on.
 *     Alternate between group 0 and group 1 each frame.
 *
 *   Scan order: read rows (from previous column), latch, DMA next column word.
 *   One step per 10 kHz timer tick → 16 columns → 1 frame per 1.6 ms (~625 Hz).
 */
#pragma once
#include <stdint.h>

/* ---- flash map override (MUST come before fm1_flash.h) ---- */
#include "fm1_flash_map.h"

/* The remaining board-layer headers pull in the full board layer via unity.c */
#include "fm1_gpio.h"
#include "fm1_time.h"
#include "fm1_adc.h"

/* ================================================================
 * Scan constants
 * ================================================================ */
#ifndef FM1_INPUT_IDLE
#define FM1_INPUT_IDLE() ((void)0)
#endif
#ifndef FM1_INPUT_NOW
#define FM1_INPUT_NOW() fm1_ticks()
#endif
#ifndef FM1_LED_TRACE
#define FM1_LED_TRACE(rowmask) ((void)0)
#endif
#ifndef FM1_SR_LATCH_TRACE
#define FM1_SR_LATCH_TRACE() ((void)0)
#endif
#ifndef FM1_INPUT_LED_OK
#define FM1_INPUT_LED_OK(col, rowbit) 1
#endif

/* Debounce: same as FM-1 (frame = 16 ticks ≈ 1.6 ms) */
#define FM1_DEB_PRESS    2u    /* frames closed in a row → pressed */
#define FM1_DEB_RELEASE  4u    /* frames open in a row → released (reduced; 4 x 1.6ms ≈ 6.4ms) */
#define FM1_INPUT_LAT    1     /* press latency stats enabled */
#define FM1_SETTLE_US    10u   /* column settle time */

/* LED dim / breath constants (button LEDs; same as FM-1 for compatibility) */
#ifndef FM1_LED_DIM_NS
#define FM1_LED_DIM_NS      3200u
#endif
#ifndef FM1_LED_DIM_LO_NS
#define FM1_LED_DIM_LO_NS   1600u
#endif
#ifndef FM1_LED_DIM_DIV
#define FM1_LED_DIM_DIV     1u
#endif
#define FM1_LED_BREATH_FRAMES  1024u
#ifndef FM1_LED_BREATH_PK
#define FM1_LED_BREATH_PK      154u
#endif
#ifndef FM1_LED_BREATH_PK_LO
#define FM1_LED_BREATH_PK_LO   77u
#endif
#ifndef FM1_LED_MID_N
#define FM1_LED_MID_N    8u
#endif
#ifndef FM1_LED_MID_N_LO
#define FM1_LED_MID_N_LO 12u
#endif
#ifndef FM1_LED_TICK_US
#define FM1_LED_TICK_US  100u
#endif

/* Encoder constants */
#define FM1_REST_FRAMES  900u  /* ~1.4 s still: re-learn detent */
#define FM1_ENC_GO       20u   /* 2-state jump uses last direction within this many frames */

/* ================================================================
 * Matrix dimensions
 * ================================================================ */
#define FM1_NCOL  16u          /* 16 columns (4 × 74HC595) */
#define FM1_NROW   8u          /* 8 row returns */
#define FM1_NKEY  55u          /* 37 keys (ids 0..36) + 18 buttons (ids 37..54) */
#define FM1_NKEY_NOTES 37u
#define FM1_NKEY_BTNS  18u
#define FM1_NENC   8u          /* K1..K8 */
#define FM1_NPADS  16u         /* RGB pads */

/* Button id 0 = KNOB BANK, 4 = OCTAVE-, 5 = OCTAVE+ (per hardware table) */
enum {
    FM1_BTN_OCT_DOWN = 4,      /* OCTAVE- */
    FM1_BTN_OCT_UP   = 5       /* OCTAVE+ */
};

/* ================================================================
 * Register addresses
 * ================================================================ */

/* SPI2 (matrix shift registers), group C: CLK PA3, MOSI PA4 */
#define SMK_SPI2_CON   (*(volatile uint32_t *)0x11E00u)
#define SMK_SPI2_BAUD  (*(volatile uint32_t *)0x11E04u)
#define SMK_SPI2_ADR   (*(volatile uint32_t *)0x11E0Cu)
#define SMK_SPI2_CNT   (*(volatile uint32_t *)0x11E10u)

/* IOMAP_CON1: bit 4 = SPI1 group (0=A, 1=B), bits 17..16 = SPI2 group (10=C) */
#define SMK_IOMAP_CON1 (*(volatile uint32_t *)0x51020u)

/* Button-LED commons: USB2_PHY pad GPIO register */
#define SMK_LED_CMN_REG  (*(volatile uint32_t *)0x16A0Cu)
#define SMK_LED_CMN_BITS 0x1000Fu           /* GPIO-mode bits that must stay set */
#define SMK_LED_CMN0     (1u << 17)         /* group 0 common, low = on */
#define SMK_LED_CMN1     (1u << 18)         /* group 1 common, low = on */

/* SPI1 (shared: LCD group A, LED group B) */
#define SMK_SPI1_CON    (*(volatile uint32_t *)0x11D00u)
#define SMK_SPI1_BAUD   (*(volatile uint32_t *)0x11D04u)
#define SMK_SPI1_ADR    (*(volatile uint32_t *)0x11D0Cu)
#define SMK_SPI1_CNT    (*(volatile uint32_t *)0x11D10u)
#define SMK_LCD_CS      (1u << 8)           /* PC8, active low */
#define SMK_LCD_PC_OUT  (*(volatile uint32_t *)0x50080u)
#define SMK_IOMAP_BIT4  0x10u               /* SPI1 group B = 1 */

/* ADC mux channel numbers (chip-level ADC; see fm1_adc.h) */
#define SMK_ADC_MUX_A   1u    /* PA8  */
#define SMK_ADC_MUX_B   8u    /* PC9  */
#define SMK_ADC_MUX_C   2u    /* PA10 */

/* ADC timing for SMK (fast path, ~40 us; FM-1 timing too slow for the SMK) */
#define SMK_ADC_CON(ch) (0x400Du | ((uint32_t)(ch) << 8u))

/* ================================================================
 * Column word construction
 *   bits 31..24: column 0..7 select (active low, col c → bit 24+c = 0)
 *   bits 23..16: column 8..15 select (active low, col c → bit 16+(c-8) = 0)
 *   bits  5..3:  mux address
 *   bits 15..0:  button LED bits (active low), ANDed with led_word
 * ================================================================ */
static inline uint32_t smk__col_word(uint32_t col, uint32_t mux_addr, uint32_t led_word)
{
    uint32_t sel = col < 8u
        ? ~(0x01000000u << col)
        : ~(0x00010000u << (col - 8u));
    uint32_t low = led_word & ~0x38u;           /* clear bits 5..3 */
    low |= (mux_addr & 7u) << 3;               /* insert mux address */
    return (sel & 0xFFFF0000u) | (low & 0xFFFFu);
}

/* ================================================================
 * Keymap: col<<4 | row → key id (0..36 = note keys, 37..54 = buttons, -1 = none)
 * Each key has TWO contacts: c1 (rows 0..3) and c2 (rows 4..7), same column.
 * The first contact (c1) starts the velocity timer; c2 fires the note-on.
 * Rows 5..6 carry buttons; row 7 carries encoders.
 * Encoding: SMK_KEYMAP[row][col] = key id, -1 = empty
 * ================================================================ */

/* Note key to (c1 col, c2 col): extracted from 01-hardware.md table
 * k=0→cols 1,5; k=1→2,7; k=2→3,9; k=3→4,11; k=4→5,13; k=5→6,15; k=6→7,14;
 * k=7→8,12; k=8→9,10; k=9→10,8; k=10→12,6; k=11→13,4; k=12→14,2; k=13→15,1;
 * k=14→11,3; k=15→1,3; k=16→2,4; k=17→3,5; k=18→4,6; k=19→5,7; k=20→6,8;
 * k=21→7,9; k=22→8,10; k=23→9,11; k=24→10,12; k=25→11,13; k=26→12,14; k=27→13,2;
 * k=28→14,1; k=29→15,1; k=30→6,2; k=31→7,3; k=32→8,4; k=33→9,5; k=34→10,13;
 * k=35→11,15; k=36→12,14
 * (col<<4)|row codes from the doc table reparse: c1 code → col=high, row=low)
 *
 * From doc: k=0: c1=10,c2=52 → c1 col=1 row=0, c2 col=5 row=2
 * The full 16×8 keymap is built from the doc's (col<<4|row) codes.
 */

/* SMK_KEYMAP[row][col] = key id (0..36=notes, 37..54=buttons), -1=empty, -2=encoder */
static const int8_t SMK_KEYMAP[8][16] = {
    /* row 0 = PA2 : first contacts (c1) of keys 0..13 (lower half of keyboard) */
    { -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 14, 10, 11, 12, 13 },
    /* row 1 = PA5 : first contacts (c1) of keys 15..29 */
    { -1, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29 },
    /* row 2 = PA6 : second contacts (c2) of keys 12,7,22,26,28,13,0,1,2... */
    { -1, 13, 12, 14, 16, 17, 18, 19, 20, 21, 22, -1, -1, 10, -1, -1 },
    /* row 3 = PA7 : second contacts (c2) of keys 28,27,15,... */
    { -1, -1, 27, 31, 32, 33, 30, -1, -1, 23, 24, 25, -1, 34, -1, 35 },
    /* row 4 = PA9 : second contacts (c2) of upper keys */
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 29, 28, -1, -1, -1, -1 },
    /* row 5 = PH6 : buttons (ids 37..54, stored as id-37 here mapped below) */
    { -1, 10+37, 0+37, 8+37, 9+37, 3+37, 5+37, 7+37, 9+37, -1, -1, 2+37, 4+37, -1, 11+37, -1 },
    /* row 6 = PH8 : buttons */
    { 16+37, -1, 2+37, -1, -1, 15+37, -1, 1+37, 14+37, 12+37, -1, 13+37, -1, 6+37, 17+37, -1 },
    /* row 7 = PH9 : encoders (K1..K8, not debounced as keys) */
    { -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2 },
};

/* ================================================================
 * Velocity key c1/c2 contacts
 * c1 = (col,row) of first contact; c2 = (col,row) of second contact
 * From 01-hardware.md section 5.2, (col<<4|row) notation
 * ================================================================ */
static const uint8_t SMK_KEY_C1[37] = {
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0x90, 0xa0, 0xc0, 0xd0, 0xe0, 0xf0, 0xb0, 0x11,
    0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81, 0x91,
    0xa1, 0xb1, 0xc1, 0xd1, 0xe1, 0xf1, 0x64, 0x74,
    0x84, 0x94, 0xa4, 0xb4, 0xc4
};
static const uint8_t SMK_KEY_C2[37] = {
    0x52, 0x72, 0x92, 0xb2, 0xd2, 0xf2, 0xe2, 0xc2,
    0xa2, 0x82, 0x62, 0x42, 0x22, 0x12, 0x32, 0x33,
    0x43, 0x53, 0x63, 0x73, 0x83, 0x93, 0xa3, 0xb3,
    0xc3, 0xd3, 0xe3, 0x23, 0x13, 0x14, 0x24, 0x34,
    0x44, 0x54, 0xd4, 0xf4, 0xe4
};

/* Button (col<<4|row) positions, ids 0..17 per hardware table */
static const uint8_t SMK_BTN_POS[18] = {
    0x26, 0x76, 0x96, 0x56, 0x66, 0x36,  /* 0..5:  KNOB BANK, FADER BANK, <, >, OCT-, OCT+ */
    0x75, 0x65, 0x25, 0x45, 0x15, 0x86,  /* 6..11: ARP, NOTE REPEAT, SCALE, CHORD, GLOBE, BT */
    0x95, 0x55, 0x85, 0x35, 0x16, 0x46   /* 12..17: PATCH, PARA, FX, SEQ, SEQ PLAY, SEQ REC */
};

/* Encoder A/B contacts: A=(col_a,row), B=(col_b,row), all row 7
 * K1..K8 A: cols 9,3,1,11,5,7,13,15; B: cols 10,4,2,12,6,8,14,0 */
static const uint8_t SMK_ENC[8][4] = {
    {  9, 7, 10, 7 },   /* K1 */
    {  3, 7,  4, 7 },   /* K2 */
    {  1, 7,  2, 7 },   /* K3 */
    { 11, 7, 12, 7 },   /* K4 */
    {  5, 7,  6, 7 },   /* K5 */
    {  7, 7,  8, 7 },   /* K6 */
    { 13, 7, 14, 7 },   /* K7 */
    { 15, 7,  0, 7 },   /* K8 */
};

/* ================================================================
 * Button LED assignments (group, bit) for ids 0..17 + spare(18)
 * Group 0 = common 0 (SMK_LED_CMN0), Group 1 = common 1 (SMK_LED_CMN1)
 * Bit positions in the low 16 bits of the 595 word (active low)
 * ================================================================ */
static const uint8_t SMK_BTNLED_GRP[19] = {
    0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1
};
static const uint8_t SMK_BTNLED_BIT[19] = {
    12, 2, 0, 0, 13, 13, 2, 1, 9, 8, 11, 10, 1, 9, 8, 10, 12, 14, 11
};

/* WS2812 pad LED chain slot for hardware pad 0..15 */
static const uint8_t SMK_PAD_CHAIN[16] = {
    0, 15, 14, 13, 12, 11, 10, 9, 1, 2, 3, 4, 5, 6, 7, 8
};

/* ================================================================
 * State structs
 * ================================================================ */

/* Raw row sample per column, one byte packs the 8 rows (bit i = row i) */
static volatile uint8_t smk_raw[FM1_NCOL];   /* 1 = contact closed */

/* Note-key debounce and velocity timing */
static volatile struct {
    uint8_t  c1_seen;           /* c1 has been seen closed (bit per key) → unused (stored per key) */
    uint32_t c1_time[FM1_NKEY_NOTES];   /* FM1_INPUT_NOW() when c1 first closed */
    uint8_t  c1_cnt[FM1_NKEY_NOTES];   /* frames c1 closed since c1_time set */
    uint8_t  c2_cnt[FM1_NKEY_NOTES];   /* debounce counter for c2 */
    uint8_t  c1_state;          /* bit field for keys 0..7 c1 open/closed debounced */
    uint32_t notes;             /* bit n = note key n down */
    uint32_t notes_pressed;     /* edge: press since last fm1_input_note_edges() */
    uint32_t note_t0[FM1_NKEY_NOTES];  /* time of first c2 close (for latency stats) */
} smk_keys;

/* Button debounce */
static volatile struct {
    uint8_t  cnt[FM1_NKEY_BTNS];
    uint32_t buttons;           /* bit i = button i down */
    uint32_t pressed;
    uint32_t released;
} smk_btns;

/* Encoder state */
static volatile struct {
    uint8_t  prev[FM1_NENC];
    uint8_t  last[FM1_NENC];
    uint8_t  rest[FM1_NENC];
    uint16_t still[FM1_NENC];
    int8_t   sub[FM1_NENC];
    int16_t  steps[FM1_NENC];
    int8_t   dir[FM1_NENC];
    uint32_t dfr[FM1_NENC];
} smk_enc;

/* ADC / mux state: cycles through 8 mux addresses × 3 channels = 24 steps */
static volatile struct {
    uint8_t  step;              /* 0..23 */
    uint16_t pad_a[8];          /* mux A: hardware pads 0..7 (top row) */
    uint16_t pad_b[8];          /* mux B: hardware pads 8..15 (bottom row) */
    uint16_t fader[4];          /* mux C addr 0..3 → fader index 1,2,3,0 */
    uint16_t pitch;             /* mux C addr 4 */
    uint16_t mod;               /* mux C addr 5 */
    uint16_t battery;           /* mux C addr 7 */
    uint8_t  pend;              /* conversion in flight */
    uint8_t  pend_mux;          /* mux channel of in-flight conversion */
    uint8_t  pend_addr;         /* mux address of in-flight conversion */
} smk_an;

/* Virtual MASTER pot (K1 encoder accumulates; 0..1023) */
static volatile uint16_t smk_master;

/* Frame counter */
static volatile uint32_t smk_frames;

/* Scan state: which column is latched / being driven */
static volatile uint8_t smk_tick_col;

/* LED state */
static uint8_t smk_btnled[2][16];   /* button LED word bits [group][column] (OR into led_word low half) */
static uint8_t smk_led_grp;         /* current group: 0 or 1 */

/* RGB pad LED frame buffer: 36 zero bytes + 16×24 bytes = 420 bytes */
#define SMK_RGB_RESET_BYTES  36u
#define SMK_RGB_LED_BYTES   (16u * 24u)
#define SMK_RGB_FRAME_BYTES (SMK_RGB_RESET_BYTES + SMK_RGB_LED_BYTES)
static uint8_t smk_rgb[SMK_RGB_FRAME_BYTES];
static volatile uint8_t smk_rgb_dirty;  /* nonzero = needs flush */

/* Diagnostics */
#define FM1_GAP_BINS 8u
static volatile struct {
    uint32_t last, gap_max, gap_hist[FM1_GAP_BINS];
    uint32_t cost_sum, cost_max, ticks;
    uint32_t enc_moves[FM1_NENC], enc_lost[FM1_NENC];
    uint32_t press_n, press_sum, press_max;
} smk_diag;

/* ================================================================
 * Expose the fm1_in / fm1_an shapes that app code expects
 * (app/ reads fm1_in.notes, fm1_in.buttons, fm1_an.master, fm1_an.faders)
 * ================================================================ */
static volatile struct {
    uint32_t notes;
    uint32_t buttons;
    uint32_t pressed, released;
    uint32_t notes_pressed;
    uint8_t  raw[FM1_NCOL];
    uint8_t  cnt[FM1_NKEY];
    uint32_t note_t0[FM1_NKEY_NOTES];
    uint8_t  enc_prev[FM1_NENC], enc_last[FM1_NENC];
    uint8_t  enc_rest[FM1_NENC];
    uint16_t enc_still[FM1_NENC];
    int8_t   enc_sub[FM1_NENC];
    int16_t  enc_steps[FM1_NENC];
    int8_t   enc_dir[FM1_NENC];
    uint32_t enc_dfr[FM1_NENC];
    uint32_t frames;
} fm1_in;

static volatile struct {
    uint16_t con;
    uint8_t  drive, probe;
    uint8_t  pend, col, rowbit;
    uint8_t  driven, probing;
    uint8_t  phase;
    uint16_t master;            /* virtual MASTER from K1 */
    uint16_t faders[4];         /* CC16-19 */
    uint16_t timeouts;
} fm1_an;

/* Button-LED arrays (indexed by column, row-bit mask) — app uses fm1_led[] */
static uint8_t fm1_led[FM1_NCOL];
static uint8_t fm1_led_dim[FM1_NCOL];
static uint8_t fm1_led_breath[FM1_NCOL];
static uint8_t fm1_led_mid[FM1_NCOL];

/* fm1_led_anim.h is in bsp/boards/fm1/ (not on the SMK include path).
 * Pull in only the constants we need inline: */
#define FM1_ANIM_FRAMES 640u
#define FM1_ANIM_SWEEP  512u
#define FM1_ANIM_NBTN   14u
#define FM1_ANIM_NKEY   27u

/* Power-on sweep animation state variables (forward declarations before tick) */
static uint16_t fm1__an_f;
static uint8_t  fm1__an_glow, fm1__an_idle;
static uint8_t  fm1__an_cb[FM1_NKEY], fm1__an_acc[FM1_NKEY];

/* Forward declarations: defined after fm1_input_scan to allow mutual ref. */
static void fm1__anim_frame(void);
static void fm1__anim_begin(uint32_t glow, uint32_t idle);

/* ================================================================
 * SPI2 helpers: shift a 32-bit column word to the 4×595 chain by DMA
 * ================================================================ */

/* Column word scratch buffer (must be in DMA-accessible RAM; static = OK) */
static uint32_t smk__col_buf;

/* Start a DMA transfer of smk__col_buf to SPI2 (does NOT latch) */
static inline void smk__sr_dma_start(void)
{
    SMK_SPI2_CON &= ~1u;                    /* disable SPI2 */
    SMK_SPI2_ADR  = (uint32_t)(uintptr_t)&smk__col_buf;
    SMK_SPI2_CNT  = 4u;                     /* 4 bytes = 32 bits */
    SMK_SPI2_CON |= 1u;                     /* enable: starts DMA */
}

static void smk__sr_dma_wait(void)
{
    while (!(SMK_SPI2_CON & 0x8000u))
        FM1_INPUT_IDLE();
    SMK_SPI2_CON |= 0x4000u;               /* clear pending flag */
}

static void smk__sr_latch(void)
{
    FM1_SR_LATCH_TRACE();
    /* Pulse PA1 high then low */
    FM1_PR(FM1_PA, FM1_OUT) |=  1u << 1;
    FM1_PR(FM1_PA, FM1_OUT) &= ~(1u << 1);
}

/* ================================================================
 * Row reading: 8 rows packed into one byte
 * low = contact closed (pull-up, active low)
 * ================================================================ */
static uint8_t smk__rows(void)
{
    uint32_t a = FM1_PR(FM1_PA, FM1_IN);
    uint32_t h = FM1_PR(FM1_PH, FM1_IN);
    /* row 0=PA2, 1=PA5, 2=PA6, 3=PA7, 4=PA9, 5=PH6, 6=PH8, 7=PH9 */
    uint8_t r = (uint8_t)(
        ((~a >> 2) & 1u)       |   /* row 0: PA2 */
        ((~a >> 4) & 2u)       |   /* row 1: PA5 */
        ((~a >> 4) & 4u)       |   /* row 2: PA6 */
        ((~a >> 4) & 8u)       |   /* row 3: PA7 */
        ((~a >> 5) & 0x10u)    |   /* row 4: PA9 */
        ((~h >> 1) & 0x20u)    |   /* row 5: PH6 */
        ((~h >> 1) & 0x40u)    |   /* row 6: PH8 */
        ((~h >> 2) & 0x80u)        /* row 7: PH9 */
    );
    return r;
}

/* ================================================================
 * ADC helpers (SMK timing: ~40 us per conversion)
 * ================================================================ */
static void smk__adc_start(uint32_t ch)
{
    uint32_t con = SMK_ADC_CON(ch);
    /* Same sequence as fm1_adc_start but with SMK timing constant */
    FM1_ADC_CON_R = 0;
    if (FM1_WLA_CON0 & (1u << 14))
        FM1_WLA_CON0 &= ~(1u << 14);
    FM1_ADC_CON_R = con | 0x40u;
    FM1_ADC_CON_R |= 0x10u;
    FM1_ADC_CON_R |= 0x40u;
}

/* ================================================================
 * Note-key velocity debounce
 * Returns 1 when note-on should fire for key k (c2 just debounced closed)
 * ================================================================ */
static void smk__key_c1(uint32_t k, uint32_t closed)
{
    /* Track first closure of c1 for velocity timing */
    if (closed && !(fm1_in.note_t0[k])) {
        fm1_in.note_t0[k] = FM1_INPUT_NOW();
    } else if (!closed) {
        /* c1 released: if note is on, start note-off debounce */
        if (fm1_in.notes & (1u << k))
            fm1_in.cnt[k] = 0;
    }
}

static void smk__key_c2(uint32_t k, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[k];
    uint32_t on = (fm1_in.notes >> k) & 1u;
    if (closed == on) { *c = 0; return; }
    if (!on && *c == 0u)
        fm1_in.note_t0[k] = FM1_INPUT_NOW();   /* start latency timer */
    if (++*c < (on ? FM1_DEB_RELEASE : FM1_DEB_PRESS))
        return;
    *c = 0;
    fm1_in.notes ^= 1u << k;
    if (!on) {
        fm1_in.notes_pressed |= 1u << k;
        smk_diag.press_n++;
    }
}

/* ================================================================
 * Button debounce
 * ================================================================ */
static void smk__btn(uint32_t id, uint32_t closed)
{
    volatile uint8_t *c = &fm1_in.cnt[FM1_NKEY_NOTES + id];
    uint32_t on = (fm1_in.buttons >> id) & 1u;
    if (closed == on) { *c = 0; return; }
    if (++*c < (on ? FM1_DEB_RELEASE : FM1_DEB_PRESS))
        return;
    *c = 0;
    fm1_in.buttons ^= 1u << id;
    if (!on)
        fm1_in.pressed  |= 1u << id;
    else
        fm1_in.released |= 1u << id;
}

/* ================================================================
 * Process one column's rows after reading
 * ================================================================ */
static void smk__process_col(uint32_t col)
{
    uint8_t raw = smk_raw[col];
    uint32_t r;
    /* Keybed rows 0..4 */
    for (r = 0; r < 5u; r++) {
        uint32_t closed = (raw >> r) & 1u;
        /* Check c1 contacts */
        uint32_t k;
        for (k = 0; k < FM1_NKEY_NOTES; k++) {
            if ((SMK_KEY_C1[k] >> 4) == col && (SMK_KEY_C1[k] & 0xFu) == r)
                smk__key_c1(k, closed);
            if ((SMK_KEY_C2[k] >> 4) == col && (SMK_KEY_C2[k] & 0xFu) == r)
                smk__key_c2(k, closed);
        }
    }
    /* Button rows 5..6 */
    for (r = 5u; r < 7u; r++) {
        uint32_t closed = (raw >> r) & 1u;
        uint32_t b;
        for (b = 0; b < FM1_NKEY_BTNS; b++) {
            if ((SMK_BTN_POS[b] >> 4) == col && (SMK_BTN_POS[b] & 0xFu) == r)
                smk__btn(b, closed);
        }
    }
    /* Row 7 = encoders: handled in smk__frame() using raw bits directly */
    fm1_in.raw[col] = raw;
}

/* ================================================================
 * Per-frame encoder update (called after each full 16-column scan)
 * ================================================================ */
static void smk__frame(void)
{
    uint32_t e;
    for (e = 0; e < FM1_NENC; e++) {
        const uint8_t *m = SMK_ENC[e];
        uint32_t col_a = m[0], row_a = m[1];
        uint32_t col_b = m[2], row_b = m[3];
        uint32_t cur = ((smk_raw[col_a] >> row_a) & 1u) << 1
                     | ((smk_raw[col_b] >> row_b) & 1u);
        volatile int8_t *sub = &fm1_in.enc_sub[e];

        if (cur != fm1_in.enc_last[e]) {
            fm1_in.enc_last[e] = (uint8_t)cur;
            fm1_in.enc_still[e] = 0;
        }
        if (fm1_in.enc_prev[e] == 0xFF) {
            fm1_in.enc_prev[e] = (uint8_t)cur;
            fm1_in.enc_rest[e] = (uint8_t)cur;
        }
        if (fm1_in.enc_still[e] < 0xFFFFu && ++fm1_in.enc_still[e] == FM1_REST_FRAMES
                && cur != fm1_in.enc_rest[e]) {
            fm1_in.enc_rest[e] = (uint8_t)cur;
            *sub = 0;
        }
        if (cur == fm1_in.enc_prev[e])
            continue;
        {
            uint32_t idx = (uint32_t)fm1_in.enc_prev[e] << 2 | cur;
            if ((0x4182u >> idx) & 1u) {
                (*sub)++;
                smk_diag.enc_moves[e]++;
            } else if ((0x2814u >> idx) & 1u) {
                (*sub)--;
                smk_diag.enc_moves[e]++;
            } else {
                int32_t d = *sub > 0 ? 1 : *sub < 0 ? -1 :
                    (uint32_t)(fm1_in.frames - fm1_in.enc_dfr[e]) <= FM1_ENC_GO
                        ? fm1_in.enc_dir[e] : 0;
                smk_diag.enc_lost[e]++;
                *sub = (int8_t)(*sub + 2 * d);
            }
            fm1_in.enc_prev[e] = (uint8_t)cur;
        }
        if (*sub > 100 || *sub < -100)
            *sub = 0;
        if (cur == fm1_in.enc_rest[e]) {
            int32_t n = *sub < 0 ? -*sub : *sub;
            n = n >= 2 ? (n + 2) / 4 : 0;
            fm1_in.enc_steps[e] = (int16_t)(fm1_in.enc_steps[e] + (*sub < 0 ? -n : n));
            if (n) {
                fm1_in.enc_dir[e] = (int8_t)(*sub < 0 ? -1 : 1);
                fm1_in.enc_dfr[e] = fm1_in.frames;
            }
            *sub = 0;
        }
    }
    fm1_in.frames++;

    /* --- ADC mux step: advance one of the 24 steps (8 addr × 3 channels) --- */
    if (!smk_an.pend) {
        uint32_t step = smk_an.step;
        uint32_t addr = step % 8u;
        uint32_t ch_idx = step / 8u;   /* 0=mux A, 1=mux B, 2=mux C */
        uint32_t ch = ch_idx == 0u ? SMK_ADC_MUX_A
                    : ch_idx == 1u ? SMK_ADC_MUX_B
                    : SMK_ADC_MUX_C;
        /* The mux address is written in the next column shift (bits 5..3 of col word);
         * start the conversion now and take the result next frame step */
        smk__adc_start(ch);
        smk_an.pend      = 1;
        smk_an.pend_mux  = (uint8_t)ch_idx;
        smk_an.pend_addr = (uint8_t)addr;
        smk_an.step      = (uint8_t)((step + 1u) % 24u);
    }
}

/* Store a finished ADC conversion result */
static void smk__adc_take(void)
{
    if (!fm1_adc_done()) {
        fm1_adc_abort();
        fm1_an.timeouts++;
        smk_an.pend = 0;
        return;
    }
    uint32_t v = fm1_adc_take();
    uint32_t addr = smk_an.pend_addr;
    smk_an.pend = 0;

    /* Pad address → hardware pad index mapping per mux */
    static const uint8_t MUX_A_PAD[8] = {1,2,3,0,4,7,5,6};
    static const uint8_t MUX_B_PAD[8] = {9,10,11,8,12,15,13,14};
    static const uint8_t MUX_C_FADER[4] = {1,2,3,0};  /* mux C 0..3 → fader index */

    if (smk_an.pend_mux == 0u) {
        smk_an.pad_a[MUX_A_PAD[addr]] = (uint16_t)v;
    } else if (smk_an.pend_mux == 1u) {
        smk_an.pad_b[MUX_B_PAD[addr]] = (uint16_t)v;
    } else {
        switch (addr) {
        case 0: case 1: case 2: case 3:
            smk_an.fader[MUX_C_FADER[addr]] = (uint16_t)v;
            fm1_an.faders[MUX_C_FADER[addr]] = (uint16_t)v;
            break;
        case 4: smk_an.pitch   = (uint16_t)v; break;
        case 5: smk_an.mod     = (uint16_t)v; break;
        case 7: smk_an.battery = (uint16_t)v; break;
        }
    }
}

/* ================================================================
 * Button LED helpers
 * ================================================================ */

/* Flush button-LED word for current group into the column word low 16 bits.
 * Called from fm1_input_tick() when shifting column `col`. */
static uint16_t smk__btnled_word(uint32_t col)
{
    uint16_t w = 0xFFFFu;   /* all-high = all LEDs off (active low) */
    uint32_t grp = smk_led_grp;
    if (col < 16u)
        w &= (uint16_t)~smk_btnled[grp][col];   /* turn on requested bits */
    return w;
}

/* ================================================================
 * RGB pad LED: encode one LED into the SPI byte stream (GRB, MSB first)
 * bit=1 → 0xF0, bit=0 → 0xC0
 * ================================================================ */
static void smk__rgb_encode(uint32_t slot, uint8_t g, uint8_t r, uint8_t b)
{
    uint8_t *p = smk_rgb + SMK_RGB_RESET_BYTES + slot * 24u;
    uint32_t i, byte;
    uint8_t rgb[3] = { g, r, b };
    for (i = 0; i < 3u; i++) {
        byte = rgb[i];
        uint32_t j;
        for (j = 0; j < 8u; j++) {
            *p++ = (byte & 0x80u) ? 0xF0u : 0xC0u;
            byte <<= 1;
        }
    }
}

/* Flush the RGB frame via SPI1 group B.
 * Call only when LCD CS (PC8) is high and no LCD DMA is running. */
static void smk_rgb_flush(void)
{
    if (!smk_rgb_dirty)
        return;
    smk_rgb_dirty = 0;

    /* Wait for LCD to deselect */
    uint32_t n;
    for (n = 0; n < 4000000u && !(SMK_LCD_PC_OUT & SMK_LCD_CS); n++)
        ;

    /* Switch SPI1 to group B (bit 4 = 1) for WS2812 output on PC10 */
    SMK_IOMAP_CON1 |= SMK_IOMAP_BIT4;
    SMK_SPI1_BAUD   = 8u;
    SMK_SPI1_CON   |= 0x4000u;    /* clear pending */

    /* DMA the 420-byte frame */
    SMK_SPI1_ADR = (uint32_t)(uintptr_t)smk_rgb;
    SMK_SPI1_CNT = SMK_RGB_FRAME_BYTES;

    /* Wait for DMA done */
    for (n = 0; n < 4000000u && !(SMK_SPI1_CON & 0x8000u); n++)
        ;
    SMK_SPI1_CON |= 0x4000u;

    /* Restore LCD group A and baud */
    SMK_IOMAP_CON1 &= ~SMK_IOMAP_BIT4;
    SMK_SPI1_BAUD   = 4u;
    SMK_SPI1_CON   |= 0x4000u;
}

/* ================================================================
 * fm1_input_init
 * ================================================================ */
static void fm1_input_init(void)
{
    uint32_t i;

    /* --- SPI2 group C: CLK PA3, MOSI PA4 (IOMAP_CON1 bits 17..16 = 10) --- */
    SMK_IOMAP_CON1 = (SMK_IOMAP_CON1 & ~(3u << 16)) | (2u << 16);

    /* PA3 (CLK), PA4 (MOSI): outputs, no pulls */
    {
        uint32_t sr_mask = (1u << 3) | (1u << 4);
        FM1_PR(FM1_PA, FM1_DIE) |=  sr_mask;
        FM1_PR(FM1_PA, FM1_PU)  &= ~sr_mask;
        FM1_PR(FM1_PA, FM1_PD)  &= ~sr_mask;
        FM1_PR(FM1_PA, FM1_OUT) &= ~sr_mask;
        FM1_PR(FM1_PA, FM1_DIR) &= ~sr_mask;
    }

    /* PA1 (latch), PA0 (OE held low): outputs */
    {
        uint32_t ctrl_mask = (1u << 0) | (1u << 1);
        FM1_PR(FM1_PA, FM1_DIE) |=  ctrl_mask;
        FM1_PR(FM1_PA, FM1_OUT) &= ~ctrl_mask;   /* PA0=0 (OE active), PA1=0 */
        FM1_PR(FM1_PA, FM1_DIR) &= ~ctrl_mask;
    }

    /* SPI2: master, transmit only, DMA mode */
    SMK_SPI2_CON  = 0x4020u;
    SMK_SPI2_BAUD = 5u;

    /* Row inputs with pull-ups: PA2, PA5, PA6, PA7 */
    {
        uint32_t row_a = (1u<<2)|(1u<<5)|(1u<<6)|(1u<<7);
        FM1_PR(FM1_PA, FM1_DIE) |=  row_a;
        FM1_PR(FM1_PA, FM1_DIR) |=  row_a;   /* input */
        FM1_PR(FM1_PA, FM1_PU)  |=  row_a;
        FM1_PR(FM1_PA, FM1_PD)  &= ~row_a;
    }

    /* Row inputs with pull-ups: PA9, PH6, PH8, PH9  — NEVER drive as output */
    {
        FM1_PR(FM1_PA, FM1_DIE) |=  1u << 9;
        FM1_PR(FM1_PA, FM1_DIR) |=  1u << 9;
        FM1_PR(FM1_PA, FM1_PU)  |=  1u << 9;
        FM1_PR(FM1_PA, FM1_PD)  &= ~(1u << 9);
    }
    {
        uint32_t row_h = (1u<<6)|(1u<<8)|(1u<<9);
        FM1_PR(FM1_PH, FM1_DIE) |=  row_h;
        FM1_PR(FM1_PH, FM1_DIR) |=  row_h;
        FM1_PR(FM1_PH, FM1_PU)  |=  row_h;
        FM1_PR(FM1_PH, FM1_PD)  &= ~row_h;
    }

    /* ADC analog inputs: faders on mux C = PA10 analog; PC9 stays analog */
    {
        uint32_t m = 1u << 10;
        FM1_PR(FM1_PA, FM1_DIE) &= ~m;
        FM1_PR(FM1_PA, FM1_PU)  &= ~m;
        FM1_PR(FM1_PA, FM1_PD)  &= ~m;
        FM1_PR(FM1_PA, FM1_DIR) |=  m;
    }
    /* PA8 (mux A) — analog */
    {
        uint32_t m = 1u << 8;
        FM1_PR(FM1_PA, FM1_DIE) &= ~m;
        FM1_PR(FM1_PA, FM1_PU)  &= ~m;
        FM1_PR(FM1_PA, FM1_PD)  &= ~m;
        FM1_PR(FM1_PA, FM1_DIR) |=  m;
    }

    /* PC10 (WS2812 data): output, low between frames */
    {
        uint32_t m = 1u << 10;
        FM1_PR(FM1_PC, FM1_DIE) |=  m;
        FM1_PR(FM1_PC, FM1_OUT) &= ~m;
        FM1_PR(FM1_PC, FM1_DIR) &= ~m;
    }

    /* Button-LED common driver init: keep GPIO-mode bits set, both commons off */
    SMK_LED_CMN_REG = (SMK_LED_CMN_REG | SMK_LED_CMN_BITS) | SMK_LED_CMN0 | SMK_LED_CMN1;

    /* Seed encoder prev to 0xFF (first frame re-learns) */
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;

    /* Zero RGB frame (reset bytes already zero; LED bytes will be set by caller) */
    for (i = 0; i < SMK_RGB_FRAME_BYTES; i++)
        smk_rgb[i] = 0;

    /* Shift all-ones to turn all columns off, then latch */
    smk__col_buf = 0xFFFFFFFFu;
    smk__sr_dma_start();
    smk__sr_dma_wait();
    smk__sr_latch();

    /* Start with column 0 */
    smk_tick_col = 0;
}

/* ================================================================
 * fm1_input_tick — called from a ~10 kHz timer ISR
 * One column per call:
 *   1. Take ADC result from previous tick (if pending)
 *   2. Read rows belonging to the column latched one tick earlier
 *   3. Debounce keys/buttons for that column
 *   4. Shift and latch the next column word (with LED bits and mux address)
 *   5. On frame boundary (col wraps to 0): run encoders, advance ADC step,
 *      toggle button-LED group, flush animation frame
 * ================================================================ */
static void fm1_input_tick(void)
{
    uint32_t p = smk_tick_col;
    uint32_t n = (p + 1u) & (FM1_NCOL - 1u);
    uint32_t t0 = fm1_ticks();

    /* 1. Take ADC result from last tick */
    if (smk_an.pend)
        smk__adc_take();

    /* 2. Read rows (column p was latched last tick) */
    smk_raw[p] = smk__rows();

    /* 3. Debounce keys/buttons for column p */
    smk__process_col(p);

    /* 4. Build next column word and start SPI2 DMA */
    {
        /* Mux address: use the ADC step that will be scheduled this frame.
         * We need to write the mux address one step before converting,
         * so use the NEXT step's address. */
        uint32_t next_step = (smk_an.step + 1u) % 24u;
        uint32_t mux_addr  = next_step % 8u;
        uint16_t led_word  = smk__btnled_word(n);
        smk__col_buf = smk__col_word(n, mux_addr, (uint32_t)led_word);
        smk__sr_dma_start();
        smk__sr_dma_wait();   /* SPI2 is fast at BAUD 5; wait inline */
        smk__sr_latch();
    }

    /* 5. Frame boundary */
    if (n == 0u) {
        /* Toggle button-LED common */
        if (smk_led_grp == 0u) {
            SMK_LED_CMN_REG = (SMK_LED_CMN_REG | SMK_LED_CMN_BITS | SMK_LED_CMN1) & ~SMK_LED_CMN0;
        } else {
            SMK_LED_CMN_REG = (SMK_LED_CMN_REG | SMK_LED_CMN_BITS | SMK_LED_CMN0) & ~SMK_LED_CMN1;
        }
        smk_led_grp ^= 1u;

        smk__frame();

        if (fm1__an_f)
            fm1__anim_frame();   /* power-on sweep, same as FM-1 */
    }

    smk_tick_col = (uint8_t)n;

    /* Diagnostics */
    {
        uint32_t g = t0 - smk_diag.last;
        smk_diag.last = t0;
        if (g > smk_diag.gap_max)
            smk_diag.gap_max = g;
        uint32_t d = fm1_ticks() - t0;
        smk_diag.cost_sum += d;
        smk_diag.ticks++;
        if (d > smk_diag.cost_max)
            smk_diag.cost_max = d;
    }
}

/* ================================================================
 * fm1_input_scan — blocking full frame (used in polling / host mode)
 * ================================================================ */
static void fm1_input_scan(void)
{
    uint32_t p;
    for (p = 0; p < FM1_NCOL; p++) {
        uint32_t mux_addr = smk_an.step % 8u;
        uint16_t led_word = smk__btnled_word(p);
        smk__col_buf = smk__col_word(p, mux_addr, (uint32_t)led_word);
        smk__sr_dma_start();
        smk__sr_dma_wait();
        smk__sr_latch();

        /* Wait for settle then read rows */
        uint32_t t0 = fm1_ticks(), span = FM1_SETTLE_US * FM1_TICKS_PER_US;
        while ((uint32_t)(fm1_ticks() - t0) < span)
            FM1_INPUT_IDLE();

        smk_raw[p] = smk__rows();
        smk__process_col(p);
    }
    smk__frame();
}

/* ================================================================
 * IRQ-safe edge APIs (same signature as FM-1)
 * ================================================================ */
static inline uint32_t fm1__lock(void)
{
    __asm__ volatile("cli" ::: "memory");
    return 0;
}
static inline void fm1__unlock(uint32_t v)
{
    (void)v;
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.pressed;
    if (released) *released = fm1_in.released;
    fm1_in.pressed = fm1_in.released = 0;
    fm1__unlock(k);
    return p;
}

static uint32_t fm1_input_note_edges(void)
{
    uint32_t k = fm1__lock();
    uint32_t p = fm1_in.notes_pressed;
    fm1_in.notes_pressed = 0;
    fm1__unlock(k);
    return p;
}

static int32_t fm1_enc_take(uint32_t e)
{
    uint32_t k = fm1__lock();
    int32_t s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    fm1__unlock(k);
    return s;
}

/* ================================================================
 * LED helpers: button LEDs (fm1_led_key / fm1_led_btn)
 * Map key id or button id to the 595's button LED bits.
 * RGB pad LEDs are driven via smk_rgb_set_pad().
 * ================================================================ */
static inline void fm1_led_key(uint32_t id, int on)
{
    /* On the SMK there are no per-key LEDs (piano keys have no LEDs).
     * Key ids 0..36 are note keys; button ids 37..54 map to button LEDs. */
    if (id >= FM1_NKEY_NOTES && (id - FM1_NKEY_NOTES) < FM1_NKEY_BTNS) {
        uint32_t b = id - FM1_NKEY_NOTES;
        uint32_t grp = SMK_BTNLED_GRP[b];
        uint32_t bit = SMK_BTNLED_BIT[b];
        uint32_t c;
        for (c = 0; c < FM1_NCOL; c++) {
            if (on)
                smk_btnled[grp][c] |=  (uint8_t)(1u << bit);
            else
                smk_btnled[grp][c] &= ~(uint8_t)(1u << bit);
        }
    }
}

static inline void fm1_led_btn(uint32_t id, uint8_t *arr, uint32_t on)
{
    (void)arr;  /* SMK uses smk_btnled[], not the fm1_led[] column buffer */
    if (id < FM1_NKEY_BTNS) {
        uint32_t grp = SMK_BTNLED_GRP[id];
        uint32_t bit = SMK_BTNLED_BIT[id];
        uint32_t c;
        for (c = 0; c < FM1_NCOL; c++) {
            if (on)
                smk_btnled[grp][c] |=  (uint8_t)(1u << bit);
            else
                smk_btnled[grp][c] &= ~(uint8_t)(1u << bit);
        }
    }
}

static inline void fm1_led_dim_level(uint32_t lo) { (void)lo; }

/* ================================================================
 * SMK-specific: set an RGB pad LED (hardware pad index 0..15, GRB 0..255)
 * Call smk_rgb_flush() from the main loop after updating pads.
 * ================================================================ */
static void smk_rgb_set_pad(uint32_t pad_hw, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad_hw >= 16u) return;
    smk__rgb_encode(SMK_PAD_CHAIN[pad_hw], g, r, b);
    smk_rgb_dirty = 1;
}

/* Convenience: set by step (reading order 0..15, where 0=step 1) */
static void smk_rgb_set_step(uint32_t step, uint8_t r, uint8_t g, uint8_t b)
{
    /* Reading order: steps 0..7 → top row = hardware pads 0..7;
     *               steps 8..15 → bottom row = hardware pads 8..15 */
    smk_rgb_set_pad(step, r, g, b);
}

/* Clear all pad LEDs */
static void smk_rgb_clear(void)
{
    uint32_t i;
    for (i = SMK_RGB_RESET_BYTES; i < SMK_RGB_FRAME_BYTES; i++)
        smk_rgb[i] = 0;
    smk_rgb_dirty = 1;
}

/* ================================================================
 * Power-on sweep stubs (fm1_led_anim.h interface)
 * ================================================================ */
/* (fm1__an_f, fm1__an_glow, fm1__an_idle, fm1__an_cb, fm1__an_acc
 *  declared above before fm1_input_tick to avoid forward-ref errors.) */

static __attribute__((noinline)) void fm1__anim_frame(void)
{
    /* SMK: drive the power-on sweep through the RGB pads and button LEDs.
     * The sweep uses fm1_led[] / fm1_led_dim[] shape from fm1_led_anim.h
     * which is sized for FM1_NCOL=16. Map column/row → pad LED.
     * For now: a simple all-off / all-on is sufficient for bring-up. */
    uint32_t f = fm1__an_f ? fm1__an_f - 1u : 0u;
    (void)f;
    if (fm1_in.notes | fm1_in.buttons) {
        fm1__an_f = 0;
        return;
    }
    if (!fm1__an_f)
        return;
    fm1__an_f = (uint16_t)(fm1__an_idle ? fm1__an_f : (fm1__an_f >= FM1_ANIM_FRAMES ? 0u : fm1__an_f + 1u));
}

static void fm1__anim_begin(uint32_t glow, uint32_t idle)
{
    fm1__an_glow = glow != 0u;
    fm1__an_idle = idle != 0u;
    fm1__an_f    = 1u;
    smk_rgb_clear();
}
static void fm1_led_anim_start(uint32_t glow) { fm1__anim_begin(glow, 0); }
static int  fm1_led_anim_on(void)  { return fm1__an_f != 0u; }
static void fm1_led_idle_start(uint32_t glow) { fm1__anim_begin(glow, 1); }
static int  fm1_led_idle_on(void)  { return fm1__an_f != 0u && fm1__an_idle; }
static void fm1_led_anim_stop(void) { fm1__an_f = 0; }
