// Интерфейс дисплея 320x172: главный экран (HUD), меню настроек, всплывашки, подтверждения.
// Стиль — минималистичный hi-tech/military: чёрный фон, оттенки серого, один акцентный цвет.
// Макет: docs/ui/mockup.png.
//
// Управление (Fn-слой): Enter — меню/выбор, Ctrl — назад, стрелки — навигация/значение.
// В меню крутилка: поворот — вверх/вниз, нажатие — выбор. Вне меню Fn+стрелки —
// быстрые настройки подсветки (↑↓ яркость, ←→ эффект).
//
// Перерисовка только изменившихся областей: SPI без DMA, полный экран ~75 мс.

#include "quantum.h"

#ifdef QUANTUM_PAINTER_ENABLE

#    include "qp.h"
#    include "usb_util.h"
#    include "ui.h"
#    include "type84_pro.h"
#    include "wireless.h"
#    include "battery.h"
#    include "battery_cw2017.h"
#    include "strip.h"
#    include "graphics/tech14.qff.c"
#    include "graphics/tech20.qff.c"
#    include "graphics/raj44.qff.c"

#    define W 320
#    define H 172

#    define MENU_TIMEOUT_MS 30000
#    define TOAST_MS 1500
#    define ROW_Y0 26
#    define ROW_H 24
#    define ROWS_VISIBLE 5
#    define HEADER_LINE_Y 22
#    define FOOTER_LINE_Y 150

static painter_device_t      lcd;
static painter_font_handle_t f14, f20, f44;

/* --------------------------------------------------------------------------
 * Цвета (HSV QMK)
 * ------------------------------------------------------------------------ */

typedef struct {
    uint8_t h, s, v;
} col_t;

static const col_t C_BG    = {0, 0, 0};
static const col_t C_PANEL = {0, 0, 16};
static const col_t C_LINE  = {0, 0, 40};
static const col_t C_DIM   = {0, 0, 90};
static const col_t C_TXT   = {0, 0, 220};
static const col_t C_OK    = {85, 200, 200};
static const col_t C_WARN  = {0, 220, 230};

// Акцент — настраивается (DISPLAY > ACCENT)
static const struct {
    const char *name;
    col_t       c;
} accents[] = {
    {"AMBER", {28, 255, 255}},
    {"GREEN", {85, 255, 220}},
    {"CYAN", {128, 255, 230}},
    {"RED", {0, 230, 240}},
    {"WHITE", {0, 0, 235}},
};
static col_t C_ACC;

static void rect(int x0, int y0, int x1, int y1, col_t c, bool fill) {
    qp_rect(lcd, x0, y0, x1, y1, c.h, c.s, c.v, fill);
}

static void hline(int x0, int x1, int y, col_t c) {
    qp_line(lcd, x0, y, x1, y, c.h, c.s, c.v);
}

static int text(int x, int y, painter_font_handle_t f, const char *s, col_t fg, col_t bg) {
    return qp_drawtext_recolor(lcd, x, y, f, s, fg.h, fg.s, fg.v, bg.h, bg.s, bg.v);
}

static int text_r(int xr, int y, painter_font_handle_t f, const char *s, col_t fg, col_t bg) {
    int w = qp_textwidth(f, s);
    return text(xr - w, y, f, s, fg, bg);
}

// Уголки рамки в стиле прицела
static void brackets(int x0, int y0, int x1, int y1, col_t c, int l) {
    qp_line(lcd, x0, y0, x0 + l, y0, c.h, c.s, c.v);
    qp_line(lcd, x0, y0, x0, y0 + l, c.h, c.s, c.v);
    qp_line(lcd, x1 - l, y0, x1, y0, c.h, c.s, c.v);
    qp_line(lcd, x1, y0, x1, y0 + l, c.h, c.s, c.v);
    qp_line(lcd, x0, y1, x0 + l, y1, c.h, c.s, c.v);
    qp_line(lcd, x0, y1 - l, x0, y1, c.h, c.s, c.v);
    qp_line(lcd, x1 - l, y1, x1, y1, c.h, c.s, c.v);
    qp_line(lcd, x1, y1 - l, x1, y1, c.h, c.s, c.v);
}

/* --------------------------------------------------------------------------
 * Настройки UI в EEPROM (eeconfig_kb): биты 6-8 — акцент, 9-11 — таймаут экрана.
 * Биты 0-5 — wireless.c (слот, автосон), бит 12 — однократная миграция (type84_pro.c).
 * ------------------------------------------------------------------------ */

static const struct {
    const char *name;
    uint16_t    sec;
} screen_timeouts[] = {{"OFF", 0}, {"30S", 30}, {"1M", 60}, {"5M", 300}, {"10M", 600}};

static uint8_t accent_idx, scr_to_idx;

static void settings_load(void) {
    uint32_t kb = eeconfig_read_kb();
    accent_idx  = (kb >> 6) & 7;
    scr_to_idx  = (kb >> 9) & 7;
    if (accent_idx >= ARRAY_SIZE(accents)) accent_idx = 0;
    if (scr_to_idx >= ARRAY_SIZE(screen_timeouts)) scr_to_idx = 0;
    C_ACC = accents[accent_idx].c;
}

static void settings_save(void) {
    uint32_t kb = eeconfig_read_kb();
    kb          = (kb & ~((7UL << 6) | (7UL << 9))) | ((uint32_t)accent_idx << 6) | ((uint32_t)scr_to_idx << 9);
    eeconfig_update_kb(kb);
    C_ACC = accents[accent_idx].c;
}

