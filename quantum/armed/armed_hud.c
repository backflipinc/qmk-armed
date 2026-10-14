// Copyright 2026 backflipinc
// SPDX-License-Identifier: GPL-2.0-or-later

// HUD: the main screen as a list of widgets, format in docs/armed.md.
// vial-gui-armed's hud_render.py must draw the same thing.

#include "armed.h"
#include "quantum.h"
#include "via.h"

#ifdef QUANTUM_PAINTER_ENABLE

#    include "qp.h"
#    ifdef BATTERY_ENABLE
#        include "battery.h"
#    endif

__attribute__((weak)) void armed_hud_defaults_kb(void) {
    armed_hud_add(ARMED_HUD_W_TEXT, ARMED_HUD_S_NONE, 0, ARMED_HUD_C_DIM, ARMED_HUD_C_BG, 0, 8, 8, 120, 0, 0, "LAYER");
    armed_hud_add(ARMED_HUD_W_TEXT, ARMED_HUD_S_LAYER, 0, ARMED_HUD_C_AUTO, ARMED_HUD_C_BG, 0, 8, 28, 120, 0, 0, NULL);
}

__attribute__((weak)) bool armed_hud_source_kb(uint8_t source, armed_hud_value_t *value) {
    return false;
}

__attribute__((weak)) void armed_hud_color_kb(uint8_t role, uint8_t *hsv) {
    static const uint8_t palette[ARMED_HUD_C_COUNT][3] = {{0, 0, 0}, {0, 0, 16}, {0, 0, 40}, {0, 0, 90}, {0, 0, 220}, {28, 255, 255}, {85, 200, 200}, {0, 220, 230}};
    memcpy(hsv, palette[role], 3);
}

__attribute__((weak)) bool armed_hud_load_kb(uint8_t *data, uint16_t size) {
    return false;
}

__attribute__((weak)) bool armed_hud_save_kb(const uint8_t *data, uint16_t size) {
    return false;
}

static painter_device_t        lcd;
static const armed_hud_font_t *fonts;
static uint8_t                 font_count;
static uint16_t                scr_w, scr_h;

static armed_hud_layout_t layout;
static uint16_t           strings_used;
static uint32_t           sig[ARMED_HUD_WIDGETS];
static bool               redraw_all, editing;
static uint16_t           edit_at;

enum { H_NONE, H_SET, H_TIMED };

static struct {
    int32_t  num;
    uint32_t until;
    uint8_t  dec, state;
} host[ARMED_HUD_HOST_VALUES];

static struct {
    char     text[ARMED_HUD_TEXT_LEN];
    uint32_t until;
    uint8_t  state;
} host_txt[ARMED_HUD_HOST_TEXTS];

static char    staging[ARMED_HUD_TEXT_LEN];
static uint8_t staging_slot = 0xFF, staging_len;

static struct {
    uint8_t  widget, n, head;
    uint16_t at;
    uint8_t  v[ARMED_HUD_GRAPH_POINTS];
} graph[ARMED_HUD_GRAPHS];
static uint8_t graphs;

static uint8_t avail[ARMED_HUD_S_COUNT + ARMED_HUD_HOST_VALUES + ARMED_HUD_HOST_TEXTS];
static uint8_t avail_count;

static const char *const source_names[ARMED_HUD_S_COUNT] = {
    "NONE", "LAYER", "WPM", "BATTERY", "VOLTAGE", "CHARGING", "LINK", "MODE", "TIME", "DATE", "CAPS LOCK", "NUM LOCK", "SCROLL LOCK", "NKRO", "LIGHTING", "WIN LOCK", "SNAPTAP", "EFFECT", "UPTIME", "BRIGHTNESS",
};

static const uint8_t source_kinds[ARMED_HUD_S_COUNT] = {
    ARMED_HUD_K_TEXT, ARMED_HUD_K_TEXT, ARMED_HUD_K_NUMBER, ARMED_HUD_K_NUMBER, ARMED_HUD_K_NUMBER, ARMED_HUD_K_FLAG, ARMED_HUD_K_TEXT, ARMED_HUD_K_TEXT, ARMED_HUD_K_TEXT, ARMED_HUD_K_TEXT, ARMED_HUD_K_FLAG, ARMED_HUD_K_FLAG, ARMED_HUD_K_FLAG, ARMED_HUD_K_FLAG, ARMED_HUD_K_FLAG, ARMED_HUD_K_FLAG, ARMED_HUD_K_FLAG, ARMED_HUD_K_TEXT, ARMED_HUD_K_TEXT, ARMED_HUD_K_NUMBER,
};

/* --------------------------------------------------------------------------
 * Sources
 * ------------------------------------------------------------------------ */

