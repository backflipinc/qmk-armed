// qmk-armed hooks: colour palette, two-colour effects, Fn colour, light bar, auto-sleep
#include "quantum.h"

#ifdef ARMED_ENABLE

#    include "armed.h"
#    include "type84_pro.h"
#    include "strip.h"
#    include "wireless.h"

uint16_t armed_features_kb(void) {
    return ARMED_F_COLOR2 | ARMED_F_FN_COLOR | ARMED_F_STRIP | ARMED_F_SLEEP;
}

uint8_t armed_list_count_kb(uint8_t list) {
    switch (list) {
        case ARMED_L_COLORS:
            return type84_colors_count;
        case ARMED_L_STRIP_MODES:
            return STRIP_MODES;
        case ARMED_L_SLEEP:
            return wireless_sleep_option_count();
    }
    return 0;
}

bool armed_list_entry_kb(uint8_t list, uint8_t idx, uint8_t *out, uint8_t length) {
    switch (list) {
        case ARMED_L_COLORS:
            out[0] = type84_colors[idx].h;
            out[1] = type84_colors[idx].s;
            armed_put_name(&out[2], length - 2, type84_colors[idx].name);
            return true;
        case ARMED_L_STRIP_MODES:
            armed_put_name(out, length, strip_mode_name(idx));
            return true;
        case ARMED_L_SLEEP:
            out[0] = wireless_sleep_option_minutes(idx);
            return true;
    }
    return false;
}

uint8_t armed_effect_flags_kb(uint8_t mode) {
    return mode >= RGB_MATRIX_CUSTOM_TWO_TONE && mode < RGB_MATRIX_EFFECT_MAX ? ARMED_EFFECT_COLOR2 : 0;
}

bool armed_get_value_kb(uint8_t id, uint8_t *out) {
    switch (id) {
        case ARMED_V_COLOR2:
            *out = type84_color2_index();
            return true;
        case ARMED_V_FN_COLOR:
            *out = type84_fn_color_index();
            return true;
        case ARMED_V_STRIP_MODE:
            *out = strip_mode();
            return true;
        case ARMED_V_STRIP_COLOR:
            *out = strip_color_index();
            return true;
        case ARMED_V_STRIP_CAPS:
            *out = strip_caps_alert();
            return true;
        case ARMED_V_SLEEP:
            *out = wireless_sleep_index();
            return true;
    }
    return false;
}

bool armed_set_value_kb(uint8_t id, uint8_t v) {
    switch (id) {
        case ARMED_V_COLOR2:
            if (v >= type84_colors_count) return false;
            type84_color2_set_index(v);
            return true;
        case ARMED_V_FN_COLOR:
            if (v >= type84_colors_count) return false;
            type84_fn_color_set_index(v);
            return true;
        case ARMED_V_STRIP_MODE:
            if (v >= STRIP_MODES) return false;
            strip_set_mode(v);
            return true;
        case ARMED_V_STRIP_COLOR:
            if (v >= type84_colors_count) return false;
            strip_set_color_index(v);
            return true;
        case ARMED_V_STRIP_CAPS:
            strip_set_caps_alert(v);
            return true;
        case ARMED_V_SLEEP:
            if (v >= wireless_sleep_option_count()) return false;
            wireless_sleep_set_index(v);
            return true;
    }
    return false;
}

#endif
