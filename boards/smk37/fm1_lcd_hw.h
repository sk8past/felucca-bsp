/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Peter Whitney */
/* SMK-37 LCD wiring: ST7789V on SPI1 group A.
 *   CLK  PB6  (SPI1-A SCK)
 *   MOSI PB7  (SPI1-A MOSI)
 *   CS   PC8
 *   D/C  PB5
 *   No hardware backlight pin (screen brightness is always on).
 *
 * The FM-1 uses SPI1 group B (IOMAP_CON1 bit4 = 1: CLK PC9, MOSI PC10).
 * The SMK uses group A (bit4 = 0: CLK PB6, MOSI PB7).
 *
 * API (identical to fm1/fm1_lcd_hw.h):
 *   fm1_lcd_hw_init()          pins, SPI1 master at BAUD 4
 *   fm1_lcd_baud(b)            SPI1 clock = lsb / (b + 1)
 *   fm1_lcd_send_cmd(c)        D/C low, CS low, one byte
 *   fm1_lcd_send_data(p, n)    D/C high, CS low, DMA n bytes from RAM p
 *   fm1_lcd_wait()             SPI done (or timeout, counted), pending cleared
 *   fm1_lcd_deselect()         CS high
 *   fm1_lcd_backlight(on)      no-op on SMK (always on)
 */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

/* Port B (PB5=D/C, PB6=CLK, PB7=MOSI) */
#define FM1_LCD_PB_OUT   (*(volatile uint32_t *)0x50040u)
#define FM1_LCD_PB_DIR   (*(volatile uint32_t *)0x50048u)
/* Port C (PC8=CS) */
#define FM1_LCD_PC_OUT   (*(volatile uint32_t *)0x50080u)
#define FM1_LCD_PC_DIR   (*(volatile uint32_t *)0x50088u)
/* IOMAP_CON1 bit 4: 1 = SPI1 group B (FM-1), 0 = SPI1 group A (SMK) */
#define FM1_LCD_IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
/* SPI1 registers (same addresses on both boards) */
#define FM1_LCD_SPI_CON  (*(volatile uint32_t *)0x11D00u)
#define FM1_LCD_SPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define FM1_LCD_SPI_BUF  (*(volatile uint32_t *)0x11D08u)
#define FM1_LCD_SPI_ADR  (*(volatile uint32_t *)0x11D0Cu)
#define FM1_LCD_SPI_CNT  (*(volatile uint32_t *)0x11D10u)

/* Pin masks */
#define FM1_LCD_DC       (1u << 5)   /* PB5 */
#define FM1_LCD_CLK      (1u << 6)   /* PB6  (SPI1-A; driven by SPI, not GPIO) */
#define FM1_LCD_DO       (1u << 7)   /* PB7  (SPI1-A; driven by SPI, not GPIO) */
#define FM1_LCD_CS       (1u << 8)   /* PC8 */

static uint32_t fm1_lcd_timeouts;

FM1_INLINE void fm1_lcd_hw_init(void)
{
    /* Group A: clear bit 4 of IOMAP_CON1 (FM-1 sets it; SMK clears it). */
    FM1_LCD_IOMAP_CON1 &= ~0x10u;
    /* CS high (deselected), D/C low (command), CLK/DO as SPI output. */
    FM1_LCD_PC_OUT |=  FM1_LCD_CS;
    FM1_LCD_PC_DIR &= ~FM1_LCD_CS;
    FM1_LCD_PB_OUT &= ~(FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_PB_DIR &= ~(FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_SPI_CON  = 0x4021u;
    FM1_LCD_SPI_BAUD = 4u;
}

/* No backlight on SMK: the LCD's LEDK is wired to 3.3 V. */
FM1_INLINE void fm1_lcd_backlight(uint32_t on) { (void)on; }

FM1_INLINE void fm1_lcd_baud(uint32_t b) { FM1_LCD_SPI_BAUD = b; }

static void fm1_lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 4000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u)
        fm1_lcd_timeouts++;
    FM1_LCD_SPI_CON |= 0x4000u;
}

FM1_INLINE void fm1_lcd_deselect(void) { FM1_LCD_PC_OUT |= FM1_LCD_CS; }

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_LCD_PB_OUT &= ~FM1_LCD_DC;   /* D/C low = command (PB5) */
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;   /* CS low (PC8) */
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_BUF  = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_LCD_PB_OUT |=  FM1_LCD_DC;   /* D/C high = data (PB5) */
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;   /* CS low (PC8) */
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR  = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT  = n;
}