static void copy(char *dst, const char *src, uint8_t len) {
    uint8_t i = 0;
    for (; src[i] && i < len - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static bool is_host(uint8_t s) {
    return s >= ARMED_HUD_S_HOST && s < ARMED_HUD_S_HOST + ARMED_HUD_HOST_VALUES;
}

static bool is_host_text(uint8_t s) {
    return s >= ARMED_HUD_S_HOST_TXT && s < ARMED_HUD_S_HOST_TXT + ARMED_HUD_HOST_TEXTS;
}

static void flag(armed_hud_value_t *v, bool on) {
    v->num  = on;
    v->hint = on ? ARMED_HUD_C_ACCENT : ARMED_HUD_C_LINE;
    strcpy(v->text, on ? "ON" : "OFF");
}

static bool host_alive(uint8_t *state, uint32_t until) {
    if (*state == H_TIMED && timer_expired32(timer_read32(), until)) *state = H_NONE;
    return *state != H_NONE;
}

static bool core_source(uint8_t s, armed_hud_value_t *v) {
    if (is_host(s)) {
        uint8_t i = s - ARMED_HUD_S_HOST;
        v->valid  = host_alive(&host[i].state, host[i].until);
        v->num    = host[i].num;
        v->dec    = host[i].dec;
        if (!v->valid) v->hint = ARMED_HUD_C_LINE;
        return true;
    }
    if (is_host_text(s)) {
        uint8_t i = s - ARMED_HUD_S_HOST_TXT;
        v->valid  = host_alive(&host_txt[i].state, host_txt[i].until);
        if (!v->valid) {
            v->hint = ARMED_HUD_C_LINE;
            strcpy(v->text, "--");
            return true;
        }
        uint8_t n = 0;
        for (; n < sizeof(v->text) - 1 && host_txt[i].text[n] && host_txt[i].text[n] != '\n'; n++) v->text[n] = host_txt[i].text[n];
        v->text[n] = 0;
        return true;
    }
    switch (s) {
        case ARMED_HUD_S_LAYER: {
            uint8_t l = get_highest_layer(layer_state | default_layer_state);
            v->num    = l;
            v->hint   = l ? ARMED_HUD_C_ACCENT : ARMED_HUD_C_TEXT;
            if (l) {
                snprintf(v->text, sizeof(v->text), "L%u", l);
            } else {
                strcpy(v->text, "BASE");
            }
            return true;
        }
#    ifdef WPM_ENABLE
        case ARMED_HUD_S_WPM:
            v->num = get_current_wpm();
            return true;
#    endif
#    ifdef BATTERY_ENABLE
        case ARMED_HUD_S_BATTERY:
            v->num = battery_get_percent();
            if (v->num <= 15) v->hint = ARMED_HUD_C_WARN;
            return true;
#    endif
        case ARMED_HUD_S_TIME:
        case ARMED_HUD_S_DATE: {
            static const char *const days[]   = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
            static const char *const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
            armed_time_t             t;
            if (!(armed_features_kb() & ARMED_F_CLOCK)) return false;
            if (!armed_clock_now(&t)) {
                v->valid = false;
                v->hint  = s == ARMED_HUD_S_TIME ? ARMED_HUD_C_LINE : ARMED_HUD_C_DIM;
                strcpy(v->text, s == ARMED_HUD_S_TIME ? "--:--" : "NO SYNC");
            } else if (s == ARMED_HUD_S_TIME) {
                v->num = t.hour * 60 + t.minute;
                snprintf(v->text, sizeof(v->text), "%02u:%02u", t.hour, t.minute);
            } else {
                snprintf(v->text, sizeof(v->text), "%s %u %s", days[t.weekday], t.day, months[t.month - 1]);
            }
            return true;
        }
        case ARMED_HUD_S_CAPS:
            flag(v, host_keyboard_led_state().caps_lock);
            return true;
        case ARMED_HUD_S_NUM:
            flag(v, host_keyboard_led_state().num_lock);
            return true;
        case ARMED_HUD_S_SCROLL:
            flag(v, host_keyboard_led_state().scroll_lock);
            return true;
        case ARMED_HUD_S_NKRO:
            flag(v, keymap_config.nkro);
            return true;
        case ARMED_HUD_S_GUI_LOCK:
            flag(v, keymap_config.no_gui);
            return true;
#    ifdef RGB_MATRIX_ENABLE
        case ARMED_HUD_S_LIGHT:
            flag(v, rgb_matrix_is_enabled());
            return true;
        case ARMED_HUD_S_BRIGHTNESS:
            v->num = rgb_matrix_get_val() * 100 / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            return true;
#        ifdef RGB_MATRIX_MODE_NAME_ENABLE
        case ARMED_HUD_S_EFFECT: {
            uint8_t mode = rgb_matrix_get_mode();
            int8_t  slot = armed_theme_slot(mode);
            if (slot < 0 || !armed_theme_name(slot, v->text, sizeof(v->text))) armed_put_name((uint8_t *)v->text, sizeof(v->text), rgb_matrix_get_mode_name(mode));
            return true;
        }
#        endif
#    endif
        case ARMED_HUD_S_UPTIME: {
            uint32_t m = timer_read32() / 60000;
            v->num     = m;
            if (m >= 1440) {
                snprintf(v->text, sizeof(v->text), "%luD %02luH", m / 1440, m / 60 % 24);
            } else {
                snprintf(v->text, sizeof(v->text), "%02lu:%02lu", m / 60, m % 60);
            }
            return true;
        }
    }
    return false;
}

static bool value_get(uint8_t s, armed_hud_value_t *v) {
    memset(v, 0, sizeof(*v));
    v->valid = true;
    v->hint  = ARMED_HUD_C_TEXT;
    if (s == ARMED_HUD_S_NONE) return false;
    return armed_hud_source_kb(s, v) || core_source(s, v);
}

static uint8_t source_kind(uint8_t s) {
    if (is_host(s)) return ARMED_HUD_K_NUMBER;
    if (is_host_text(s)) return ARMED_HUD_K_LINES;
    return s < ARMED_HUD_S_COUNT ? source_kinds[s] : ARMED_HUD_K_TEXT;
}

static void source_name(uint8_t s, char *buf, uint8_t len) {
    if (is_host(s)) {
        snprintf(buf, len, "HOST %u", s - ARMED_HUD_S_HOST + 1);
    } else if (is_host_text(s)) {
        snprintf(buf, len, "HOST TEXT %u", s - ARMED_HUD_S_HOST_TXT + 1);
    } else {
        copy(buf, s < ARMED_HUD_S_COUNT ? source_names[s] : "?", len);
    }
}

static void list_sources(void) {
    armed_hud_value_t v;
    avail_count = 0;
    for (uint8_t s = 1; s < ARMED_HUD_S_COUNT; s++) {
        if (value_get(s, &v)) avail[avail_count++] = s;
    }
    for (uint8_t i = 0; i < ARMED_HUD_HOST_VALUES; i++) avail[avail_count++] = ARMED_HUD_S_HOST + i;
    for (uint8_t i = 0; i < ARMED_HUD_HOST_TEXTS; i++) avail[avail_count++] = ARMED_HUD_S_HOST_TXT + i;
}

// "-12.5" with at least `digits` integer digits
static void format_num(const armed_hud_value_t *v, uint8_t digits, char *out, uint8_t len) {
    uint32_t a = v->num < 0 ? -(uint32_t)v->num : (uint32_t)v->num;
    uint32_t p = 1;
    for (uint8_t i = 0; i < v->dec; i++) p *= 10;
    int n = snprintf(out, len, "%s%0*lu", v->num < 0 ? "-" : "", digits, a / p);
    if (!v->dec || n < 0 || n + 1 + v->dec >= len) return;
    out[n++]       = '.';
    out[n + v->dec] = 0;
    for (uint32_t f = a % p, i = v->dec; i--; f /= 10) out[n + i] = '0' + f % 10;
}


static void value_text(const armed_hud_value_t *v, uint8_t digits, char *out, uint8_t len) {
    if (v->text[0]) {
        copy(out, v->text, len);
    } else if (!v->valid) {
        strcpy(out, "--");
    } else {
        format_num(v, digits, out, len);
    }
}

/* --------------------------------------------------------------------------
 * Layout
 * ------------------------------------------------------------------------ */

armed_hud_widget_t *armed_hud_add(uint8_t type, uint8_t source, uint8_t font, uint8_t fg, uint8_t bg, uint8_t flags, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t param, const char *label) {
    if (layout.count >= ARMED_HUD_WIDGETS) return NULL;
    armed_hud_widget_t *wd = &layout.widget[layout.count++];
    memset(wd, 0, sizeof(*wd));
    wd->type   = type;
    wd->source = source;
    wd->font   = font;
    wd->fg     = fg;
    wd->bg     = bg;
    wd->flags  = flags;
    wd->x      = x;
    wd->y      = y;
    wd->w      = w;
    wd->h      = h;
    wd->param  = param;
    wd->label  = ARMED_HUD_NO_LABEL;
    uint16_t n = label ? strlen(label) + 1 : 0;
    if (n && strings_used + n <= ARMED_HUD_STRINGS) {
        memcpy(&layout.strings[strings_used], label, n);
        wd->label = strings_used;
        strings_used += n;
    }
    return wd;
}

static void defaults(void) {
    memset(&layout, 0, sizeof(layout));
    layout.magic  = ARMED_HUD_MAGIC;
    layout.format = ARMED_HUD_FORMAT;
    strings_used  = 0;
    armed_hud_defaults_kb();
}

static bool valid(void) {
    return layout.magic == ARMED_HUD_MAGIC && layout.format == ARMED_HUD_FORMAT && layout.count <= ARMED_HUD_WIDGETS;
}

static void setup(void) {
    layout.strings[ARMED_HUD_STRINGS - 1] = 0;
    graphs                                = 0;
    for (uint8_t i = 0; i < layout.count && graphs < ARMED_HUD_GRAPHS; i++) {
        if (layout.widget[i].type != ARMED_HUD_W_GRAPH) continue;
        graph[graphs].widget = i;
        graph[graphs].n      = 0;
        graph[graphs].head   = 0;
        graph[graphs].at     = timer_read();
        graphs++;
    }
    redraw_all = true;
}

static void load(void) {
    if (!armed_hud_load_kb((uint8_t *)&layout, sizeof(layout)) || !valid()) defaults();
    setup();
}

static const char *label_of(const armed_hud_widget_t *wd) {
    return wd->label < ARMED_HUD_STRINGS ? &layout.strings[wd->label] : NULL;
}

/* --------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------ */

typedef struct {
    int16_t x0, y0, x1, y1;
} box_t;

static painter_font_handle_t font_of(const armed_hud_widget_t *wd) {
    return (painter_font_handle_t)fonts[wd->font < font_count ? wd->font : 0].handle;
}

// Widget's area clipped to the screen; w / h of 0 run to the screen edge / one line of its font
static bool area(const armed_hud_widget_t *wd, box_t *b) {
    if (wd->x < 0 || wd->y < 0 || wd->x >= scr_w || wd->y >= scr_h) return false;
    int16_t w = wd->w > 0 ? wd->w : scr_w - wd->x;
    int16_t h = wd->h > 0 ? wd->h : font_of(wd)->line_height;
    b->x0     = wd->x;
    b->y0     = wd->y;
    b->x1     = MIN(wd->x + w, scr_w) - 1;
    b->y1     = MIN(wd->y + h, scr_h) - 1;
    return true;
}

static void hsv(uint8_t role, uint8_t *c) {
    armed_hud_color_kb(role < ARMED_HUD_C_COUNT ? role : ARMED_HUD_C_TEXT, c);
}

static void fill(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t role) {
    if (x1 < x0 || y1 < y0) return;
    uint8_t c[3];
    hsv(role, c);
    qp_rect(lcd, x0, y0, x1, y1, c[0], c[1], c[2], true);
}

static void outline(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t role) {
    uint8_t c[3];
    hsv(role, c);
    qp_rect(lcd, x0, y0, x1, y1, c[0], c[1], c[2], false);
}

static void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t role) {
    uint8_t c[3];
    hsv(role, c);
    qp_line(lcd, x0, y0, x1, y1, c[0], c[1], c[2]);
}

static void text_at(int16_t x, int16_t y, painter_font_handle_t f, const char *s, uint8_t fg, uint8_t bg) {
    if (!*s) return;
    uint8_t a[3], b[3];
    hsv(fg, a);
    hsv(bg, b);
    qp_drawtext_recolor(lcd, x, y, f, s, a[0], a[1], a[2], b[0], b[1], b[2]);
}

// cut the text until it fits `width`
static int16_t fit(painter_font_handle_t f, char *s, int16_t width) {
    size_t  n = strlen(s);
    int16_t w = qp_textwidth(f, s);
    while (n && w > width) {
        s[--n] = 0;
        w      = qp_textwidth(f, s);
    }
    return w;
}

static int16_t aligned(const armed_hud_widget_t *wd, const box_t *b, int16_t width) {
    switch (wd->flags & ARMED_HUD_F_ALIGN) {
        case 1:
            return b->x0 + (b->x1 - b->x0 + 1 - width) / 2;
        case 2:
            return b->x1 + 1 - width;
    }
    return b->x0;
}

static uint8_t fg_role(const armed_hud_widget_t *wd, const armed_hud_value_t *v) {
    uint8_t r = wd->fg;
    if (r == ARMED_HUD_C_AUTO) r = v ? v->hint : ARMED_HUD_C_TEXT;
    if (v && v->valid && (wd->flags & ARMED_HUD_F_WARN) && ((wd->flags & ARMED_HUD_F_WARN_BELOW) ? v->num <= wd->warn : v->num >= wd->warn)) r = ARMED_HUD_C_WARN;
    return r;
}

// label with the first "{}" replaced by the value; no label = the value alone
static void expand(const armed_hud_widget_t *wd, const armed_hud_value_t *v, char *out, uint8_t len) {
    const char *l = label_of(wd);
    char        val[24];
    val[0] = 0;
    if (v) value_text(v, wd->param & 0x0F, val, sizeof(val));
    if (!l) l = "{}";
    const char *p = strstr(l, "{}");
    if (!p) {
        copy(out, l, len);
        return;
    }
    snprintf(out, len, "%.*s%s%s", (int)(p - l), l, val, p + 2);
}

static void draw_text(const armed_hud_widget_t *wd, const box_t *b, const armed_hud_value_t *v) {
    painter_font_handle_t f = font_of(wd);
    char                  s[48];
    expand(wd, v, s, sizeof(s));
    int16_t w = fit(f, s, b->x1 - b->x0 + 1);
    text_at(aligned(wd, b, w), b->y0, f, s, fg_role(wd, v), wd->bg);
}

static int32_t span(const armed_hud_widget_t *wd, const armed_hud_value_t *v, int32_t *lo) {
    *lo        = wd->lo;
    int32_t hi = wd->hi;
    if (hi <= *lo) {
        *lo = 0;
        hi  = 100;
        for (uint8_t i = 0; i < v->dec; i++) hi *= 10;
    }
    return hi - *lo;
}

// 0..1000
static int32_t level(const armed_hud_widget_t *wd, const armed_hud_value_t *v) {
    if (!v->valid) return 0;
    int32_t lo, s = span(wd, v, &lo);
    int32_t r = (int32_t)((int64_t)(v->num - lo) * 1000 / s);
    return r < 0 ? 0 : r > 1000 ? 1000 : r;
}

static void draw_bar(const armed_hud_widget_t *wd, const box_t *b, const armed_hud_value_t *v) {
    outline(b->x0, b->y0, b->x1, b->y1, ARMED_HUD_C_LINE);
    int16_t x0 = b->x0 + 2, x1 = b->x1 - 2, y0 = b->y0 + 2, y1 = b->y1 - 2;
    int16_t iw = x1 - x0 + 1;
    if (iw <= 0 || y1 < y0) return;
    int32_t l  = level(wd, v);
    uint8_t fg = fg_role(wd, v);
    if (wd->param < 2) {
        fill(x0, y0, x0 + iw * l / 1000 - 1, y1, fg);
        return;
    }
    uint8_t n  = wd->param;
    uint8_t on = (l * n + 500) / 1000;
    for (uint8_t i = 0; i < n; i++) {
        int16_t sx0 = x0 + iw * i / n, sx1 = x0 + iw * (i + 1) / n - 2;
        fill(sx0, y0, MAX(sx0, sx1), y1, i < on ? fg : ARMED_HUD_C_LINE);
    }
}

static void graph_sample(uint8_t g) {
    const armed_hud_widget_t *wd = &layout.widget[graph[g].widget];
    armed_hud_value_t         v;
    value_get(wd->source, &v);
    graph[g].v[graph[g].head] = level(wd, &v) * 255 / 1000;
    graph[g].head             = (graph[g].head + 1) % ARMED_HUD_GRAPH_POINTS;
    if (graph[g].n < ARMED_HUD_GRAPH_POINTS) graph[g].n++;
}

static void graphs_task(void) {
    for (uint8_t g = 0; g < graphs; g++) {
        uint8_t period = layout.widget[graph[g].widget].param;
        if (timer_elapsed(graph[g].at) < (period ? period : 10) * 100) continue;
        graph[g].at = timer_read();
        graph_sample(g);
    }
}

// newest sample at the right edge, one point per 2 px at most
static void draw_graph(uint8_t i, const armed_hud_widget_t *wd, const box_t *b, const armed_hud_value_t *v) {
    uint8_t g = 0;
    while (g < graphs && graph[g].widget != i) g++;
    if (g == graphs) return;
    int16_t w = b->x1 - b->x0 + 1, h = b->y1 - b->y0;
    line(b->x0, b->y1, b->x1, b->y1, ARMED_HUD_C_LINE);
    uint8_t pts = MIN(ARMED_HUD_GRAPH_POINTS, w / 2);
    if (pts < 2) return;
    uint8_t n  = MIN(graph[g].n, pts);
    uint8_t fg = fg_role(wd, v);
    int16_t px = 0, py = 0;
    for (uint8_t k = 0; k < n; k++) {
        uint8_t sv = graph[g].v[(graph[g].head + ARMED_HUD_GRAPH_POINTS - n + k) % ARMED_HUD_GRAPH_POINTS];
        int16_t x  = b->x0 + (int32_t)(w - 1) * (pts - n + k) / (pts - 1);
        int16_t y  = b->y1 - 1 - (int32_t)(h - 1) * sv / 255;
        if (wd->flags & ARMED_HUD_F_FILL) {
            int16_t nx = k + 1 < n ? b->x0 + (int32_t)(w - 1) * (pts - n + k + 1) / (pts - 1) - 1 : x;
            fill(x, y, MAX(x, nx), b->y1 - 1, fg);
        } else if (k) {
            line(px, py, x, y, fg);
        }
        px = x;
        py = y;
    }
}

static void draw_indicator(const armed_hud_widget_t *wd, const box_t *b, const armed_hud_value_t *v) {
    bool on = v && v->valid && v->num;
    if (wd->flags & ARMED_HUD_F_DOT) {
        uint8_t c = wd->fg == ARMED_HUD_C_AUTO ? (v ? v->hint : ARMED_HUD_C_TEXT) : on ? wd->fg : ARMED_HUD_C_LINE;
        fill(b->x0, b->y0, b->x1, b->y1, c);
        return;
    }
    uint8_t bg  = on ? ARMED_HUD_C_PANEL : wd->bg;
    uint8_t dot = on ? (wd->fg == ARMED_HUD_C_AUTO ? ARMED_HUD_C_ACCENT : wd->fg) : ARMED_HUD_C_LINE;
    int16_t h   = b->y1 - b->y0 + 1;
    fill(b->x0, b->y0, b->x1, b->y1, bg);
    outline(b->x0, b->y0, b->x1, b->y1, ARMED_HUD_C_LINE);
    fill(b->x0 + 4, b->y0 + (h - 6) / 2, b->x0 + 9, b->y0 + (h - 6) / 2 + 5, dot);
    painter_font_handle_t f = font_of(wd);
    char                  s[24];
    const char           *l = label_of(wd);
    if (l) {
        copy(s, l, sizeof(s));
    } else {
        source_name(wd->source, s, sizeof(s));
    }
    fit(f, s, b->x1 - b->x0 - 18);
    text_at(b->x0 + 16, b->y0 + (h - f->line_height - 1) / 2, f, s, on ? ARMED_HUD_C_TEXT : ARMED_HUD_C_DIM, bg);
}

static void draw_battery(const armed_hud_widget_t *wd, const box_t *b, const armed_hud_value_t *v) {
    armed_hud_value_t c;
    bool              chg = value_get(ARMED_HUD_S_CHARGING, &c) && c.num;
    uint8_t           pct = v && v->valid ? v->num : 0;
    uint8_t           fg  = wd->fg == ARMED_HUD_C_AUTO ? ARMED_HUD_C_ACCENT : wd->fg;
    int16_t           x = b->x0, y = b->y0;
    outline(x, y, x + 22, y + 10, ARMED_HUD_C_TEXT);
    fill(x + 23, y + 3, x + 24, y + 7, ARMED_HUD_C_TEXT);
    uint8_t n = (pct + 10) / 20;
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t r = ARMED_HUD_C_LINE;
        if (i < n) r = chg ? ARMED_HUD_C_OK : (pct > 15 ? fg : ARMED_HUD_C_WARN);
        fill(x + 2 + i * 4, y + 2, x + 4 + i * 4, y + 8, r);
    }
}