/* --------------------------------------------------------------------------
 * Модель меню
 * ------------------------------------------------------------------------ */

enum item_type { IT_SUB, IT_TOGGLE, IT_ENUM, IT_RANGE, IT_ACTION, IT_INFO };

enum item_id {
    // подменю (id = номер страницы)
    P_ROOT,
    P_LIGHT,
    P_STRIP,
    P_WIRELESS,
    P_POWER,
    P_DISPLAY,
    P_SYSTEM,
    P_ABOUT,
    // пункты
    LIGHT_EN,
    LIGHT_FX,
    LIGHT_COLOR,
    LIGHT_COLOR2,
    LIGHT_FNCOLOR,
    LIGHT_VAL,
    LIGHT_HUE,
    LIGHT_SAT,
    LIGHT_SPD,
    ST_MODE,
    ST_COLOR,
    ST_CAPS,
    WL_MODE,
    WL_SLOT,
    WL_PAIR,
    PW_SLEEP,
    PW_BATT,
    PW_VOLT,
    PW_CHG,
    DS_ACCENT,
    DS_TIMEOUT,
    SY_NKRO,
    SY_GUI,
    SY_BOOT,
    SY_RESET,
    AB_FW,
    AB_BUILD,
    AB_MCU,
    AB_UPTIME,
};

typedef struct {
    const char *name;
    uint8_t     type;
    uint8_t     id;
} item_t;

static const item_t items_root[] = {
    {"LIGHTING", IT_SUB, P_LIGHT}, {"LIGHT BAR", IT_SUB, P_STRIP}, {"WIRELESS", IT_SUB, P_WIRELESS}, {"POWER", IT_SUB, P_POWER},
    {"DISPLAY", IT_SUB, P_DISPLAY}, {"SYSTEM", IT_SUB, P_SYSTEM},     {"ABOUT", IT_SUB, P_ABOUT},
};
static const item_t items_light[] = {
    {"ENABLED", IT_TOGGLE, LIGHT_EN}, {"EFFECT", IT_ENUM, LIGHT_FX},  {"COLOR", IT_ENUM, LIGHT_COLOR}, {"COLOR 2", IT_ENUM, LIGHT_COLOR2}, {"FN COLOR", IT_ENUM, LIGHT_FNCOLOR}, {"BRIGHT", IT_RANGE, LIGHT_VAL},
    {"HUE", IT_RANGE, LIGHT_HUE},     {"SATUR", IT_RANGE, LIGHT_SAT}, {"SPEED", IT_RANGE, LIGHT_SPD},
};
static const item_t items_strip[] = {
    {"MODE", IT_ENUM, ST_MODE},
    {"COLOR", IT_ENUM, ST_COLOR},
    {"CAPS ALERT", IT_TOGGLE, ST_CAPS},
};
static const item_t items_wireless[] = {
    {"MODE", IT_INFO, WL_MODE},
    {"BT SLOT", IT_ENUM, WL_SLOT},
    {"PAIR NOW", IT_ACTION, WL_PAIR},
};
static const item_t items_power[] = {
    {"AUTO SLEEP", IT_ENUM, PW_SLEEP},
    {"BATTERY", IT_INFO, PW_BATT},
    {"VOLTAGE", IT_INFO, PW_VOLT},
    {"STATUS", IT_INFO, PW_CHG},
};
static const item_t items_display[] = {
    {"ACCENT", IT_ENUM, DS_ACCENT},
    {"SCREEN OFF", IT_ENUM, DS_TIMEOUT},
};
static const item_t items_system[] = {
    {"NKRO", IT_TOGGLE, SY_NKRO},
    {"WIN LOCK", IT_TOGGLE, SY_GUI},
    {"BOOTLOADER", IT_ACTION, SY_BOOT},
    {"FACTORY RESET", IT_ACTION, SY_RESET},
};
static const item_t items_about[] = {
    {"FIRMWARE", IT_INFO, AB_FW},
    {"BUILD", IT_INFO, AB_BUILD},
    {"MCU", IT_INFO, AB_MCU},
    {"UPTIME", IT_INFO, AB_UPTIME},
};

static const struct {
    const char   *title;
    const item_t *items;
    uint8_t       count;
} pages[] = {
    [P_ROOT]     = {"MENU", items_root, ARRAY_SIZE(items_root)},
    [P_LIGHT]    = {"LIGHTING", items_light, ARRAY_SIZE(items_light)},
    [P_STRIP]    = {"LIGHT BAR", items_strip, ARRAY_SIZE(items_strip)},
    [P_WIRELESS] = {"WIRELESS", items_wireless, ARRAY_SIZE(items_wireless)},
    [P_POWER]    = {"POWER", items_power, ARRAY_SIZE(items_power)},
    [P_DISPLAY]  = {"DISPLAY", items_display, ARRAY_SIZE(items_display)},
    [P_SYSTEM]   = {"SYSTEM", items_system, ARRAY_SIZE(items_system)},
    [P_ABOUT]    = {"ABOUT", items_about, ARRAY_SIZE(items_about)},
};

/* --------------------------------------------------------------------------
 * Состояние UI
 * ------------------------------------------------------------------------ */

typedef enum { SCR_MAIN, SCR_MENU, SCR_CONFIRM } screen_t;

