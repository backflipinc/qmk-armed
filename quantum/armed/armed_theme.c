// Copyright 2026 backflipinc
// SPDX-License-Identifier: GPL-2.0-or-later

// Lighting themes, format in docs/armed.md. vial-gui-armed's theme_render.py must draw the same thing.

#include "armed.h"
#include "quantum.h"
#include "via.h"
#include "lib/lib8tion/lib8tion.h"
#include "keymap_introspection.h"
#ifdef DYNAMIC_KEYMAP_ENABLE
#    include "dynamic_keymap.h"
#endif
#ifdef TRI_LAYER_ENABLE
#    include "tri_layer.h"
#endif

__attribute__((weak)) bool armed_theme_load_kb(uint8_t slot, uint8_t *data, uint16_t size) {
    return false;
}

__attribute__((weak)) bool armed_theme_save_kb(uint8_t slot, const uint8_t *data, uint16_t size) {
    return false;
}

#if defined(RGB_MATRIX_ENABLE) && defined(ARMED_THEMES) && ARMED_THEMES > 0

#    if ARMED_THEMES > 8
#        error "ARMED_THEMES: at most 8 slots"
#    endif

#    define LEDS RGB_MATRIX_LED_COUNT
#    define MASK_BYTES ((LEDS + 7) / 8)
#    define LAYERS 8
#    define MAGIC 0xA7
#    define NAME_LEN 16

enum { T_NONE, T_SOLID, T_GRADIENT, T_RAINBOW, T_BREATHE, T_WAVE, T_SPARKLE, T_REACTIVE, T_RIPPLE, T_PAINT };
enum { B_NORMAL, B_ADD, B_MULTIPLY };

#    define F_RADIAL 0x01
#    define F_MIRROR 0x02
#    define F_REVERSE 0x04
#    define F_HIDDEN 0x80

#    define C_ALWAYS 0x00
#    define C_CAPS 0x01
#    define C_NO_CAPS 0x02
#    define C_LAYER 0x10
#    define C_NOT_LAYER 0x20

// which keys a layer covers: its mask, every key, or the keys of keymap layer 1..15
#    define K_CUSTOM 0x00
#    define K_ALL 0x80
#    define K_LAYER_MAX 0x0F

typedef struct PACKED {
    uint8_t type, blend, opacity, speed, dir, size, flags, cond;
    uint8_t a[3], b[3];
    uint8_t param, cx, cy, keys, reserved[2];
    uint8_t mask[MASK_BYTES];
} layer_t;

typedef struct PACKED {
    uint8_t magic, format, layers, reserved;
    char    name[NAME_LEN];
    layer_t layer[LAYERS];
    uint8_t paint[LEDS][3];
} theme_t;

_Static_assert(sizeof(layer_t) == 20 + MASK_BYTES, "theme layer layout");

static theme_t themes[ARMED_THEMES];
static bool    loaded;

static uint8_t spark[LAYERS][LEDS];
static uint8_t spark_mix[LAYERS][LEDS];


static void set_rgb(uint8_t *c, uint8_t r, uint8_t g, uint8_t b) {
    c[0] = r;
    c[1] = g;
    c[2] = b;
}

static layer_t *add_layer(theme_t *t, uint8_t type) {
    layer_t *l = &t->layer[t->layers++];
    l->type    = type;
    l->opacity = 255;
    l->size    = 128;
    l->param   = 255;
    l->cx      = 112;
    l->cy      = 32;
    l->keys    = K_ALL;
    memset(l->mask, 0xFF, MASK_BYTES);
    return l;
}