static void draw_frame(const armed_hud_widget_t *wd, const box_t *b) {
    uint8_t c = wd->fg == ARMED_HUD_C_AUTO ? ARMED_HUD_C_LINE : wd->fg;
    switch (wd->param) {
        case ARMED_HUD_FRAME_BRACKETS: {
            int16_t l = wd->lo > 0 ? wd->lo : 6;
            line(b->x0, b->y0, b->x0 + l, b->y0, c);
            line(b->x0, b->y0, b->x0, b->y0 + l, c);
            line(b->x1 - l, b->y0, b->x1, b->y0, c);
            line(b->x1, b->y0, b->x1, b->y0 + l, c);
            line(b->x0, b->y1, b->x0 + l, b->y1, c);
            line(b->x0, b->y1 - l, b->x0, b->y1, c);
            line(b->x1 - l, b->y1, b->x1, b->y1, c);
            line(b->x1, b->y1 - l, b->x1, b->y1, c);
            break;
        }
        case ARMED_HUD_FRAME_BOX:
            outline(b->x0, b->y0, b->x1, b->y1, c);
            break;
        case ARMED_HUD_FRAME_HLINE:
            line(b->x0, b->y0, b->x1, b->y0, c);
            break;
        case ARMED_HUD_FRAME_VLINE:
            line(b->x0, b->y0, b->x0, b->y1, c);
            break;
        case ARMED_HUD_FRAME_PANEL:
            fill(b->x0, b->y0, b->x1, b->y1, c);
            break;
    }
}

