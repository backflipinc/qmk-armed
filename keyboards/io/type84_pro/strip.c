// Светодиодная полоса под дисплеем: 9 RGB-светодиодов на свободных позициях LED-матрицы
// (найдены пробой keymaps/ledprobe). В RGB Matrix не входят — эффекты клавиш их не трогают;
// рисуем сами, прямо в выводимый буфер (ledscan_set_direct), ~50 раз в секунду.
//
// Режимы: WPM (чёрный -> цвет по скорости печати, полный цвет на 200 WPM), WPM BAR, BATTERY, LINK, SCANNER, OFF.
// CAPS ALERT: при Caps Lock вся полоса горит цветом полосы поверх режима.

#include "quantum.h"
#include "lib/lib8tion/lib8tion.h"
#include "ledscan.h"
#include "strip.h"
#include "settings.h"
#include "type84_pro.h"
#include "wireless.h"
#include "battery.h"
#include "battery_cw2017.h"

// Слева направо (строка, колонка LED-матрицы)
static const uint8_t strip_pos[STRIP_LEDS][2] = {
    {3, 13}, {3, 14}, {3, 15}, {4, 13}, {4, 15}, {5, 3}, {5, 4}, {5, 5}, {5, 7},
};

static const char *const mode_names[STRIP_MODES] = {"WPM", "WPM BAR", "BATTERY", "LINK", "SCANNER", "OFF"};

#ifndef STRIP_WPM_FULL
#    define STRIP_WPM_FULL 200 // WPM, при которой режим WPM горит в полную яркость
#endif

static uint8_t mode, color_idx;
static bool    caps_alert = true;

/* --------------------------------------------------------------------------
 * Настройки
 * ------------------------------------------------------------------------ */

static void strip_save(void) {
    kb_settings_t *s    = kb_settings();
    s->strip_mode       = mode;
    s->strip_color      = color_idx;
    s->strip_caps_alert = caps_alert;
    kb_settings_save();
}

void strip_init(void) {
    kb_settings_t *s = kb_settings();
    mode             = s->strip_mode;
    color_idx        = s->strip_color;
    caps_alert       = s->strip_caps_alert;
    if (mode >= STRIP_MODES) mode = 0;
    if (color_idx >= type84_colors_count) color_idx = 0; // 0 = RED
}

uint8_t strip_mode(void) {
    return mode;
}
const char *strip_mode_name(uint8_t m) {
    return m < STRIP_MODES ? mode_names[m] : "?";
}
void strip_set_mode(uint8_t m) {
    mode = m % STRIP_MODES;
    strip_save();
}
uint8_t strip_color_index(void) {
    return color_idx;
}
void strip_set_color_index(uint8_t i) {
    color_idx = i % type84_colors_count;
    strip_save();
}
bool strip_caps_alert(void) {
    return caps_alert;
}
void strip_set_caps_alert(bool on) {
    caps_alert = on;
    strip_save();
}

/* --------------------------------------------------------------------------
 * Рисование
 * ------------------------------------------------------------------------ */

static uint8_t level(void) {
    uint8_t v = rgb_matrix_get_val();
    return v < 60 ? 60 : v; // полоса видна даже при тусклой подсветке клавиш
}

static rgb_t hsv_rgb(uint8_t h, uint8_t s, uint8_t v) {
    hsv_t c = {h, s, v};
    return hsv_to_rgb(c);
}

static rgb_t strip_color(uint8_t v) {
    const type84_color_t *c = &type84_colors[color_idx];
    if (c->s == 1 && c->h == 0) return hsv_rgb(0, 0, 0); // BLACK
    return hsv_rgb(c->h, c->s, v);
}

// Гасить готовый цвет масштабом каналов, а не яркостью HSV: hsv_to_rgb() пропускает V через
// кривую CIE1931 и считает каналы целыми — на малой яркости слабый канал обнуляется и цвет
// скачком меняется (оранжевый -> красный). scale8_video не обнуляет ненулевой канал.
static rgb_t dim(rgb_t c, uint8_t t) {
    rgb_t d = {scale8_video(c.r, t), scale8_video(c.g, t), scale8_video(c.b, t)};
    return d;
}