static void defaults(uint8_t slot) {
    theme_t *t = &themes[slot];
    memset(t, 0, sizeof(*t));
    t->magic  = MAGIC;
    t->format = ARMED_THEME_FORMAT;
    layer_t *l;
    switch (slot % 4) {
        case 0:
            strcpy(t->name, "AMBER DRIFT");
            l = add_layer(t, T_GRADIENT);
            set_rgb(l->a, 255, 170, 0);
            set_rgb(l->b, 255, 40, 0);
            l->speed = 16;
            l->flags = F_MIRROR;
            break;
        case 1:
            strcpy(t->name, "DEEP SEA");
            l = add_layer(t, T_GRADIENT);
            set_rgb(l->a, 0, 200, 255);
            set_rgb(l->b, 0, 20, 160);
            l->dir   = 64;
            l->speed = 12;
            l->flags = F_MIRROR;
            l = add_layer(t, T_SPARKLE);
            set_rgb(l->a, 255, 255, 255);
            set_rgb(l->b, 120, 255, 255);
            l->speed = 60;
            break;
        case 2:
            strcpy(t->name, "SHOCKWAVE");
            l = add_layer(t, T_SOLID);
            set_rgb(l->a, 20, 0, 60);
            l = add_layer(t, T_RIPPLE);
            set_rgb(l->a, 255, 170, 0);
            l->speed = 100;
            l->size  = 96;
            break;
        default:
            strcpy(t->name, "SPECTRUM");
            l = add_layer(t, T_RAINBOW);
            l->speed = 24;
            break;
    }
}

static bool valid(const theme_t *t) {
    return t->magic == MAGIC && t->format == ARMED_THEME_FORMAT && t->layers <= LAYERS;
}

static void load(uint8_t slot) {
    theme_t *t = &themes[slot];
    if (!armed_theme_load_kb(slot, (uint8_t *)t, sizeof(*t)) || !valid(t)) defaults(slot);
    t->name[NAME_LEN - 1] = 0;
}

// Lazy: keyboard storage may come up only in keyboard_post_init_kb()
static void ready(void) {
    if (loaded) return;
    loaded = true;
    for (uint8_t i = 0; i < ARMED_THEMES; i++) load(i);
}

int8_t armed_theme_slot(uint8_t mode) {
    return mode >= RGB_MATRIX_CUSTOM_THEME_1 && mode < RGB_MATRIX_CUSTOM_THEME_1 + ARMED_THEMES ? mode - RGB_MATRIX_CUSTOM_THEME_1 : -1;
}

bool armed_theme_name(uint8_t slot, char *buf, uint8_t length) {
    if (slot >= ARMED_THEMES || !length) return false;
    ready();
    armed_put_name((uint8_t *)buf, length, themes[slot].name);
    return true;
}


bool armed_theme_uses_layers(uint8_t slot) {
    if (slot >= ARMED_THEMES) return false;
    ready();
    const theme_t *t = &themes[slot];
    for (uint8_t i = 0; i < MIN(t->layers, LAYERS); i++) {
        const layer_t *l = &t->layer[i];
        if (l->type != T_NONE && !(l->flags & F_HIDDEN) && (l->cond & 0xF0) & (C_LAYER | C_NOT_LAYER)) return true;
    }
    return false;
}

typedef struct {
    bool    on;
    uint8_t phase;
    int16_t c, s;
    int32_t e;
    uint8_t maxd;
} frame_t;

static uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return a + ((int16_t)b - a) * t / 255;
}

static uint8_t tri8(uint8_t x) {
    return x < 128 ? x * 2 : (255 - x) * 2;
}

static uint8_t dist8(int16_t dx, int16_t dy) {
    return sqrt16((uint16_t)(dx * dx + dy * dy));
}

static bool condition(uint8_t cond) {
    uint8_t n = cond & 0x0F;
    switch (cond & 0xF0) {
        case 0x00:
            if (cond == C_CAPS) return host_keyboard_led_state().caps_lock;
            if (cond == C_NO_CAPS) return !host_keyboard_led_state().caps_lock;
            return true;
        // the active layer, the highest one on: holding both tri-layer keys turns on 1, 2 and 3, but shows 3
        case C_LAYER:
            return get_highest_layer(layer_state) == n;
        case C_NOT_LAYER:
            return get_highest_layer(layer_state) != n;
    }
    return true;
}