// wrapped at '\n' and between words at the box width; F_TAIL keeps the last lines that fit
static void draw_textbox(const armed_hud_widget_t *wd, const box_t *b, const armed_hud_value_t *v) {
    const char *src = NULL;
    if (is_host_text(wd->source) && v && v->valid) {
        src = host_txt[wd->source - ARMED_HUD_S_HOST_TXT].text;
    } else if (v) {
        src = v->text;
    }
    if (!src) return;
    painter_font_handle_t f     = font_of(wd);
    int16_t               pitch = f->line_height + wd->param;
    int16_t               width = b->x1 - b->x0 + 1;
    uint8_t               fit_n = MIN((b->y1 - b->y0 + 1 + wd->param) / MAX(pitch, 1), 16);
    bool                  tail  = wd->flags & ARMED_HUD_F_TAIL;
    if (!fit_n) return;
    uint8_t start[16], len[16], lines = 0;
    char    one[2] = {0, 0}, buf[64];
    uint8_t pos    = 0;
    while (src[pos] && (tail || lines < fit_n)) {
        uint8_t n = 0, cut = 0;
        int16_t w = 0;
        while (src[pos + n] && src[pos + n] != '\n' && n < sizeof(buf) - 1) {
            one[0] = src[pos + n];
            w += qp_textwidth(f, one);
            if (w > width && n) {
                if (cut) n = cut; // wrap after the last space
                break;
            }
            n++;
            if (one[0] == ' ') cut = n;
        }
        if (lines == fit_n) { // tail: slide the window
            memmove(start, start + 1, fit_n - 1);
            memmove(len, len + 1, fit_n - 1);
            lines--;
        }
        start[lines] = pos;
        len[lines++] = n;
        pos += n;
        if (src[pos] == '\n') pos++;
    }
    uint8_t fg = fg_role(wd, v);
    for (uint8_t i = 0; i < lines; i++) {
        memcpy(buf, src + start[i], len[i]);
        buf[len[i]] = 0;
        int16_t w   = fit(f, buf, width);
        text_at(aligned(wd, b, w), b->y0 + i * pitch, f, buf, fg, wd->bg);
    }
}

