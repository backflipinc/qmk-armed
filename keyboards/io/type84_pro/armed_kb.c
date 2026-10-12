// qmk-armed hooks: colour palette, two-colour effects, Fn colour, light bar, auto-sleep, screen
// (the screen animation upload itself is in screen.c)
#include "quantum.h"

#ifdef ARMED_ENABLE

#    include "armed.h"
#    include "type84_pro.h"
#    include "strip.h"
#    include "wireless.h"
#    include "spiflash.h"
#    ifdef QUANTUM_PAINTER_ENABLE
#        include "ui.h"
#    endif

// Past the stock firmware's write limit (0xFF5000), one sector per slot
#    define THEME_BASE 0xFFC000UL

bool armed_theme_load_kb(uint8_t slot, uint8_t *data, uint16_t size) {
    if (slot >= ARMED_THEMES || size > SPIFLASH_SECTOR) return false;
    spiflash_read(SPIFLASH_USER, THEME_BASE + slot * SPIFLASH_SECTOR, data, size);
    return true;
}

// Last page first: a write cut short leaves the header erased, so the slot falls back to defaults
bool armed_theme_save_kb(uint8_t slot, const uint8_t *data, uint16_t size) {
    uint32_t addr = THEME_BASE + slot * SPIFLASH_SECTOR;
    if (slot >= ARMED_THEMES || size > SPIFLASH_SECTOR || !spiflash_erase(SPIFLASH_USER, addr, SPIFLASH_SECTOR)) return false;
    for (int32_t off = (size - 1) / SPIFLASH_PAGE * SPIFLASH_PAGE; off >= 0; off -= SPIFLASH_PAGE) {
        if (!spiflash_program(SPIFLASH_USER, addr + off, data + off, MIN(SPIFLASH_PAGE, size - off))) return false;
    }
    return true;
}

uint16_t armed_features_kb(void) {
    uint16_t f = ARMED_F_COLOR2 | ARMED_F_FN_COLOR | ARMED_F_STRIP | ARMED_F_SLEEP;
#    ifdef QUANTUM_PAINTER_ENABLE
    f |= ARMED_F_SCREEN | ARMED_F_CLOCK;
#    endif
    return f;
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
    return mode >= RGB_MATRIX_CUSTOM_TWO_TONE && mode < RGB_MATRIX_EFFECT_MAX && armed_theme_slot(mode) < 0 ? ARMED_EFFECT_COLOR2 : 0;
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
#    ifdef QUANTUM_PAINTER_ENABLE
        case ARMED_V_SCREEN:
            *out = ui_screen_mode();
            return true;
        case ARMED_V_CLOCK:
            *out = ui_clock_format();
            return true;
#    endif
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
#    ifdef QUANTUM_PAINTER_ENABLE
        case ARMED_V_SCREEN:
            if (v >= UI_SCREEN_MODES) return false;
            ui_set_screen_mode(v);
            return true;
        case ARMED_V_CLOCK:
            if (v >= UI_CLOCK_FORMATS) return false;
            ui_set_clock_format(v);
            return true;
#    endif
    }
    return false;
}

#endif