static void frame_setup(const layer_t *l, frame_t *f, uint32_t now) {
    f->on = l->type != T_NONE && !(l->flags & F_HIDDEN) && condition(l->cond);
    if (!f->on) return;
    f->phase = (uint8_t)(((uint64_t)now * l->speed) >> 8);
    if (l->flags & F_RADIAL) {
        uint8_t m = 1;
        m       = MAX(m, dist8(l->cx, l->cy));
        m       = MAX(m, dist8(224 - l->cx, l->cy));
        m       = MAX(m, dist8(l->cx, 64 - l->cy));
        m       = MAX(m, dist8(224 - l->cx, 64 - l->cy));
        f->maxd = m;
    } else {
        f->c = (int16_t)cos8(l->dir) - 128;
        f->s = (int16_t)sin8(l->dir) - 128;
        f->e = 112 * abs(f->c) + 32 * abs(f->s);
        if (f->e == 0) f->e = 1;
    }
}

static uint8_t position(const layer_t *l, const frame_t *f, uint8_t x, uint8_t y) {
    int32_t p;
    if (l->flags & F_RADIAL) {
        p = (int32_t)dist8(x - l->cx, y - l->cy) * 255 / f->maxd;
    } else {
        p = ((int32_t)(x - 112) * f->c + (int32_t)(y - 32) * f->s + f->e) * 255 / (2 * f->e);
    }
    p = p < 0 ? 0 : p > 255 ? 255 : p;
    return l->flags & F_REVERSE ? 255 - p : p;
}

// size 128 = one repeat across the keyboard, 0 = none
static uint8_t spread(const layer_t *l, uint8_t pos) {
    return l->size ? (uint8_t)((uint16_t)pos * 128 / l->size) : 0;
}

#    ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
static uint8_t reactive(const layer_t *l, uint8_t i, uint8_t x, uint8_t y) {
    uint16_t fade = 100 + l->size * 8;
    uint8_t  best = 0;
    for (uint8_t j = 0; j < g_last_hit_tracker.count; j++) {
        uint16_t tick = g_last_hit_tracker.tick[j];
        if (tick >= fade) continue;
        uint8_t lvl = 255 - (uint32_t)tick * 255 / fade;
        if (l->param == 0 || l->param == 255) {
            if (g_last_hit_tracker.index[j] != i) continue;
        } else {
            uint8_t d = dist8(x - g_last_hit_tracker.x[j], y - g_last_hit_tracker.y[j]);
            if (d > l->param) continue;
            lvl = (uint16_t)lvl * (l->param + 1 - d) / (l->param + 1);
        }
        best = MAX(best, lvl);
    }
    return best;
}

static uint8_t ripple(const layer_t *l, uint8_t x, uint8_t y) {
    uint8_t w    = 4 + l->size / 8;
    uint8_t best = 0;
    for (uint8_t j = 0; j < g_last_hit_tracker.count; j++) {
        uint32_t r = ((uint32_t)g_last_hit_tracker.tick[j] * (l->speed + 8)) >> 8;
        if (r >= 255) continue;
        int16_t d = abs((int16_t)dist8(x - g_last_hit_tracker.x[j], y - g_last_hit_tracker.y[j]) - (int16_t)r);
        if (d >= w) continue;
        uint8_t lvl = 255 - d * 255 / w;
        best        = MAX(best, (uint16_t)lvl * (255 - r) / 255);
    }
    return best;
}
#    endif