static screen_t screen = SCR_MAIN;
static uint8_t  page;            // текущая страница меню
static uint8_t  sel[ARRAY_SIZE(pages)]; // выбранный пункт на каждой странице
static uint8_t  scroll;          // первая видимая строка
static uint8_t  confirm_id;
static uint16_t last_ui_input;
static bool     full_redraw = true;
static bool     toast_on;
static uint16_t toast_at;
static bool     screen_dark; // таймаут экрана

/* --------------------------------------------------------------------------
 * Значения пунктов
 * ------------------------------------------------------------------------ */

static int8_t      color_find(uint8_t h, uint8_t sat); // готовые цвета подсветки — ниже
static int8_t      color_current(void);
static const char *color_name(int8_t i);

// Какие цвета использует текущий эффект: радужные — ни одного, двухцветные (rgb_matrix_kb.inc) — оба
static bool effect_uses_color(uint8_t n) {
    switch (rgb_matrix_get_mode()) {
        case RGB_MATRIX_CUSTOM_TWO_TONE:
        case RGB_MATRIX_CUSTOM_GRADIENT_2:
        case RGB_MATRIX_CUSTOM_CROSSFADE_2:
        case RGB_MATRIX_CUSTOM_SCANNER_2:
        case RGB_MATRIX_CUSTOM_STARLIGHT_2:
#    ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
        case RGB_MATRIX_CUSTOM_REACTIVE_2:
        case RGB_MATRIX_CUSTOM_RIPPLE_2:
#    endif
            return true;
        case RGB_MATRIX_DIGITAL_RAIN:
        case RGB_MATRIX_TYPING_HEATMAP:
        case RGB_MATRIX_CYCLE_ALL:
        case RGB_MATRIX_CYCLE_LEFT_RIGHT:
        case RGB_MATRIX_CYCLE_UP_DOWN:
        case RGB_MATRIX_RAINBOW_MOVING_CHEVRON:
        case RGB_MATRIX_CYCLE_OUT_IN:
        case RGB_MATRIX_CYCLE_PINWHEEL:
        case RGB_MATRIX_CYCLE_SPIRAL:
        case RGB_MATRIX_RAINBOW_BEACON:
            return false;
        default:
            return n == 1;
    }
}

static void effect_name(char *buf, uint8_t len) {
    const char *n = rgb_matrix_get_mode_name(rgb_matrix_get_mode());
    uint8_t     i = 0;
    for (; n[i] && i < len - 1; i++) buf[i] = n[i] == '_' ? ' ' : n[i];
    buf[i] = 0;
}

static const char *mode_name(void) {
    switch (wireless_mode()) {
        case WL_MODE_BT:
            return "BLUETOOTH";
        case WL_MODE_24G:
            return "2.4 GHZ";
        default:
            return "USB";
    }
}

// Текст значения; *gauge = 0..255 для шкалы или -1
static void item_value(uint8_t id, char *buf, uint8_t len, int16_t *gauge) {
    *gauge = -1;
    buf[0] = 0;
    switch (id) {
        case LIGHT_EN:
            strcpy(buf, rgb_matrix_is_enabled() ? "ON" : "OFF");
            break;
        case LIGHT_FX:
            effect_name(buf, 14);
            break;
        case LIGHT_COLOR: {
            int8_t c = color_current();
            strcpy(buf, color_name(c));
            break;
        }
        case LIGHT_COLOR2:
            strcpy(buf, color_name(type84_color2_index()));
            break;
        case LIGHT_FNCOLOR:
            strcpy(buf, color_name(type84_fn_color_index()));
            break;
        case LIGHT_VAL:
            *gauge = (int16_t)rgb_matrix_get_val() * 255 / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            snprintf(buf, len, "%3u%%", rgb_matrix_get_val() * 100 / RGB_MATRIX_MAXIMUM_BRIGHTNESS);
            break;
        case LIGHT_HUE:
            *gauge = rgb_matrix_get_hue();
            snprintf(buf, len, "%03u", rgb_matrix_get_hue());
            break;
        case LIGHT_SAT:
            *gauge = rgb_matrix_get_sat();
            snprintf(buf, len, "%03u", rgb_matrix_get_sat());
            break;
        case LIGHT_SPD:
            *gauge = rgb_matrix_get_speed();
            snprintf(buf, len, "%03u", rgb_matrix_get_speed());
            break;
        case ST_MODE:
            strcpy(buf, strip_mode_name(strip_mode()));
            break;
        case ST_COLOR:
            strcpy(buf, color_name(strip_color_index()));
            break;
        case ST_CAPS:
            strcpy(buf, strip_caps_alert() ? "ON" : "OFF");
            break;
        case WL_MODE:
            strcpy(buf, mode_name());
            break;
        case WL_SLOT:
            snprintf(buf, len, "%u", wireless_bt_slot());
            break;
        case PW_SLEEP: {
            uint8_t m = wireless_sleep_minutes();
            if (m) {
                snprintf(buf, len, "%u MIN", m);
            } else {
                strcpy(buf, "OFF");
            }
            break;
        }
        case PW_BATT:
            *gauge = (int16_t)battery_get_percent() * 255 / 100;
            snprintf(buf, len, "%u%%", battery_get_percent());
            break;
        case PW_VOLT: {
            uint16_t mv = battery_millivolts();
            snprintf(buf, len, "%u.%02u V", mv / 1000, (mv % 1000) / 10);
            break;
        }
        case PW_CHG:
            strcpy(buf, battery_charging() ? "CHARGING" : battery_on_usb_power() ? "FULL / USB" : "ON BATTERY");
            break;
        case DS_ACCENT:
            strcpy(buf, accents[accent_idx].name);
            break;
        case DS_TIMEOUT:
            strcpy(buf, screen_timeouts[scr_to_idx].name);
            break;
        case SY_NKRO:
            strcpy(buf, keymap_config.nkro ? "ON" : "OFF");
            break;
        case SY_GUI:
            strcpy(buf, keymap_config.no_gui ? "ON" : "OFF");
            break;
        case AB_FW:
            strcpy(buf, "QMK SN32");
            break;
        case AB_BUILD:
            strncpy(buf, __DATE__, len - 1);
            buf[len - 1] = 0;
            break;
        case AB_MCU:
            strcpy(buf, "SN32F290");
            break;
        case AB_UPTIME: {
            uint32_t s = timer_read32() / 1000;
            snprintf(buf, len, "%02lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
            break;
        }
    }
}

// Готовые цвета подсветки — общая таблица type84_colors (type84_pro.c)
#    define colors type84_colors
#    define ncolors type84_colors_count

static int8_t color_find(uint8_t h, uint8_t sat) {
    return type84_color_find(h, sat);
}

static int8_t color_current(void) {
    return color_find(rgb_matrix_get_hue(), rgb_matrix_get_sat());
}

static const char *color_name(int8_t i) {
    return i < 0 ? "CUSTOM" : colors[i].name;
}

static void color2_step(int8_t dir) {
    type84_color2_set_index((type84_color2_index() + ncolors + dir) % ncolors);
}

static void fn_color_step(int8_t dir) {
    type84_fn_color_set_index((type84_fn_color_index() + ncolors + dir) % ncolors);
}

static void color_step(int8_t dir) {
    int8_t  i = color_current();
    uint8_t n = ncolors;
    i         = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + n + dir) % n;
    rgb_matrix_sethsv(colors[i].h, colors[i].s, rgb_matrix_get_val());
}

