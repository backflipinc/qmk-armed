// Copyright 2026 backflipinc
// SPDX-License-Identifier: GPL-2.0-or-later

// qmk-armed raw HID protocol: settings VIA / Vial don't cover, read by vial-gui-armed
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define ARMED_COMMAND_ID 0xAD
#define ARMED_PROTOCOL 1

#define ARMED_VERSION_MAJOR 2
#define ARMED_VERSION_MINOR 1
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
    ARMED_BOOTLOADER,
    ARMED_HUD_INFO,
    ARMED_HUD_SOURCE,
    ARMED_HUD_VALUE,
    ARMED_HUD_FONT,
    ARMED_HUD_PALETTE,
    ARMED_HUD_READ,
    ARMED_HUD_WRITE,
    ARMED_HUD_APPLY,
    ARMED_HUD_REVERT,
    ARMED_HUD_PUSH,
    ARMED_HUD_TEXT,
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
    ARMED_F_HUD        = 1 << 8,
    ARMED_F_KEYMAP     = 1 << 9, // set by the core: NKRO, GUI lock
    ARMED_F_SNAPTAP    = 1 << 10,
    ARMED_F_DISPLAY    = 1 << 11, // UI accent, screen-off timeout
    ARMED_F_BT_SLOT    = 1 << 12,
};

enum armed_list {
    ARMED_L_EFFECTS,
    ARMED_L_COLORS,
    ARMED_L_STRIP_MODES,
    ARMED_L_SLEEP,
    ARMED_L_LEDS,
    ARMED_L_COUNT, // lists sized in INFO; the ones below are read until 0xFF
    ARMED_L_SNAPTAP_MODES = ARMED_L_COUNT,
    ARMED_L_ACCENTS,
    ARMED_L_SCREEN_OFF,
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
    ARMED_V_NKRO,
    ARMED_V_GUI_LOCK,
    ARMED_V_SNAPTAP,
    ARMED_V_ACCENT,
    ARMED_V_SCREEN_OFF,
    ARMED_V_BT_SLOT,
    ARMED_V_HUD_SCREEN, // which stored HUD layout the main screen shows
};

// effect flags in the effect list
#define ARMED_EFFECT_COLOR2 0x01
#define ARMED_EFFECT_THEME 0x02

// false: send no reply (host data pushes)
bool armed_raw_hid_receive(uint8_t *data, uint8_t length);
// requests that shouldn't count as user activity (keep a wireless keyboard from sleeping)
bool armed_background_request(const uint8_t *data);

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

// ARMED_F_HUD: the main screen built from widgets in Vial Armed and fed by the host, see docs/armed.md.
// Set by the core once the keyboard calls armed_hud_init().
#define ARMED_HUD_FORMAT 1
#define ARMED_HUD_MAGIC 0xB7
// layouts the keyboard stores; one is shown at a time
#ifndef ARMED_HUD_SCREENS
#    define ARMED_HUD_SCREENS 1
#endif
#ifndef ARMED_HUD_WIDGETS
#    define ARMED_HUD_WIDGETS 40
#endif
#define ARMED_HUD_STRINGS 512
#define ARMED_HUD_HOST_VALUES 16
#define ARMED_HUD_HOST_TEXTS 4
#define ARMED_HUD_TEXT_LEN 160
#define ARMED_HUD_GRAPHS 4
#define ARMED_HUD_GRAPH_POINTS 64
#define ARMED_HUD_NO_LABEL 0xFFFF

enum armed_hud_widget_type {
    ARMED_HUD_W_NONE,
    ARMED_HUD_W_TEXT,      // label with "{}" replaced by the source's value
    ARMED_HUD_W_BAR,       // param: segments, 0 = solid
    ARMED_HUD_W_GRAPH,     // param: sample period in 100 ms units
    ARMED_HUD_W_INDICATOR, // tile with a dot and the label, lit when the source is non-zero
    ARMED_HUD_W_BATTERY,   // battery icon
    ARMED_HUD_W_FRAME,     // param: armed_hud_frame
    ARMED_HUD_W_TEXTBOX,   // the source's text wrapped over several lines
};

enum armed_hud_frame { ARMED_HUD_FRAME_BRACKETS, ARMED_HUD_FRAME_BOX, ARMED_HUD_FRAME_HLINE, ARMED_HUD_FRAME_VLINE, ARMED_HUD_FRAME_PANEL };

