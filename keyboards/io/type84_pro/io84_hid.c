// Raw HID channel for vial-gui-armed: keyboard settings that VIA / VialRGB don't cover
#include "quantum.h"

#ifdef VIA_ENABLE

#    include "type84_pro.h"
#    include "strip.h"
#    include "wireless.h"

#    define IO84_CMD 0x84
#    define IO84_PROTOCOL 1

enum io84_sub { IO84_INFO, IO84_LIST, IO84_GET, IO84_SET, IO84_SAVE };
enum io84_list { L_EFFECTS, L_COLORS, L_STRIP_MODES, L_SLEEP, L_LEDS, L_COUNT };
enum io84_value { V_ENABLED, V_EFFECT, V_COLOR2, V_FN_COLOR, V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS, V_SLEEP, V_HUE, V_SAT, V_VAL, V_SPEED };

static uint8_t list_count(uint8_t list) {
    switch (list) {
        case L_EFFECTS:
            return RGB_MATRIX_EFFECT_MAX - 1;
        case L_COLORS:
            return type84_colors_count;
        case L_STRIP_MODES:
            return STRIP_MODES;
        case L_SLEEP:
            return wireless_sleep_option_count();
        case L_LEDS:
            return RGB_MATRIX_LED_COUNT;
    }
    return 0;
}

static void put_name(uint8_t *dst, uint8_t len, const char *s) {
    uint8_t i = 0;
    for (; s[i] && i < len - 1; i++) dst[i] = s[i] == '_' ? ' ' : s[i];
    dst[i] = 0;
}

// data[2] list, data[3] index -> data[4..] entry
static void list_entry(uint8_t *data, uint8_t length) {
    uint8_t list = data[2], idx = data[3];
    if (idx >= list_count(list)) {
        data[0] = id_unhandled;
        return;
    }
    switch (list) {
        case L_EFFECTS:
            data[4] = idx + 1;
            put_name(&data[5], length - 5, rgb_matrix_get_mode_name(idx + 1));
            break;
        case L_COLORS:
            data[4] = type84_colors[idx].h;
            data[5] = type84_colors[idx].s;
            put_name(&data[6], length - 6, type84_colors[idx].name);
            break;
        case L_STRIP_MODES:
            put_name(&data[4], length - 4, strip_mode_name(idx));
            break;
        case L_SLEEP:
            data[4] = wireless_sleep_option_minutes(idx);
            break;
        case L_LEDS:
            data[4] = data[5] = 0xFF;
            for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
                for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                    if (g_led_config.matrix_co[r][c] == idx) {
                        data[4] = r;
                        data[5] = c;
                    }
                }
            }
            data[6] = g_led_config.point[idx].x;
            data[7] = g_led_config.point[idx].y;
            data[8] = g_led_config.flags[idx];
            break;
    }
}

static bool get_value(uint8_t id, uint8_t *out) {
    switch (id) {
        case V_ENABLED:
            *out = rgb_matrix_is_enabled();
            return true;
        case V_EFFECT:
            *out = rgb_matrix_get_mode();
            return true;
        case V_COLOR2:
            *out = type84_color2_index();
            return true;
        case V_FN_COLOR:
            *out = type84_fn_color_index();
            return true;
        case V_STRIP_MODE:
            *out = strip_mode();
            return true;
        case V_STRIP_COLOR:
            *out = strip_color_index();
            return true;
        case V_STRIP_CAPS:
            *out = strip_caps_alert();
            return true;
        case V_SLEEP:
            *out = wireless_sleep_index();
            return true;
        case V_HUE:
            *out = rgb_matrix_get_hue();
            return true;
        case V_SAT:
            *out = rgb_matrix_get_sat();
            return true;
        case V_VAL:
            *out = rgb_matrix_get_val();
            return true;
        case V_SPEED:
            *out = rgb_matrix_get_speed();
            return true;
    }
    return false;
}

static bool set_value(uint8_t id, uint8_t v) {
    switch (id) {
        case V_ENABLED:
            v ? rgb_matrix_enable() : rgb_matrix_disable();
            return true;
        case V_EFFECT:
            if (v == 0 || v >= RGB_MATRIX_EFFECT_MAX) return false;
            rgb_matrix_mode(v);
            return true;
        case V_COLOR2:
            if (v >= type84_colors_count) return false;
            type84_color2_set_index(v);
            return true;
        case V_FN_COLOR:
            if (v >= type84_colors_count) return false;
            type84_fn_color_set_index(v);
            return true;
        case V_STRIP_MODE:
            if (v >= STRIP_MODES) return false;
            strip_set_mode(v);
            return true;
        case V_STRIP_COLOR:
            if (v >= type84_colors_count) return false;
            strip_set_color_index(v);
            return true;
        case V_STRIP_CAPS:
            strip_set_caps_alert(v);
            return true;
        case V_SLEEP:
            if (v >= wireless_sleep_option_count()) return false;
            wireless_sleep_set_index(v);
            return true;
        case V_HUE:
            rgb_matrix_sethsv_noeeprom(v, rgb_matrix_get_sat(), rgb_matrix_get_val());
            return true;
        case V_SAT:
            rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue(), v, rgb_matrix_get_val());
            return true;
        case V_VAL:
            rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue(), rgb_matrix_get_sat(), MIN(v, RGB_MATRIX_MAXIMUM_BRIGHTNESS));
            return true;
        case V_SPEED:
            rgb_matrix_set_speed_noeeprom(v);
            return true;
    }
    return false;
}

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (data[0] != IO84_CMD) {
        data[0] = id_unhandled;
        return;
    }
    switch (data[1]) {
        case IO84_INFO:
            data[2] = 'I';
            data[3] = 'O';
            data[4] = '8';
            data[5] = '4';
            data[6] = IO84_PROTOCOL;
            for (uint8_t i = 0; i < L_COUNT; i++) data[7 + i] = list_count(i);
            data[7 + L_COUNT] = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            break;
        case IO84_LIST:
            list_entry(data, length);
            break;
        case IO84_GET:
            if (!get_value(data[2], &data[3])) data[0] = id_unhandled;
            break;
        case IO84_SET:
            if (!set_value(data[2], data[3])) data[0] = id_unhandled;
            break;
        case IO84_SAVE: // HSV and speed are set without EEPROM writes while a slider moves
            eeconfig_force_flush_rgb_matrix();
            break;
        default:
            data[0] = id_unhandled;
            break;
    }
}

#endif