static void toggle_keymap_flag(bool nkro) {
    clear_keyboard();
    if (nkro) {
        keymap_config.nkro = !keymap_config.nkro;
    } else {
        keymap_config.no_gui = !keymap_config.no_gui;
    }
    eeconfig_update_keymap(&keymap_config);
}

// Изменить значение: dir = -1 / +1
static void item_adjust(uint8_t id, int8_t dir) {
    switch (id) {
        case LIGHT_EN:
            rgb_matrix_toggle();
            break;
        case LIGHT_FX:
            if (dir > 0) {
                rgb_matrix_step();
            } else {
                rgb_matrix_step_reverse();
            }
            break;
        case LIGHT_COLOR:
            color_step(dir);
            break;
        case LIGHT_COLOR2:
            color2_step(dir);
            break;
        case LIGHT_FNCOLOR:
            fn_color_step(dir);
            break;
        case LIGHT_VAL:
            if (dir > 0) {
                rgb_matrix_increase_val();
            } else {
                rgb_matrix_decrease_val();
            }
            break;
        case LIGHT_HUE:
            if (dir > 0) {
                rgb_matrix_increase_hue();
            } else {
                rgb_matrix_decrease_hue();
            }
            break;
        case LIGHT_SAT:
            if (dir > 0) {
                rgb_matrix_increase_sat();
            } else {
                rgb_matrix_decrease_sat();
            }
            break;
        case LIGHT_SPD:
            if (dir > 0) {
                rgb_matrix_increase_speed();
            } else {
                rgb_matrix_decrease_speed();
            }
            break;
        case ST_MODE:
            strip_set_mode((strip_mode() + STRIP_MODES + dir) % STRIP_MODES);
            break;
        case ST_COLOR:
            strip_set_color_index((strip_color_index() + ncolors + dir) % ncolors);
            break;
        case ST_CAPS:
            strip_set_caps_alert(!strip_caps_alert());
            break;
        case WL_SLOT: {
            int8_t s = wireless_bt_slot() + dir;
            if (s < 1) s = 5;
            if (s > 5) s = 1;
            wireless_select_slot(s);
            break;
        }
        case PW_SLEEP: {
            uint8_t n = wireless_sleep_option_count();
            wireless_sleep_set_index((wireless_sleep_index() + n + dir) % n);
            break;
        }
        case DS_ACCENT:
            accent_idx = (accent_idx + ARRAY_SIZE(accents) + dir) % ARRAY_SIZE(accents);
            settings_save();
            full_redraw = true; // акцент меняет весь экран
            break;
        case DS_TIMEOUT:
            scr_to_idx = (scr_to_idx + ARRAY_SIZE(screen_timeouts) + dir) % ARRAY_SIZE(screen_timeouts);
            settings_save();
            break;
        case SY_NKRO:
            toggle_keymap_flag(true);
            break;
        case SY_GUI:
            toggle_keymap_flag(false);
            break;
    }
}

/* --------------------------------------------------------------------------
 * Всплывающая надпись (поверх главного экрана или меню)
 * ------------------------------------------------------------------------ */

static char toast_msg[20];

static void draw_toast(void) {
    int w = qp_textwidth(f20, toast_msg) + 32;
    if (w > W - 20) w = W - 20;
    int x0 = (W - w) / 2, y0 = 60, x1 = x0 + w, y1 = 100;
    rect(x0, y0, x1, y1, C_BG, true);
    rect(x0, y0, x1, y1, C_LINE, false);
    brackets(x0 - 3, y0 - 3, x1 + 3, y1 + 3, C_ACC, 6);
    text((W - qp_textwidth(f20, toast_msg)) / 2, y0 + 9, f20, toast_msg, C_ACC, C_BG);
}