static bool is_static(const armed_hud_widget_t *wd) {
    return wd->type == ARMED_HUD_W_FRAME || (wd->type == ARMED_HUD_W_TEXT && wd->source == ARMED_HUD_S_NONE);
}

static void draw_widget(uint8_t i, const armed_hud_value_t *v, bool clear) {
    const armed_hud_widget_t *wd = &layout.widget[i];
    box_t                     b;
    if (!area(wd, &b)) return;
    if (clear && wd->type != ARMED_HUD_W_FRAME) fill(b.x0, b.y0, b.x1, b.y1, wd->bg);
    switch (wd->type) {
        case ARMED_HUD_W_TEXT:
            draw_text(wd, &b, v);
            break;
        case ARMED_HUD_W_BAR:
            draw_bar(wd, &b, v);
            break;
        case ARMED_HUD_W_GRAPH:
            draw_graph(i, wd, &b, v);
            break;
        case ARMED_HUD_W_INDICATOR:
            draw_indicator(wd, &b, v);
            break;
        case ARMED_HUD_W_BATTERY:
            draw_battery(wd, &b, v);
            break;
        case ARMED_HUD_W_FRAME:
            draw_frame(wd, &b);
            break;
        case ARMED_HUD_W_TEXTBOX:
            draw_textbox(wd, &b, v);
            break;
    }
}