// Colour roles, the keyboard's palette (armed_hud_color_kb) gives them colours
enum armed_hud_color {
    ARMED_HUD_C_BG,
    ARMED_HUD_C_PANEL,
    ARMED_HUD_C_LINE,
    ARMED_HUD_C_DIM,
    ARMED_HUD_C_TEXT,
    ARMED_HUD_C_ACCENT,
    ARMED_HUD_C_OK,
    ARMED_HUD_C_WARN,
    ARMED_HUD_C_COUNT,
    ARMED_HUD_C_AUTO = 0x0F, // the colour the source suggests
};

#define ARMED_HUD_F_ALIGN 0x03 // 0 left, 1 centre, 2 right
#define ARMED_HUD_F_WARN 0x04  // WARN colour once the value reaches `warn`
#define ARMED_HUD_F_WARN_BELOW 0x08
#define ARMED_HUD_F_FILL 0x10 // graph: columns instead of a line
#define ARMED_HUD_F_TAIL 0x20 // text box: show the last lines
#define ARMED_HUD_F_DOT 0x40  // indicator: the dot only

enum armed_hud_source {
    ARMED_HUD_S_NONE,
    ARMED_HUD_S_LAYER,
    ARMED_HUD_S_WPM,
    ARMED_HUD_S_BATTERY,
    ARMED_HUD_S_VOLTAGE,
    ARMED_HUD_S_CHARGING,
    ARMED_HUD_S_LINK,
    ARMED_HUD_S_MODE,
    ARMED_HUD_S_TIME,
    ARMED_HUD_S_DATE,
    ARMED_HUD_S_CAPS,
    ARMED_HUD_S_NUM,
    ARMED_HUD_S_SCROLL,
    ARMED_HUD_S_NKRO,
    ARMED_HUD_S_LIGHT,
    ARMED_HUD_S_GUI_LOCK,
    ARMED_HUD_S_SNAPTAP,
    ARMED_HUD_S_EFFECT,
    ARMED_HUD_S_UPTIME,
    ARMED_HUD_S_BRIGHTNESS,
    ARMED_HUD_S_COUNT,
    ARMED_HUD_S_HOST     = 0x40, // + slot, numbers pushed by the host
    ARMED_HUD_S_HOST_TXT = 0x60, // + slot, text pushed by the host
};

enum armed_hud_kind { ARMED_HUD_K_TEXT, ARMED_HUD_K_NUMBER, ARMED_HUD_K_FLAG, ARMED_HUD_K_LINES };

typedef struct __attribute__((packed)) {
    uint8_t  type, source, font, fg, bg, flags;
    int16_t  x, y, w, h;
    int16_t  lo, hi, warn; // in the source's units before the decimal point is placed
    uint8_t  param, reserved;
    uint16_t label; // offset in strings, ARMED_HUD_NO_LABEL = none
} armed_hud_widget_t;

typedef struct __attribute__((packed)) {
    uint8_t            magic, format, count, reserved;
    armed_hud_widget_t widget[ARMED_HUD_WIDGETS];
    char               strings[ARMED_HUD_STRINGS];
} armed_hud_layout_t;

typedef struct {
    int32_t num; // value * 10^dec
    uint8_t dec;
    uint8_t hint; // suggested colour role
    bool    valid;
    char    text[24]; // text sources; numbers leave it empty
} armed_hud_value_t;

typedef struct {
    const void    *handle; // painter_font_handle_t
    const uint8_t *data;   // the QFF, read by Vial Armed for its preview
    uint32_t       size;
} armed_hud_font_t;

// device: painter_device_t; fonts must stay valid
void armed_hud_init(const void *device, const armed_hud_font_t *fonts, uint8_t count);
// draws the main screen, only what changed unless `all`
void armed_hud_draw(bool all);
bool armed_hud_ready(void);
// the layout shown and edited, 0 .. ARMED_HUD_SCREENS - 1; callable before armed_hud_init()
uint8_t armed_hud_screen(void);
void    armed_hud_set_screen(uint8_t screen);
bool armed_hud_receive(uint8_t *data, uint8_t length); // false: no reply
// for armed_hud_defaults_kb(): appends a widget, NULL when full
armed_hud_widget_t *armed_hud_add(uint8_t type, uint8_t source, uint8_t font, uint8_t fg, uint8_t bg, uint8_t flags, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t param, const char *label);

void armed_hud_defaults_kb(void);
bool armed_hud_source_kb(uint8_t source, armed_hud_value_t *value); // keyboard's own sources, or override the core's
void armed_hud_color_kb(uint8_t role, uint8_t *hsv);
bool armed_hud_load_kb(uint8_t screen, uint8_t *data, uint16_t size);
bool armed_hud_save_kb(uint8_t screen, const uint8_t *data, uint16_t size);