void ui_toast(const char *msg) {
    strncpy(toast_msg, msg, sizeof(toast_msg) - 1);
    toast_msg[sizeof(toast_msg) - 1] = 0;
    toast_on = true;
    toast_at = timer_read();
    if (lcd) {
        draw_toast();
        qp_flush(lcd);
    }
}

/* --------------------------------------------------------------------------
 * Главный экран
 * ------------------------------------------------------------------------ */

static struct {
    uint8_t bat;
    bool    chg;
    uint8_t layer;
    uint8_t wpm;
    uint16_t mv;
    uint8_t tiles;
    char    status[24];
    col_t   status_c;
} hud;

static void battery_icon(int x, int y, uint8_t pct, bool chg) {
    rect(x, y, x + 22, y + 10, C_TXT, false);
    rect(x + 23, y + 3, x + 24, y + 7, C_TXT, true);
    uint8_t n = (pct + 10) / 20;
    for (uint8_t i = 0; i < 5; i++) {
        col_t c = C_LINE;
        if (i < n) c = chg ? C_OK : (pct > 15 ? C_ACC : C_WARN);
        rect(x + 2 + i * 4, y + 2, x + 4 + i * 4, y + 8, c, true);
    }
}

static void link_info(char *label, bool *ok, char *status, col_t *sc) {
    wl_link_t l = wireless_link();
    switch (wireless_mode()) {
        case WL_MODE_BT:
            snprintf(label, 6, "BT%u", wireless_bt_slot());
            break;
        case WL_MODE_24G:
            strcpy(label, "2.4G");
            break;
        default:
            strcpy(label, "USB");
            *ok = usb_connected_state();
            strcpy(status, *ok ? "USB HOST" : "USB --");
            *sc = *ok ? C_OK : C_DIM;
            return;
    }
    *ok = l == WL_LINK_CONNECTED;
    if (l == WL_LINK_CONNECTED) {
        snprintf(status, 24, "%s LINKED", label);
        *sc = C_OK;
    } else if (l == WL_LINK_PAIRING) {
        snprintf(status, 24, "%s PAIRING", label);
        *sc = C_ACC;
    } else {
        snprintf(status, 24, "%s SEARCH", label);
        *sc = C_WARN;
    }
}

static void main_draw(bool all) {
    char  link[6], status[24];
    bool  link_ok = false;
    col_t sc      = C_DIM;
    link_info(link, &link_ok, status, &sc);
    uint8_t  bat   = battery_get_percent();
    bool     chg   = battery_charging();
    uint8_t  layer = get_highest_layer(layer_state | default_layer_state);
    uint8_t  wpm   = get_current_wpm();
    uint16_t mv    = battery_millivolts();
    uint8_t  tiles = (host_keyboard_led_state().caps_lock ? 1 : 0) | (keymap_config.nkro ? 2 : 0) | (rgb_matrix_is_enabled() ? 4 : 0) | (keymap_config.no_gui ? 8 : 0);

    if (all) {
        rect(0, 0, W - 1, H - 1, C_BG, true);
        text(8, 3, f14, "IO//84", C_ACC, C_BG);
        text(62, 3, f14, "QMK", C_DIM, C_BG);
        hline(0, W - 1, HEADER_LINE_Y, C_LINE);
        hline(0, W - 1, FOOTER_LINE_Y, C_LINE);
        brackets(6, 28, 206, 142, C_DIM, 6);
        text(16, 32, f14, "LAYER", C_DIM, C_BG);
        text(16, 104, f14, "WPM", C_DIM, C_BG);
        text(110, 104, f14, "BAT", C_DIM, C_BG);
        text_r(W - 8, 154, f14, "FN+ENT  MENU", C_DIM, C_BG);
    }

    // верх справа: заряд (канал связи — в строке состояния внизу)
    if (all || bat != hud.bat || chg != hud.chg) {
        rect(200, 0, W - 1, HEADER_LINE_Y - 1, C_BG, true);
        char p[6];
        snprintf(p, sizeof(p), "%u%%", bat);
        text_r(266, 3, f14, p, chg ? C_OK : C_TXT, C_BG);
        battery_icon(272, 5, bat, chg);
        hud.bat = bat;
        hud.chg = chg;
    }

    // слой крупно
    if (all || layer != hud.layer) {
        rect(14, 46, 200, 98, C_BG, true);
        char        b[6];
        const char *n = layer == 0 ? "BASE" : layer == 1 ? "FN" : b;
        if (layer > 1) snprintf(b, sizeof(b), "L%u", layer);
        text(14, 46, f44, n, layer ? C_ACC : C_TXT, C_BG);
        hud.layer = layer;
    }

    // WPM и напряжение
    if (all || wpm != hud.wpm) {
        char b[6];
        snprintf(b, sizeof(b), "%03u", wpm);
        text(52, 100, f20, b, C_ACC, C_BG);
        hud.wpm = wpm;
    }
    if (all || mv / 10 != hud.mv / 10) {
        char b[8];
        snprintf(b, sizeof(b), "%u.%02uV", mv / 1000, (mv % 1000) / 10);
        rect(146, 100, 204, 122, C_BG, true);
        text(146, 100, f20, b, C_TXT, C_BG);
        hud.mv = mv;
    }

    // плашки состояний
    if (all || tiles != hud.tiles) {
        static const char *names[] = {"CAPS", "NKRO", "LIGHT", "WIN LK"};
        for (uint8_t i = 0; i < 4; i++) {
            bool on = tiles & (1 << i);
            int  y  = 32 + i * 28;
            rect(214, y, 312, y + 22, on ? C_PANEL : C_BG, true);
            rect(214, y, 312, y + 22, C_LINE, false);
            rect(218, y + 8, 223, y + 13, on ? C_ACC : C_LINE, true);
            text(230, y + 3, f14, names[i], on ? C_TXT : C_DIM, on ? C_PANEL : C_BG);
        }
        hud.tiles = tiles;
    }

    // низ: состояние связи
    if (all || strcmp(status, hud.status) || sc.h != hud.status_c.h || sc.v != hud.status_c.v) {
        rect(0, FOOTER_LINE_Y + 1, 200, H - 1, C_BG, true);
        rect(8, 158, 13, 163, sc, true);
        text(20, 154, f14, status, C_TXT, C_BG);
        strcpy(hud.status, status);
        hud.status_c = sc;
    }
}