static uint32_t fnv(uint32_t h, const void *data, size_t n) {
    const uint8_t *p = data;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

static uint32_t signature(uint8_t i, const armed_hud_value_t *v) {
    const armed_hud_widget_t *wd = &layout.widget[i];
    uint32_t                  h  = fnv(2166136261u, v, offsetof(armed_hud_value_t, text));
    h                            = fnv(h, v->text, strlen(v->text));
    switch (wd->type) {
        case ARMED_HUD_W_GRAPH:
            for (uint8_t g = 0; g < graphs; g++) {
                if (graph[g].widget == i) h = fnv(h, &graph[g].head, 2);
            }
            break;
        case ARMED_HUD_W_TEXTBOX:
            if (is_host_text(wd->source)) h = fnv(h, host_txt[wd->source - ARMED_HUD_S_HOST_TXT].text, strlen(host_txt[wd->source - ARMED_HUD_S_HOST_TXT].text));
            break;
        case ARMED_HUD_W_BATTERY: {
            armed_hud_value_t c;
            if (value_get(ARMED_HUD_S_CHARGING, &c)) h = fnv(h, &c.num, sizeof(c.num));
            break;
        }
    }
    return h;
}

static bool overlaps(const box_t *a, const box_t *b) {
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

void armed_hud_draw(bool all) {
    static uint16_t at;
    if (!lcd) return;
    if (editing) {
        if (timer_elapsed(edit_at) < 3000) return;
        editing = false; // the host went away halfway through
        load();
    }
    if (redraw_all) {
        redraw_all = false;
        all        = true;
    }
    graphs_task();
    if (!all && timer_elapsed(at) < 50) return;
    at = timer_read();

    if (all) fill(0, 0, scr_w - 1, scr_h - 1, ARMED_HUD_C_BG);
    for (uint8_t i = 0; i < layout.count; i++) {
        const armed_hud_widget_t *wd = &layout.widget[i];
        if (wd->type == ARMED_HUD_W_NONE) continue;
        if (is_static(wd)) {
            if (all) draw_widget(i, NULL, false);
            continue;
        }
        armed_hud_value_t v;
        value_get(wd->source, &v);
        uint32_t s = signature(i, &v);
        if (!all && s == sig[i]) continue;
        sig[i] = s;
        draw_widget(i, &v, !all || wd->bg != ARMED_HUD_C_BG);
        if (all) continue;
        // clearing the widget may have cut through lines and frames
        box_t b, o;
        if (!area(wd, &b)) continue;
        for (uint8_t j = 0; j < layout.count; j++) {
            const armed_hud_widget_t *fr = &layout.widget[j];
            if (fr->type == ARMED_HUD_W_FRAME && fr->param != ARMED_HUD_FRAME_PANEL && area(fr, &o) && overlaps(&b, &o)) draw_widget(j, NULL, false);
        }
    }
}

void armed_hud_init(const void *device, const armed_hud_font_t *f, uint8_t count) {
    lcd        = (painter_device_t)device;
    fonts      = f;
    font_count = count;
    qp_get_geometry(lcd, &scr_w, &scr_h, NULL, NULL, NULL);
    list_sources();
    load();
}

bool armed_hud_ready(void) {
    return lcd != NULL;
}

/* --------------------------------------------------------------------------
 * Protocol
 * ------------------------------------------------------------------------ */

static void info(uint8_t *d) {
    d[2]  = ARMED_HUD_FORMAT;
    d[3]  = ARMED_HUD_WIDGETS;
    d[4]  = sizeof(armed_hud_widget_t);
    d[5]  = sizeof(armed_hud_layout_t) & 0xFF;
    d[6]  = sizeof(armed_hud_layout_t) >> 8;
    d[7]  = ARMED_HUD_STRINGS & 0xFF;
    d[8]  = ARMED_HUD_STRINGS >> 8;
    d[9]  = ARMED_HUD_HOST_VALUES;
    d[10] = ARMED_HUD_HOST_TEXTS;
    d[11] = ARMED_HUD_TEXT_LEN;
    d[12] = font_count;
    d[13] = ARMED_HUD_C_COUNT;
    d[14] = avail_count;
    d[15] = scr_w & 0xFF;
    d[16] = scr_w >> 8;
    d[17] = scr_h & 0xFF;
    d[18] = scr_h >> 8;
    d[19] = ARMED_HUD_GRAPHS;
    d[20] = ARMED_HUD_GRAPH_POINTS;
}

static void push(const uint8_t *d) {
    uint8_t ttl = d[2], n = MIN(d[3], 4);
    for (uint8_t i = 0; i < n; i++) {
        const uint8_t *p = &d[4 + 6 * i];
        if (p[0] >= ARMED_HUD_HOST_VALUES) continue;
        host[p[0]].dec   = MIN(p[1], 9);
        host[p[0]].num   = (int32_t)((uint32_t)p[2] | (uint32_t)p[3] << 8 | (uint32_t)p[4] << 16 | (uint32_t)p[5] << 24);
        host[p[0]].state = ttl ? H_TIMED : H_SET;
        host[p[0]].until = timer_read32() + ttl * 1000UL;
    }
}

// the fonts are ASCII only
static char printable(uint8_t c) {
    if (c == '\n') return c;
    if (c == '\t') return ' ';
    return c < 0x20 || c > 0x7E ? '?' : c;
}

// chunks in order; a missing one drops the text
static void text_chunk(const uint8_t *d) {
    uint8_t slot = d[2], ttl = d[3], off = d[4], len = d[5];
    if (slot >= ARMED_HUD_HOST_TEXTS || len > 25 || off + len >= ARMED_HUD_TEXT_LEN) return;
    if (off == 0) {
        staging_slot = slot;
        staging_len  = 0;
    }
    if (staging_slot != slot || off != staging_len) {
        staging_slot = 0xFF;
        return;
    }
    for (uint8_t i = 0; i < len; i++) staging[off + i] = printable(d[7 + i]);
    staging_len += len;
    if (!(d[6] & 1)) return;
    staging[staging_len] = 0;
    memcpy(host_txt[slot].text, staging, staging_len + 1);
    host_txt[slot].state = ttl ? H_TIMED : H_SET;
    host_txt[slot].until = timer_read32() + ttl * 1000UL;
    staging_slot         = 0xFF;
}

bool armed_hud_receive(uint8_t *d, uint8_t length) {
    bool ok = lcd != NULL;
    if (d[1] == ARMED_HUD_PUSH || d[1] == ARMED_HUD_TEXT) {
        if (!ok) return false;
        if (d[1] == ARMED_HUD_PUSH) {
            push(d);
        } else {
            text_chunk(d);
        }
        return false;
    }
    if (ok) {
        switch (d[1]) {
            case ARMED_HUD_INFO:
                info(d);
                break;
            case ARMED_HUD_SOURCE: {
                if (d[2] >= avail_count) {
                    ok = false;
                    break;
                }
                uint8_t           s = avail[d[2]];
                armed_hud_value_t v;
                value_get(s, &v);
                d[3] = s;
                d[4] = source_kind(s);
                d[5] = is_host(s) ? 0xFF : v.dec;
                source_name(s, (char *)&d[6], length - 6);
                break;
            }
            case ARMED_HUD_VALUE: {
                armed_hud_value_t v;
                if (!value_get(d[2], &v)) {
                    ok = false;
                    break;
                }
                d[3] = v.valid;
                d[4] = v.hint;
                d[5] = v.dec;
                memcpy(&d[6], &v.num, 4);
                copy((char *)&d[10], v.text, length - 10);
                break;
            }
            case ARMED_HUD_FONT: {
                uint32_t off;
                uint8_t  f = d[2], n = d[7];
                memcpy(&off, &d[3], 4);
                if (f >= font_count || n > length - 8 || off > fonts[f].size || n > fonts[f].size - off) {
                    ok = false;
                    break;
                }
                memcpy(&d[8], fonts[f].data + off, n);
                break;
            }
            case ARMED_HUD_PALETTE:
                if (d[2] >= ARMED_HUD_C_COUNT) {
                    ok = false;
                    break;
                }
                armed_hud_color_kb(d[2], &d[3]);
                break;
            case ARMED_HUD_READ:
            case ARMED_HUD_WRITE: {
                uint16_t off = d[2] | d[3] << 8;
                uint8_t  n   = d[4];
                if (n > length - 5 || off + n > sizeof(layout)) {
                    ok = false;
                    break;
                }
                if (d[1] == ARMED_HUD_READ) {
                    memcpy(&d[5], (uint8_t *)&layout + off, n);
                } else {
                    // drawing waits for APPLY: half a layout is garbage
                    memcpy((uint8_t *)&layout + off, &d[5], n);
                    editing = true;
                    edit_at = timer_read();
                }
                break;
            }
            case ARMED_HUD_APPLY:
                editing = false;
                if (!valid()) {
                    load();
                    ok = false;
                    break;
                }
                setup();
                if (d[2]) ok = armed_hud_save_kb((const uint8_t *)&layout, sizeof(layout));
                break;
            case ARMED_HUD_REVERT:
                editing = false;
                if (d[2]) {
                    defaults();
                    setup();
                } else {
                    load();
                }
                break;
            default:
                ok = false;
        }
    }
    if (!ok) d[0] = id_unhandled;
    return true;
}

#else

bool armed_hud_ready(void) {
    return false;
}

bool armed_hud_receive(uint8_t *data, uint8_t length) {
    data[0] = id_unhandled;
    return data[1] != ARMED_HUD_PUSH && data[1] != ARMED_HUD_TEXT;
}

#endif