static uint8_t layer_color(const theme_t *t, uint8_t li, const frame_t *f, uint8_t i, uint8_t *out) {
    const layer_t *l = &t->layer[li];
    uint8_t        x = g_led_config.point[i].x, y = g_led_config.point[i].y;
    uint8_t        k;
    switch (l->type) {
        case T_SOLID:
            memcpy(out, l->a, 3);
            return 255;
        case T_GRADIENT:
            k = spread(l, position(l, f, x, y)) - f->phase;
            if (l->flags & F_MIRROR) k = tri8(k);
            break;
        case T_RAINBOW: {
            hsv_t hsv = {spread(l, position(l, f, x, y)) - f->phase, l->param, 255};
            rgb_t rgb = hsv_to_rgb(hsv);
            set_rgb(out, rgb.r, rgb.g, rgb.b);
            return 255;
        }
        case T_BREATHE:
            k = sin8(f->phase - spread(l, position(l, f, x, y)));
            break;
        case T_WAVE: {
            uint8_t pos    = position(l, f, x, y);
            uint8_t center = l->flags & F_MIRROR ? tri8(f->phase) : f->phase;
            uint8_t d      = abs((int16_t)pos - center);
            if (!(l->flags & F_MIRROR) && d > 128) d = 256 - d;
            uint8_t w = l->size / 2 + 1;
            memcpy(out, l->a, 3);
            return d >= w ? 0 : 255 - (uint16_t)d * 255 / w;
        }
        case T_SPARKLE: {
            uint8_t *lv = &spark[li][i];
            if (*lv) {
                *lv = qsub8(*lv, 1 + (255 - l->size) / 16);
            } else if (random16() < (uint16_t)(l->speed + 1) * 4) {
                *lv              = 255;
                spark_mix[li][i] = random8();
            }
            k = spark_mix[li][i];
            set_rgb(out, lerp8(l->a[0], l->b[0], k), lerp8(l->a[1], l->b[1], k), lerp8(l->a[2], l->b[2], k));
            return *lv;
        }
#    ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
        case T_REACTIVE:
            memcpy(out, l->a, 3);
            return reactive(l, i, x, y);
        case T_RIPPLE:
            memcpy(out, l->a, 3);
            return ripple(l, x, y);
#    endif
        case T_PAINT:
            memcpy(out, t->paint[i], 3);
            return 255;
        default:
            return 0;
    }
    set_rgb(out, lerp8(l->a[0], l->b[0], k), lerp8(l->a[1], l->b[1], k), lerp8(l->a[2], l->b[2], k));
    return 255;
}

// A key belongs to keymap layer n when something is bound there, or when it brings layer n up
static bool brings_up(uint16_t kc, uint8_t n) {
    if (IS_QK_MOMENTARY(kc)) return QK_MOMENTARY_GET_LAYER(kc) == n;
    if (IS_QK_LAYER_TAP(kc)) return QK_LAYER_TAP_GET_LAYER(kc) == n;
    if (IS_QK_TOGGLE_LAYER(kc)) return QK_TOGGLE_LAYER_GET_LAYER(kc) == n;
    if (IS_QK_LAYER_TAP_TOGGLE(kc)) return QK_LAYER_TAP_TOGGLE_GET_LAYER(kc) == n;
    if (IS_QK_ONE_SHOT_LAYER(kc)) return QK_ONE_SHOT_LAYER_GET_LAYER(kc) == n;
    if (IS_QK_LAYER_MOD(kc)) return QK_LAYER_MOD_GET_LAYER(kc) == n;
    if (IS_QK_TO(kc)) return QK_TO_GET_LAYER(kc) == n;
#    ifdef TRI_LAYER_ENABLE
    if (kc == QK_TRI_LAYER_LOWER) return n == get_tri_layer_lower_layer() || n == get_tri_layer_adjust_layer();
    if (kc == QK_TRI_LAYER_UPPER) return n == get_tri_layer_upper_layer() || n == get_tri_layer_adjust_layer();
#    endif
    return false;
}

#    define KEYS_TTL 250 // ms: the keymap is read again this often, so a remap shows up by itself

static uint8_t  keys_mask[K_LAYER_MAX + 1][MASK_BYTES];
static uint16_t keys_at[K_LAYER_MAX + 1];
static bool     keys_known[K_LAYER_MAX + 1];

static const uint8_t *layer_keys(uint8_t n) {
    if (keys_known[n] && timer_elapsed(keys_at[n]) < KEYS_TTL) return keys_mask[n];
    keys_known[n] = true;
    keys_at[n]    = timer_read();
    memset(keys_mask[n], 0, MASK_BYTES);
#    ifdef DYNAMIC_KEYMAP_ENABLE
    uint8_t count = dynamic_keymap_get_layer_count();
#    else
    uint8_t count = keymap_layer_count();
#    endif
    if (n >= count) return keys_mask[n];
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint8_t i = g_led_config.matrix_co[row][col];
            if (i == NO_LED || i >= LEDS) continue;
            keypos_t pos = {.row = row, .col = col};
            uint16_t kc  = keymap_key_to_keycode(n, pos);
            bool     on  = kc != KC_TRNS && kc != KC_NO;
            for (uint8_t m = 0; !on && m < count; m++)
                if (m != n) on = brings_up(keymap_key_to_keycode(m, pos), n);
            if (on) keys_mask[n][i / 8] |= 1 << (i % 8);
        }
    }
    return keys_mask[n];
}