/* --------------------------------------------------------------------------
 * Меню
 * ------------------------------------------------------------------------ */

static bool page_has_adjustable(void) {
    for (uint8_t i = 0; i < pages[page].count; i++) {
        uint8_t t = pages[page].items[i].type;
        if (t == IT_TOGGLE || t == IT_ENUM || t == IT_RANGE) return true;
    }
    return false;
}

static void menu_header(void) {
    rect(0, 0, W - 1, HEADER_LINE_Y - 1, C_BG, true);
    if (page == P_ROOT) {
        text(8, 3, f14, "MENU", C_ACC, C_BG);
    } else {
        text(8, 3, f14, "MENU", C_DIM, C_BG);
        char t[24];
        snprintf(t, sizeof(t), "/ %s", pages[page].title);
        text(48, 3, f14, t, C_ACC, C_BG);
    }
    char n[8];
    snprintf(n, sizeof(n), "%u/%u", sel[page] + 1, pages[page].count);
    text_r(W - 8, 3, f14, n, C_DIM, C_BG);
}

static void menu_footer(void) {
    rect(0, FOOTER_LINE_Y + 1, W - 1, H - 1, C_BG, true);
    text(8, 154, f14, page_has_adjustable() ? "ARROWS NAV/ADJ  ENT OK  CTL BACK" : "ARROWS NAV  ENT OK  CTL BACK", C_DIM, C_BG);
}

static void menu_row(uint8_t i) {
    if (i < scroll || i >= scroll + ROWS_VISIBLE) return;
    const item_t *it  = &pages[page].items[i];
    bool          on  = i == sel[page];
    int           y   = ROW_Y0 + (i - scroll) * ROW_H;
    col_t         bg  = on ? C_PANEL : C_BG;
    col_t         fg  = on ? C_ACC : C_TXT;
    rect(4, y, W - 6, y + ROW_H - 3, bg, true);
    if (on) {
        rect(4, y, W - 6, y + ROW_H - 3, C_ACC, false);
        rect(4, y, 7, y + ROW_H - 3, C_ACC, true);
    }

    if (page == P_ROOT) {
        char n[4];
        snprintf(n, sizeof(n), "%02u", i + 1);
        text(16, y + 4, f14, n, on ? C_ACC : C_DIM, bg);
        text(44, y, f20, it->name, fg, bg);
        text_r(W - 14, y, f20, ">", on ? C_ACC : C_LINE, bg);
        return;
    }

    text(16, y, f20, it->name, fg, bg);
    if (it->type == IT_SUB) {
        text_r(W - 14, y, f20, ">", on ? C_ACC : C_LINE, bg);
        return;
    }
    if (it->type == IT_ACTION) {
        text_r(W - 14, y, f20, on ? "[ENT]" : "", on ? C_ACC : C_DIM, bg);
        return;
    }

    char    v[20];
    int16_t gauge;
    item_value(it->id, v, sizeof(v), &gauge);
    if (it->id == LIGHT_COLOR || it->id == LIGHT_COLOR2 || it->id == LIGHT_FNCOLOR || it->id == ST_COLOR) {
        // образец цвета; если текущий эффект этот цвет не использует — тусклый
        bool  used = it->id == LIGHT_FNCOLOR || it->id == ST_COLOR || effect_uses_color(it->id == LIGHT_COLOR ? 1 : 2);
        hsv_t c    = it->id == LIGHT_COLOR ? rgb_matrix_get_hsv() : it->id == LIGHT_COLOR2 ? type84_color2() : it->id == LIGHT_FNCOLOR ? type84_fn_color() : (hsv_t){type84_colors[strip_color_index()].h, type84_colors[strip_color_index()].s, 255};
        col_t sw   = {c.h, c.s, used ? 255 : 70};
        if (type84_is_black(c)) sw = C_BG; // чёрный — пустой квадрат в рамке
        rect(140, y + 4, 156, y + 17, sw, true);
        rect(140, y + 4, 156, y + 17, C_LINE, false);
        if (!used) {
            text_r(W - 14, y, f20, v, C_LINE, bg);
            return;
        }
    }
    if (gauge >= 0) {
        uint8_t segs = (gauge * 10 + 127) / 255;
        for (uint8_t k = 0; k < 10; k++) {
            int x = 150 + k * 9;
            rect(x, y + 6, x + 5, y + 15, k < segs ? (on ? C_ACC : C_TXT) : C_LINE, true);
        }
        text_r(W - 14, y, f20, v, on ? C_ACC : C_DIM, bg);
    } else if (on && it->type != IT_INFO) {
        char vv[24];
        snprintf(vv, sizeof(vv), "< %s >", v);
        text_r(W - 14, y, f20, vv, C_ACC, bg);
    } else {
        text_r(W - 14, y, f20, v, on ? C_ACC : C_DIM, bg);
    }
}

