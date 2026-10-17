// qmk-armed hooks: colour palette, two-colour effects, Fn colour, light bar, auto-sleep, screen
// (the screen animation upload itself is in screen.c)
#include "quantum.h"

#ifdef ARMED_ENABLE

#    include "armed.h"
#    include "type84_pro.h"
#    include "strip.h"
#    include "wireless.h"
#    include "spiflash.h"
#    include "snaptap.h"
#    ifdef QUANTUM_PAINTER_ENABLE
#        include "ui.h"
#    endif

// Past the stock firmware's write limit (0xFF5000), one sector per slot; HUD screens go down from
// 0xFFB000, so the first one stays where 1.x kept the only one
#    define THEME_BASE 0xFFC000UL
#    define HUD_BASE 0xFFB000UL
#    define HUD_ADDR(screen) (HUD_BASE - (uint32_t)(screen) * SPIFLASH_SECTOR)

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

#    ifdef QUANTUM_PAINTER_ENABLE
bool armed_hud_load_kb(uint8_t screen, uint8_t *data, uint16_t size) {
    if (screen >= ARMED_HUD_SCREENS || size > SPIFLASH_SECTOR) return false;
    spiflash_read(SPIFLASH_USER, HUD_ADDR(screen), data, size);
    return true;
}

// First byte last: a write cut short leaves no magic, so the HUD falls back to the default layout
bool armed_hud_save_kb(uint8_t screen, const uint8_t *data, uint16_t size) {
    uint32_t addr = HUD_ADDR(screen);
    if (screen >= ARMED_HUD_SCREENS || size > SPIFLASH_SECTOR || !spiflash_erase(SPIFLASH_USER, addr, SPIFLASH_SECTOR)) return false;
    for (int32_t off = (size - 1) / SPIFLASH_PAGE * SPIFLASH_PAGE; off >= 0; off -= SPIFLASH_PAGE) {
        if (!spiflash_program(SPIFLASH_USER, addr + off, data + off, MIN(SPIFLASH_PAGE, size - off))) return false;
    }
    return true;
}
#    endif

uint16_t armed_features_kb(void) {
    uint16_t f = ARMED_F_COLOR2 | ARMED_F_FN_COLOR | ARMED_F_STRIP | ARMED_F_SLEEP | ARMED_F_SNAPTAP | ARMED_F_BT_SLOT;
#    ifdef QUANTUM_PAINTER_ENABLE
    f |= ARMED_F_SCREEN | ARMED_F_CLOCK | ARMED_F_DISPLAY;
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
        case ARMED_L_SNAPTAP_MODES:
            return SNAPTAP_MODES;
#    ifdef QUANTUM_PAINTER_ENABLE
        case ARMED_L_ACCENTS:
            return ui_accent_count();
        case ARMED_L_SCREEN_OFF:
            return ui_screen_off_count();
#    endif
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
        case ARMED_L_SNAPTAP_MODES:
            armed_put_name(out, length, snaptap_mode_name(idx));
            return true;
#    ifdef QUANTUM_PAINTER_ENABLE
        case ARMED_L_ACCENTS: // like the colour presets: H, S, name
            armed_put_name(&out[2], length - 2, ui_accent_name(idx, &out[0], &out[1]));
            return true;
        case ARMED_L_SCREEN_OFF: { // seconds (LE), 0 = never; name
            uint16_t sec;
            armed_put_name(&out[2], length - 2, ui_screen_off_name(idx, &sec));
            out[0] = sec & 0xFF;
            out[1] = sec >> 8;
            return true;
        }
#    endif
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
        case ARMED_V_SNAPTAP:
            *out = snaptap_mode();
            return true;
        case ARMED_V_BT_SLOT:
            *out = wireless_bt_slot();
            return true;
#    ifdef QUANTUM_PAINTER_ENABLE
        case ARMED_V_ACCENT:
            *out = ui_accent();
            return true;
        case ARMED_V_SCREEN_OFF:
            *out = ui_screen_off();
            return true;
        case ARMED_V_SCREEN:
            *out = ui_screen_mode();
            return true;
        case ARMED_V_HUD_SCREEN:
            *out = armed_hud_screen();
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
        case ARMED_V_SNAPTAP:
            if (v >= SNAPTAP_MODES) return false;
            snaptap_set_mode(v);
            return true;
        case ARMED_V_BT_SLOT:
            if (v < 1 || v > 5) return false;
            wireless_select_slot(v);
            return true;
#    ifdef QUANTUM_PAINTER_ENABLE
        case ARMED_V_ACCENT:
            if (v >= ui_accent_count()) return false;
            ui_set_accent(v);
            return true;
        case ARMED_V_SCREEN_OFF:
            if (v >= ui_screen_off_count()) return false;
            ui_set_screen_off(v);
            return true;
        case ARMED_V_SCREEN:
            if (v >= UI_SCREEN_MODES) return false;
            ui_set_screen_mode(v);
            return true;
        case ARMED_V_HUD_SCREEN:
            if (v >= ARMED_HUD_SCREENS) return false;
            ui_set_hud_screen(v);
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
