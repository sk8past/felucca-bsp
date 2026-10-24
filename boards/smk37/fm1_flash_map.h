/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Peter Whitney */
/* SMK-37 flash map: overrides fm1_flash.h's FM-1 defaults via #ifndef guards.
 * Include this before fm1_flash.h, which happens automatically because
 * smk37/fm1_input.h includes it, and fm1_input.h (board) is compiled before
 * fm1_flash.h (chip) in unity.c's include order.
 *
 * SMK-37 flash (ZB25Q08B, 1 MiB, JEDEC 0xEB6014):
 *   0x00000-0x03FFF  head / SPL            PROTECTED – never touch
 *   0x04000-0x9CFFF  app area (PRCT)       firmware image
 *   0x9D000-0xBAFFF  firmware store (VM)   OBJ_PROJ, OBJ_PANEL, OBJ_BLE…
 *   0xBB000-0xBFFFF  OTA staging           update packages
 *   0xC0000-0xC0FFF  BTIF                  PROTECTED – never touch
 *   0xC1000-0xC1FFF  USRTRIM               PROTECTED – factory calibration at 0xC1224
 *   0xC2000-0xEAFFF  USRFLASH              user samples / patches
 *   0xF4000-0xFDFFF  USR                   PROTECTED – never touch
 *   0xFF000-0xFFFFF  key_mac               PROTECTED – never touch
 */
#pragma once

/* Plain XIP window: SMK store starts at 0x9D000, so the unencrypted window
 * must begin there (fm1_flash.h's fl_plain_window_init uses this macro). */
#ifndef FL_PLAIN_WINDOW_LO
#define FL_PLAIN_WINDOW_LO  0x9D000u
#endif

/* Firmware store window (OBJ_PROJ, OBJ_PANEL, OBJ_BLE…): 0x9D000-0xBAFFF */
#ifndef FL_DATA_LO
#define FL_DATA_LO          0x9D000u
#endif
#ifndef FL_DATA_HI
#define FL_DATA_HI          0xBB000u
#endif

/* No separate superblock on SMK: the FM-1's 0xFC000 region falls in the
 * protected USR area here.  Point GLOB at the data window so any code that
 * checks FL_STORE_OK still passes for valid store offsets. */
#ifndef FL_GLOB_LO
#define FL_GLOB_LO          FL_DATA_LO
#endif
#ifndef FL_GLOB_HI
#define FL_GLOB_HI          FL_DATA_HI
#endif

/* OTA staging (update packages): 0xBB000-0xBFFFF */
#ifndef FL_OTA_LO
#define FL_OTA_LO           0xBB000u
#endif
#ifndef FL_OTA_HI
#define FL_OTA_HI           0xC0000u
#endif

/* No separate autosave slot on SMK: alias to OTA area (never both active). */
#ifndef FL_AUTO_LO
#define FL_AUTO_LO          FL_OTA_LO
#endif
#ifndef FL_AUTO_HI
#define FL_AUTO_HI          FL_OTA_HI
#endif

/* JEDEC ID of the SMK-37's SPI flash (ZB25Q08B, 1 MiB). */
#ifndef FL_JEDEC_OK
#define FL_JEDEC_OK(id)     ((id) == 0xEB6014u)
#endif

/* Safety checks: must be compile-time, not run-time. */
_Static_assert(FL_DATA_LO  >= 0x4000u,  "SMK store must sit above the app head");
_Static_assert(FL_DATA_HI  <= 0xC0000u, "SMK store must not reach protected BTIF");
_Static_assert(FL_OTA_HI   <= 0xC0000u, "SMK OTA staging must not reach protected BTIF");
_Static_assert(FL_OTA_LO   >= FL_DATA_HI, "SMK OTA staging must not overlap store");