static void menu_scrollbar(void) {
    uint8_t n = pages[page].count;
    rect(W - 3, ROW_Y0, W - 1, ROW_Y0 + ROWS_VISIBLE * ROW_H, C_BG, true);
    if (n <= ROWS_VISIBLE) return;
    int h  = ROWS_VISIBLE * ROW_H * ROWS_VISIBLE / n;
    int y0 = ROW_Y0 + (ROWS_VISIBLE * ROW_H - h) * scroll / (n - ROWS_VISIBLE);
    rect(W - 3, y0, W - 2, y0 + h, C_DIM, true);
}

static void menu_list(void) {
    rect(0, HEADER_LINE_Y + 1, W - 1, FOOTER_LINE_Y - 1, C_BG, true);
    for (uint8_t i = scroll; i < pages[page].count && i < scroll + ROWS_VISIBLE; i++) menu_row(i);
    menu_scrollbar();
}

static void menu_draw_all(void) {
    rect(0, 0, W - 1, H - 1, C_BG, true);
    hline(0, W - 1, HEADER_LINE_Y, C_LINE);
    hline(0, W - 1, FOOTER_LINE_Y, C_LINE);
    menu_header();
    menu_list();
    menu_footer();
}

static void menu_fix_scroll(void) {
    uint8_t s = sel[page];
    if (s < scroll) scroll = s;
    if (s >= scroll + ROWS_VISIBLE) scroll = s - ROWS_VISIBLE + 1;
}

/* --------------------------------------------------------------------------
 * Подтверждение
 * ------------------------------------------------------------------------ */

static void confirm_draw(void) {
    // затемнить фон «через строку» — экран остаётся виден как контекст
    for (int y = 0; y < H; y += 2) hline(0, W - 1, y, C_BG);
    rect(40, 40, 280, 130, C_BG, true);
    rect(40, 40, 280, 130, C_WARN, false);
    brackets(36, 36, 284, 134, C_WARN, 8);
    text(56, 50, f20, confirm_id == SY_BOOT ? "ENTER BOOTLOADER?" : "FACTORY RESET?", C_WARN, C_BG);
    text(56, 80, f14, confirm_id == SY_BOOT ? "KEYBOARD WILL RESET" : "ALL SETTINGS ERASED", C_DIM, C_BG);
    text(56, 106, f14, "ENT CONFIRM", C_TXT, C_BG);
    text(180, 106, f14, "CTL CANCEL", C_DIM, C_BG);
}

/* --------------------------------------------------------------------------
 * Ввод
 * ------------------------------------------------------------------------ */

static void open_menu(void) {
    screen      = SCR_MENU;
    page        = P_ROOT;
    scroll      = 0;
    full_redraw = true;
}

static void close_menu(void) {
    screen      = SCR_MAIN;
    full_redraw = true;
}

static void menu_move(int8_t d) {
    uint8_t n   = pages[page].count;
    uint8_t old = sel[page];
    sel[page]   = (sel[page] + n + d) % n;
    uint8_t os  = scroll;
    menu_fix_scroll();
    if (scroll != os) {
        menu_list();
    } else {
        menu_row(old);
        menu_row(sel[page]);
    }
    menu_header();
}

static void menu_enter(void) {
    const item_t *it = &pages[page].items[sel[page]];
    switch (it->type) {
        case IT_SUB:
            page   = it->id;
            scroll = 0;
            menu_fix_scroll();
            full_redraw = true;
            break;
        case IT_TOGGLE:
        case IT_ENUM:
            item_adjust(it->id, +1);
            if (it->id == LIGHT_FX) {
                menu_list();
            } else {
                menu_row(sel[page]);
            }
            break;
        case IT_ACTION:
            if (it->id == WL_PAIR) {
                if (wireless_pair()) {
                    close_menu();
                    ui_toast("PAIRING...");
                } else {
                    ui_toast("SWITCH TO BT/2.4G");
                    full_redraw = true;
                }
            } else {
                confirm_id = it->id;
                screen     = SCR_CONFIRM;
                confirm_draw();
            }
            break;
    }
}

static void menu_back(void) {
    if (page == P_ROOT) {
        close_menu();
        return;
    }
    // вернуться к пункту, из которого вошли
    for (uint8_t i = 0; i < pages[P_ROOT].count; i++) {
        if (items_root[i].id == page) sel[P_ROOT] = i;
    }
    page   = P_ROOT;
    scroll = 0;
    menu_fix_scroll();
    full_redraw = true;
}

static void confirm_accept(void) {
    if (confirm_id == SY_BOOT) {
        rect(0, 0, W - 1, H - 1, C_BG, true);
        text(16, 70, f20, "BOOTLOADER", C_WARN, C_BG);
        qp_flush(lcd);
        wait_ms(50);
        reset_keyboard();
    } else {
        eeconfig_init();
        soft_reset_keyboard();
    }
}

bool ui_menu_active(void) {
    return screen != SCR_MAIN;
}

