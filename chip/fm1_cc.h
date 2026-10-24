/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* BOARD_SMK37: 1 when building for the M-VAVE SMK-37, 0 for the FM-1 (the default).
 * Set by the build system (-DBOARD_SMK37=1); never define it in app code. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
