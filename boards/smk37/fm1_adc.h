/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Peter Whitney */
/* SMK-37 SARADC: battery via mux C; four fader ADC channels; no MASTER pot
 * (K1 encoder is a virtual MASTER pot — fm1_an.master is accumulated by the
 * input tick, not read from hardware).
 *
 * SMK-37 ADC channels (from docs/01-hardware.md):
 *   ch0 / PA7: key matrix row (input to fm1_input.h's tick; same as FM-1 ch0)
 *   ch1 / PA8: key matrix row (same as FM-1 ch1)
 *   ch2:       fader 1 (CC 16)
 *   ch3:       fader 2 (CC 17)
 *   ch4:       fader 3 (CC 18)
 *   ch5:       fader 4 (CC 19)
 *   battery:   mux C (read via a separate mux path, not a direct ADC channel)
 *
 * The FM-1 used PB6 (ch4) for MASTER and PB1 (ch3) for battery.
 * On the SMK those pins have different roles:
 *   PB5 = LCD D/C → not an ADC input
 *   PB6 = LCD CLK → not an ADC input
 *   PB7 = LCD MOSI / matrix → not an ADC input
 * PC9 stays as an analog input (AGENTS.md constraint).
 *
 * API (same as fm1/fm1_adc.h, row channels unchanged):
 *   fm1_adc_init()               configure analog-input pins
 *   fm1_adc_read(ch)             blocking, 0..1023, -1 on timeout
 *   fm1_adc_start(ch, con)       start one conversion
 *   fm1_adc_done()               conversion finished?
 *   fm1_adc_take()               result, converter idle
 *   fm1_adc_abort()              cancel
 *
 * Fader channels (ch2-5) are read once per frame by fm1_input_tick and
 * stored in fm1_an.faders[0..3].  Soft-takeover is handled in plat_smk37.c.
 *
 * Battery level: read from mux C; the exact implementation is Task 3.
 */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"

#define FM1_ADC_CON_R (*(volatile uint32_t *)0x13100u)
#define FM1_ADC_RES   (*(volatile uint32_t *)0x13104u)
#define FM1_WLA_CON0  (*(volatile uint32_t *)0x11900u)

/* Row ADC channels: same numbers as FM-1 (PA7 = ch0, PA8 = ch1) */
enum {
    FM1_ADC_ROW_PA7  = 0,
    FM1_ADC_ROW_PA8  = 1,
    /* No MASTER pot on SMK (K1 is a virtual encoder-based master) */
    FM1_ADC_FADER1   = 2,
    FM1_ADC_FADER2   = 3,
    FM1_ADC_FADER3   = 4,
    FM1_ADC_FADER4   = 5,
    /* FM1_ADC_BATT reads via mux C — Task 3 adds the mux control */
    FM1_ADC_BATT     = 6
};

#define FM1_ADC_CON(div, delay) ((uint32_t)((div) & 7u) | 8u | ((uint32_t)((delay) & 15u) << 12))
#define FM1_ADC_CON_SDK FM1_ADC_CON(6, 15)

/* fm1_adc_init: configure the key-row analog pins (PA7, PA8).
 * Fader pins (Task 3 will add their specific GPIO setup).
 * PC9 stays analog (AGENTS.md constraint; it is not used on the SMK but must
 * not be driven as a digital output). */
static void fm1_adc_init(void)
{
    /* PA7 and PA8 are row return inputs for the key matrix on SMK too:
     * make them floating analog (no pulls, die=0, dir=in). */
    uint32_t m = (1u << 7) | (1u << 8);
    FM1_PR(FM1_PA, FM1_DIE) &= ~m;
    FM1_PR(FM1_PA, FM1_PU)  &= ~m;
    FM1_PR(FM1_PA, FM1_PD)  &= ~m;
    FM1_PR(FM1_PA, FM1_DIR) |=  m;

    /* PC9: analog input, no pulls (AGENTS.md constraint). */
    FM1_PR(FM1_PC, FM1_DIE) &= ~(1u << 9);
    FM1_PR(FM1_PC, FM1_PU)  &= ~(1u << 9);
    FM1_PR(FM1_PC, FM1_PD)  &= ~(1u << 9);
    FM1_PR(FM1_PC, FM1_DIR) |=  (1u << 9);

    /* TODO Task 3: configure fader ADC input pins (ch2-5) and battery mux C. */
}

static void fm1_adc_start(uint32_t ch, uint32_t con)
{
    FM1_ADC_CON_R = 0;
    if (FM1_WLA_CON0 & (1u << 14))
        FM1_WLA_CON0 &= ~(1u << 14);
    FM1_ADC_CON_R = (con & 0xF00Fu) | 0x40u | ((ch & 0xFu) << 8);
    FM1_ADC_CON_R |= 0x10u;
    FM1_ADC_CON_R |= 0x40u;
}
static inline int fm1_adc_done(void) { return (FM1_ADC_CON_R & 0x80u) != 0; }
static inline uint32_t fm1_adc_take(void)
{
    uint32_t v = FM1_ADC_RES & 0x3FFu;
    FM1_ADC_CON_R = 0x40u;
    FM1_ADC_CON_R = 0;
    return v;
}
static inline void fm1_adc_abort(void)
{
    FM1_ADC_CON_R = 0x40u;
    FM1_ADC_CON_R = 0;
}

static int32_t fm1_adc_read(uint32_t ch)
{
    uint32_t t;
    fm1_adc_start(ch, FM1_ADC_CON_SDK);
    for (t = 0; t < 20000u; t++)
        if (fm1_adc_done())
            break;
    if (t == 20000u) {
        fm1_adc_abort();
        return -1;
    }
    return (int32_t)fm1_adc_take();
}