bool ui_process(uint16_t keycode, keyrecord_t *record) {
    // крутилка в меню: поворот — навигация
    if (screen == SCR_MENU && IS_ENCODEREVENT(record->event)) {
        if (record->event.pressed) {
            last_ui_input = timer_read();
            menu_move(record->event.type == ENCODER_CW_EVENT ? 1 : -1);
        }
        return false;
    }
    // нажатие крутилки (матрица (5,3)/(5,4)) в меню — выбор
    if (screen != SCR_MAIN && record->event.type == KEY_EVENT && record->event.key.row == 5 && (record->event.key.col == 3 || record->event.key.col == 4)) {
        keycode = UI_OK;
    }

    if (keycode < UI_UP || keycode > UI_BACK) return true;
    if (!record->event.pressed) return false;
    last_ui_input = timer_read();
    if (screen_dark) return false; // первое нажатие только будит экран

    switch (screen) {
        case SCR_MAIN:
            switch (keycode) {
                case UI_OK:
                    open_menu();
                    break;
                case UI_UP:
                case UI_DOWN: {
                    if (keycode == UI_UP) {
                        rgb_matrix_increase_val();
                    } else {
                        rgb_matrix_decrease_val();
                    }
                    char b[16];
                    snprintf(b, sizeof(b), "BRIGHT %u%%", rgb_matrix_get_val() * 100 / RGB_MATRIX_MAXIMUM_BRIGHTNESS);
                    ui_toast(b);
                    break;
                }
                case UI_LEFT:
                case UI_RIGHT: {
                    if (keycode == UI_RIGHT) {
                        rgb_matrix_step();
                    } else {
                        rgb_matrix_step_reverse();
                    }
                    char b[16];
                    effect_name(b, sizeof(b));
                    ui_toast(b);
                    break;
                }
            }
            break;

        case SCR_MENU: {
            const item_t *it = &pages[page].items[sel[page]];
            switch (keycode) {
                case UI_UP:
                    menu_move(-1);
                    break;
                case UI_DOWN:
                    menu_move(1);
                    break;
                case UI_LEFT:
                case UI_RIGHT:
                    if (it->type == IT_TOGGLE || it->type == IT_ENUM || it->type == IT_RANGE) {
                        item_adjust(it->id, keycode == UI_RIGHT ? 1 : -1);
                        if (it->id == LIGHT_FX) {
                            menu_list(); // эффект меняет, какие цвета активны
                        } else if (!full_redraw) {
                            menu_row(sel[page]);
                        }
                    } else if (keycode == UI_RIGHT && it->type == IT_SUB) {
                        menu_enter();
                    } else if (keycode == UI_LEFT) {
                        menu_back();
                    }
                    break;
                case UI_OK:
                    menu_enter();
                    break;
                case UI_BACK:
                    menu_back();
                    break;
            }
            break;
        }

        case SCR_CONFIRM:
            if (keycode == UI_OK) {
                confirm_accept();
            } else if (keycode == UI_BACK) {
                screen      = SCR_MENU;
                full_redraw = true;
            }
            break;
    }
    if (lcd) qp_flush(lcd);
    return false;
}

/* --------------------------------------------------------------------------
 * Цикл
 * ------------------------------------------------------------------------ */

void ui_init(painter_device_t dev) {
    lcd = dev;
    f14 = qp_load_font_mem(font_tech14);
    f20 = qp_load_font_mem(font_tech20);
    f44 = qp_load_font_mem(font_raj44);
    settings_load();
    full_redraw = true;
    ui_task();
}

void ui_redraw(void) {
    full_redraw = true;
}

static void screen_timeout_task(void) {
    uint16_t to = screen_timeouts[scr_to_idx].sec;
    bool     idle = to && last_input_activity_elapsed() > (uint32_t)to * 1000;
    if (idle && !screen_dark) {
        screen_dark = true;
        type84_lcd_sleep(true);
    } else if (!idle && screen_dark) {
        screen_dark = false;
        type84_lcd_sleep(false);
    }
}

void ui_task(void) {
    if (!lcd) return;

    screen_timeout_task();
    if (screen_dark) return;

    // меню закрывается само
    if (screen != SCR_MAIN && timer_elapsed(last_ui_input) > MENU_TIMEOUT_MS) {
        close_menu();
    }

    bool dirty = full_redraw;

    if (toast_on && timer_elapsed(toast_at) > TOAST_MS) {
        toast_on    = false;
        full_redraw = true;
        dirty       = true;
    }

    switch (screen) {
        case SCR_MAIN:
            if (!toast_on || full_redraw) {
                main_draw(full_redraw);
                dirty = true; // main_draw сам решает, что перерисовать
            }
            break;
        case SCR_MENU:
            if (full_redraw) {
                menu_draw_all();
            } else {
                // живые значения (батарея, аптайм) — раз в секунду
                static uint16_t live;
                if ((page == P_POWER || page == P_ABOUT || page == P_WIRELESS) && timer_elapsed(live) > 1000) {
                    live = timer_read();
                    for (uint8_t i = scroll; i < pages[page].count && i < scroll + ROWS_VISIBLE; i++) {
                        if (pages[page].items[i].type == IT_INFO) menu_row(i);
                    }
                    dirty = true;
                }
            }
            break;
        case SCR_CONFIRM:
            if (full_redraw) {
                menu_draw_all();
                confirm_draw();
            }
            break;
    }
    if (toast_on && full_redraw) draw_toast();
    full_redraw = false;
    if (dirty) qp_flush(lcd);
}

#endif // QUANTUM_PAINTER_ENABLE