static bool covers(const layer_t *l, uint8_t i) {
    if (l->keys == K_ALL) return true;
    const uint8_t *mask = l->keys >= 1 && l->keys <= K_LAYER_MAX ? layer_keys(l->keys) : l->mask;
    return mask[i / 8] & (1 << (i % 8));
}

bool armed_theme_render(uint8_t slot, effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    ready();
    const theme_t *t = &themes[slot];
    if (params->init) memset(spark, 0, sizeof(spark));

    uint8_t n = MIN(t->layers, LAYERS);
    frame_t f[LAYERS];
    for (uint8_t li = 0; li < n; li++) frame_setup(&t->layer[li], &f[li], g_rgb_timer);
    uint8_t v = rgb_matrix_config.hsv.v;

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint8_t o[3] = {0, 0, 0};
        for (uint8_t li = 0; li < n; li++) {
            const layer_t *l = &t->layer[li];
            if (!f[li].on || !covers(l, i)) continue;
            uint8_t c[3];
            uint8_t a = layer_color(t, li, &f[li], i, c);
            a         = (uint16_t)a * l->opacity / 255;
            if (!a) continue;
            for (uint8_t k = 0; k < 3; k++) {
                switch (l->blend) {
                    case B_ADD:
                        o[k] = qadd8(o[k], (uint16_t)c[k] * a / 255);
                        break;
                    case B_MULTIPLY:
                        o[k] = lerp8(o[k], (uint16_t)o[k] * c[k] / 255, a);
                        break;
                    default:
                        o[k] = lerp8(o[k], c[k], a);
                        break;
                }
            }
        }
        rgb_matrix_set_color(i, (uint16_t)o[0] * v / 255, (uint16_t)o[1] * v / 255, (uint16_t)o[2] * v / 255);
    }
    return rgb_matrix_check_finished_leds(led_max);
}


static uint8_t *range(uint8_t *data, uint8_t length) {
    uint8_t  slot = data[2];
    uint16_t off  = data[3] | (data[4] << 8);
    uint8_t  n    = data[5];
    if (slot >= ARMED_THEMES || n > length - 6 || off + n > sizeof(theme_t)) return NULL;
    return (uint8_t *)&themes[slot] + off;
}

void armed_theme_receive(uint8_t *data, uint8_t length) {
    ready();
    bool     ok = true;
    uint8_t *p;
    switch (data[1]) {
        case ARMED_THEME_INFO:
            data[2] = ARMED_THEMES;
            data[3] = LAYERS;
            data[4] = LEDS;
            data[5] = sizeof(theme_t) & 0xFF;
            data[6] = sizeof(theme_t) >> 8;
            data[7] = ARMED_THEME_FORMAT;
            for (uint8_t i = 0; i < ARMED_THEMES; i++) data[8 + i] = RGB_MATRIX_CUSTOM_THEME_1 + i;
            break;
        case ARMED_THEME_READ:
            if ((p = range(data, length)) != NULL) {
                memcpy(&data[6], p, data[5]);
            } else {
                ok = false;
            }
            break;
        case ARMED_THEME_WRITE:
            if ((p = range(data, length)) != NULL) {
                memcpy(p, &data[6], data[5]);
            } else {
                ok = false;
            }
            break;
        case ARMED_THEME_SAVE:
            ok = data[2] < ARMED_THEMES && valid(&themes[data[2]]) && armed_theme_save_kb(data[2], (uint8_t *)&themes[data[2]], sizeof(theme_t));
            break;
        case ARMED_THEME_REVERT:
            if (data[2] < ARMED_THEMES) {
                load(data[2]);
            } else {
                ok = false;
            }
            break;
        default:
            ok = false;
            break;
    }
    if (!ok) data[0] = id_unhandled;
}

#else

int8_t armed_theme_slot(uint8_t mode) {
    return -1;
}

bool armed_theme_name(uint8_t slot, char *buf, uint8_t length) {
    return false;
}

bool armed_theme_uses_layers(uint8_t slot) {
    return false;
}

void armed_theme_receive(uint8_t *data, uint8_t length) {
    data[0] = id_unhandled;
}

#endif
