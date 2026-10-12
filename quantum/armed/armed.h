// Copyright 2026 backflipinc
// SPDX-License-Identifier: GPL-2.0-or-later

// qmk-armed raw HID protocol: settings VIA / Vial don't cover, read by vial-gui-armed
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define ARMED_COMMAND_ID 0xAD
#define ARMED_PROTOCOL 1

#define ARMED_VERSION_MAJOR 1
#define ARMED_VERSION_MINOR 4
#define ARMED_VERSION_PATCH 0

enum armed_command {
    ARMED_INFO,
    ARMED_LIST,
    ARMED_GET,
    ARMED_SET,
    ARMED_SAVE,
    ARMED_SCREEN_INFO,
    ARMED_SCREEN_BEGIN,
    ARMED_SCREEN_DATA,
    ARMED_SCREEN_END,
    ARMED_SCREEN_READ,
    ARMED_THEME_INFO,
    ARMED_THEME_READ,
    ARMED_THEME_WRITE,
    ARMED_THEME_SAVE,
    ARMED_THEME_REVERT,
    ARMED_CLOCK_GET,
    ARMED_CLOCK_SET,
};

enum armed_feature {
    ARMED_F_RGB_MATRIX = 1 << 0,
    ARMED_F_COLOR2     = 1 << 1,
    ARMED_F_FN_COLOR   = 1 << 2,
    ARMED_F_STRIP      = 1 << 3,
    ARMED_F_SLEEP      = 1 << 4,
    ARMED_F_SCREEN     = 1 << 5,
    ARMED_F_THEMES     = 1 << 6,
    ARMED_F_CLOCK      = 1 << 7,
};

enum armed_list {
    ARMED_L_EFFECTS,
    ARMED_L_COLORS,
    ARMED_L_STRIP_MODES,
    ARMED_L_SLEEP,
    ARMED_L_LEDS,
    ARMED_L_COUNT,
};

enum armed_value {
    ARMED_V_ENABLED,
    ARMED_V_EFFECT,
    ARMED_V_COLOR2,
    ARMED_V_FN_COLOR,
    ARMED_V_STRIP_MODE,
    ARMED_V_STRIP_COLOR,
    ARMED_V_STRIP_CAPS,
    ARMED_V_SLEEP,
    ARMED_V_HUE,
    ARMED_V_SAT,
    ARMED_V_VAL,
    ARMED_V_SPEED,
    ARMED_V_SCREEN,
    ARMED_V_CLOCK,
};

// effect flags in the effect list
#define ARMED_EFFECT_COLOR2 0x01
#define ARMED_EFFECT_THEME 0x02

void armed_raw_hid_receive(uint8_t *data, uint8_t length);

// copies a name, '_' becomes ' '
void armed_put_name(uint8_t *dst, uint8_t length, const char *name);

// keyboard hooks for everything beyond RGB Matrix
uint16_t armed_features_kb(void);
uint8_t  armed_list_count_kb(uint8_t list);
bool     armed_list_entry_kb(uint8_t list, uint8_t index, uint8_t *out, uint8_t length);
uint8_t  armed_effect_flags_kb(uint8_t mode);
bool     armed_get_value_kb(uint8_t id, uint8_t *value);
bool     armed_set_value_kb(uint8_t id, uint8_t value);

// ARMED_F_SCREEN: an animation for the keyboard's screen, see docs/armed.md
#define ARMED_SCREEN_HEADER 256  // [0] frames, [1 + i] delay of frame i in 2 ms units, rest 0xFF
#define ARMED_SCREEN_RGB565_LE 1 // frames: pixels row by row, RGB565, low byte first

typedef struct {
    uint16_t width, height;
    uint8_t  format;
    uint8_t  max_frames;
    uint8_t  frames;   // stored now, 0 = none
    uint32_t capacity; // bytes, header included
} armed_screen_info_t;

void armed_screen_receive(uint8_t *data, uint8_t length);

bool armed_screen_info_kb(armed_screen_info_t *info);
// An upload: begin, the whole animation written in order, end. end(false) aborts.
// read must return anything written at least one frame before the current position.
bool armed_screen_begin_kb(uint32_t size);
bool armed_screen_write_kb(const uint8_t *data, uint16_t length);
bool armed_screen_end_kb(bool commit);
bool armed_screen_read_kb(uint32_t offset, uint8_t *data, uint16_t length);

// ARMED_THEMES (1..8) in config.h + armed_theme_effects.inc in rgb_matrix_kb.inc, see docs/armed.md
#define ARMED_THEME_FORMAT 1

void armed_theme_receive(uint8_t *data, uint8_t length);
int8_t armed_theme_slot(uint8_t mode);
bool armed_theme_name(uint8_t slot, char *buf, uint8_t length);
bool armed_theme_uses_layers(uint8_t slot);

bool armed_theme_load_kb(uint8_t slot, uint8_t *data, uint16_t size);
bool armed_theme_save_kb(uint8_t slot, const uint8_t *data, uint16_t size);

// ARMED_F_CLOCK: the keyboard keeps local wall time, on the wire as seconds since 1970-01-01 in the host's time zone
typedef struct {
    uint16_t year;
    uint8_t  month, day, weekday; // weekday 0 = Monday, filled in by armed_clock_now
    uint8_t  hour, minute, second;
} armed_time_t;

bool armed_clock_now(armed_time_t *t);
bool armed_clock_get_kb(armed_time_t *t);
bool armed_clock_set_kb(const armed_time_t *t);
