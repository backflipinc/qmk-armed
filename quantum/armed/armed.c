// Copyright 2026 backflipinc
// SPDX-License-Identifier: GPL-2.0-or-later

#include "armed.h"
#include "quantum.h"
#include "via.h"

__attribute__((weak)) uint16_t armed_features_kb(void) {
    return 0;
}

__attribute__((weak)) uint8_t armed_list_count_kb(uint8_t list) {
    return 0;
}

__attribute__((weak)) bool armed_list_entry_kb(uint8_t list, uint8_t index, uint8_t *out, uint8_t length) {
    return false;
}

__attribute__((weak)) uint8_t armed_effect_flags_kb(uint8_t mode) {
    return 0;
}

__attribute__((weak)) bool armed_get_value_kb(uint8_t id, uint8_t *value) {
    return false;
}

__attribute__((weak)) bool armed_set_value_kb(uint8_t id, uint8_t value) {
    return false;
}

void armed_put_name(uint8_t *dst, uint8_t length, const char *name) {
    uint8_t i = 0;
    for (; name[i] && i < length - 1; i++) dst[i] = name[i] == '_' ? ' ' : name[i];
    dst[i] = 0;
}

static uint16_t features(void) {
    uint16_t f = armed_features_kb();
#ifdef RGB_MATRIX_ENABLE
    f |= ARMED_F_RGB_MATRIX;
#endif
    return f;
}

static uint8_t list_count(uint8_t list) {
    switch (list) {
#ifdef RGB_MATRIX_ENABLE
        case ARMED_L_EFFECTS:
            return RGB_MATRIX_EFFECT_MAX - 1;
        case ARMED_L_LEDS:
            return RGB_MATRIX_LED_COUNT;
#endif
        default:
            return armed_list_count_kb(list);
    }
}

#ifdef RGB_MATRIX_ENABLE
static void led_entry(uint8_t idx, uint8_t *out) {
    out[0] = out[1] = 0xFF;
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < MATRIX_COLS; c++) {
            if (g_led_config.matrix_co[r][c] == idx) {
                out[0] = r;
                out[1] = c;
            }
        }
    }
    out[2] = g_led_config.point[idx].x;
    out[3] = g_led_config.point[idx].y;
    out[4] = g_led_config.flags[idx];
}
#endif

// data[2] list, data[3] index -> data[4..] entry
static bool list_entry(uint8_t *data, uint8_t length) {
    uint8_t list = data[2], idx = data[3];
    if (idx >= list_count(list)) return false;
    switch (list) {
#ifdef RGB_MATRIX_ENABLE
        case ARMED_L_EFFECTS:
            data[4] = idx + 1;
            data[5] = armed_effect_flags_kb(idx + 1);
            armed_put_name(&data[6], length - 6, rgb_matrix_get_mode_name(idx + 1));
            return true;
        case ARMED_L_LEDS:
            led_entry(idx, &data[4]);
            return true;
#endif
        default:
            return armed_list_entry_kb(list, idx, &data[4], length - 4);
    }
}

static bool get_value(uint8_t id, uint8_t *out) {
    switch (id) {
#ifdef RGB_MATRIX_ENABLE
        case ARMED_V_ENABLED:
            *out = rgb_matrix_is_enabled();
            return true;
        case ARMED_V_EFFECT:
            *out = rgb_matrix_get_mode();
            return true;
        case ARMED_V_HUE:
            *out = rgb_matrix_get_hue();
            return true;
        case ARMED_V_SAT:
            *out = rgb_matrix_get_sat();
            return true;
        case ARMED_V_VAL:
            *out = rgb_matrix_get_val();
            return true;
        case ARMED_V_SPEED:
            *out = rgb_matrix_get_speed();
            return true;
#endif
        default:
            return armed_get_value_kb(id, out);
    }
}

static bool set_value(uint8_t id, uint8_t v) {
    switch (id) {
#ifdef RGB_MATRIX_ENABLE
        case ARMED_V_ENABLED:
            v ? rgb_matrix_enable() : rgb_matrix_disable();
            return true;
        case ARMED_V_EFFECT:
            if (v == 0 || v >= RGB_MATRIX_EFFECT_MAX) return false;
            rgb_matrix_mode(v);
            return true;
        // HSV and speed skip EEPROM while a slider moves, ARMED_SAVE writes them
        case ARMED_V_HUE:
            rgb_matrix_sethsv_noeeprom(v, rgb_matrix_get_sat(), rgb_matrix_get_val());
            return true;
        case ARMED_V_SAT:
            rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue(), v, rgb_matrix_get_val());
            return true;
        case ARMED_V_VAL:
            rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue(), rgb_matrix_get_sat(), MIN(v, RGB_MATRIX_MAXIMUM_BRIGHTNESS));
            return true;
        case ARMED_V_SPEED:
            rgb_matrix_set_speed_noeeprom(v);
            return true;
#endif
        default:
            return armed_set_value_kb(id, v);
    }
}

static void info(uint8_t *data) {
    uint16_t f = features();
    data[2]    = 'A';
    data[3]    = 'R';
    data[4]    = 'M';
    data[5]    = 'D';
    data[6]    = ARMED_PROTOCOL;
    data[7]    = ARMED_VERSION_MAJOR;
    data[8]    = ARMED_VERSION_MINOR;
    data[9]    = ARMED_VERSION_PATCH;
    data[10]   = f & 0xFF;
    data[11]   = f >> 8;
    for (uint8_t i = 0; i < ARMED_L_COUNT; i++) data[12 + i] = list_count(i);
#ifdef RGB_MATRIX_ENABLE
    data[12 + ARMED_L_COUNT] = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
#endif
}

void armed_raw_hid_receive(uint8_t *data, uint8_t length) {
    bool ok = true;
    switch (data[1]) {
        case ARMED_INFO:
            info(data);
            break;
        case ARMED_LIST:
            ok = list_entry(data, length);
            break;
        case ARMED_GET:
            ok = get_value(data[2], &data[3]);
            break;
        case ARMED_SET:
            ok = set_value(data[2], data[3]);
            break;
        case ARMED_SAVE:
#ifdef RGB_MATRIX_ENABLE
            eeconfig_force_flush_rgb_matrix();
#endif
            break;
        default:
            ok = false;
            break;
    }
    if (!ok) data[0] = id_unhandled;
}