static void put(uint8_t i, rgb_t c) {
    ledscan_set_direct(strip_pos[i][0], strip_pos[i][1], c.r, c.g, c.b);
}

static void fill(rgb_t c) {
    for (uint8_t i = 0; i < STRIP_LEDS; i++) put(i, c);
}

// Шкала: n256 = заполнение 0..STRIP_LEDS*256, последний светодиод — частично
static void bar(uint16_t n256, rgb_t on) {
    for (uint8_t i = 0; i < STRIP_LEDS; i++) {
        uint16_t lo = i * 256;
        uint8_t  t  = n256 >= lo + 256 ? 255 : n256 > lo ? n256 - lo : 0;
        put(i, dim(on, t));
    }
}

static void draw_wpm(uint8_t v) {
    uint8_t wpm = get_current_wpm();
    uint8_t t   = wpm >= STRIP_WPM_FULL ? 255 : (uint16_t)wpm * 255 / STRIP_WPM_FULL;
    fill(dim(strip_color(v), t));
}

static void draw_wpm_bar(uint8_t v) {
    uint8_t wpm = get_current_wpm();
    bar((uint16_t)(wpm > 108 ? 108 : wpm) * STRIP_LEDS * 256 / 108, strip_color(v));
}

static void draw_battery(uint8_t v) {
    uint8_t pct = battery_get_percent();
    uint8_t hue = pct > 50 ? 85 : pct > 20 ? 43 : 0; // зелёный / жёлтый / красный
    if (battery_charging()) {
        // бегущая заливка от текущего уровня до конца
        uint16_t base = (uint16_t)pct * STRIP_LEDS * 256 / 100;
        uint16_t run  = base + (uint16_t)((timer_read() / 4) % 256) * (STRIP_LEDS * 256 - base) / 256;
        bar(run, hsv_rgb(hue, 255, v));
    } else {
        bar((uint16_t)pct * STRIP_LEDS * 256 / 100, hsv_rgb(hue, 255, v));
    }
}

static void draw_link(uint8_t v) {
    uint8_t ph = timer_read() / 4; // ~1 с на оборот
    if (wireless_mode() == WL_MODE_USB) {
        fill(hsv_rgb(128, 255, scale8(v, 160))); // USB — ровный циан
        return;
    }
    switch (wireless_link()) {
        case WL_LINK_CONNECTED:
            fill(hsv_rgb(85, 255, scale8(v, 160)));
            break;
        case WL_LINK_PAIRING:
            fill(hsv_rgb(28, 255, (ph & 0x80) ? v : 0)); // мигает
            break;
        default:
            fill(hsv_rgb(0, 255, scale8(v, sin8(ph)))); // пульсирует
            break;
    }
}

static void draw_scanner(uint8_t v) {
    // огонёк с хвостом туда-обратно, полный проход ~1.5 с
    uint16_t ph  = (timer_read() / 3) % 512;
    uint16_t pos = ph < 256 ? ph : 511 - ph; // 0..255
    int16_t  x   = pos * (STRIP_LEDS - 1) * 16 / 255;
    for (uint8_t i = 0; i < STRIP_LEDS; i++) {
        int16_t d = abs((int16_t)i * 16 - x);
        uint8_t t = d >= 24 ? 0 : 255 - d * 255 / 24;
        put(i, dim(strip_color(v), t));
    }
}

void strip_task(void) {
    static uint16_t last;
    if (timer_elapsed(last) < 20) return;
    last = timer_read();

    rgb_t black = {0, 0, 0};
    if (rgb_matrix_get_suspend_state()) {
        fill(black);
        return;
    }
    uint8_t v = level();
    if (caps_alert && host_keyboard_led_state().caps_lock) {
        fill(strip_color(v));
        return;
    }
    switch (mode) {
        case STRIP_WPM:
            draw_wpm(v);
            break;
        case STRIP_WPM_BAR:
            draw_wpm_bar(v);
            break;
        case STRIP_BATTERY:
            draw_battery(v);
            break;
        case STRIP_LINK:
            draw_link(v);
            break;
        case STRIP_SCANNER:
            draw_scanner(v);
            break;
        default:
            fill(black);
            break;
    }
}
