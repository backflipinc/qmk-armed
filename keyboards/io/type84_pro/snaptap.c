// SnapTap (SOCD) on the case switch: of two opposing keys held together only one (or none) is sent.

#include "quantum.h"
#include "snaptap.h"
#include "type84_pro.h"
#include "settings.h"

#ifndef SNAPTAP_PIN
#    define SNAPTAP_PIN D15
#endif
#ifndef SNAPTAP_ON_STATE
#    define SNAPTAP_ON_STATE 0 // stock: low = on (unverified)
#endif
#ifndef SNAPTAP_DEBOUNCE_MS
#    define SNAPTAP_DEBOUNCE_MS 30
#endif
#define SNAPTAP_FLASH_MS 1500

static const uint16_t pairs[][2] = {
    {KC_A, KC_D},
    {KC_W, KC_S},
};
#define NPAIRS ARRAY_SIZE(pairs)

static struct {
    bool   held[2];
    int8_t sent; // key of the pair registered on the host: 0, 1 or -1
} st[NPAIRS];

static bool     active, pending, flash_on;
static uint8_t  mode;
static uint16_t pending_since, flash_at;

static bool read_pin(void) {
    return gpio_read_pin(SNAPTAP_PIN) == SNAPTAP_ON_STATE;
}

static void reset_state(void) {
    for (uint8_t p = 0; p < NPAIRS; p++) {
        st[p].held[0] = st[p].held[1] = false;
        st[p].sent                    = -1;
    }
}

void snaptap_init(void) {
    gpio_set_pin_input_high(SNAPTAP_PIN);
    wait_us(50);
    active = read_pin();
    mode   = kb_settings()->snaptap_mode % SNAPTAP_MODES;
    reset_state();
}

void snaptap_task(void) {
    bool now = read_pin();
    if (now == active) {
        pending = false;
        return;
    }
    if (!pending) {
        pending       = true;
        pending_since = timer_read();
        return;
    }
    if (timer_elapsed(pending_since) < SNAPTAP_DEBOUNCE_MS) return;
    pending = false;
    active  = now;
    clear_keyboard(); // keys held across the switch would otherwise get stuck
    reset_state();
    flash_on = true;
    flash_at = timer_read();
    type84_lcd_toast(active ? "SNAPTAP ON" : "SNAPTAP OFF");
}

bool snaptap_active(void) {
    return active;
}

uint8_t snaptap_mode(void) {
    return mode;
}

const char *snaptap_mode_name(uint8_t m) {
    return m == SNAPTAP_NEUTRAL ? "NEUTRAL" : "LAST";
}

void snaptap_set_mode(uint8_t m) {
    mode                       = m % SNAPTAP_MODES;
    kb_settings()->snaptap_mode = mode;
    kb_settings_save();
    clear_keyboard();
    reset_state();
}

const char *snaptap_keys_name(void) {
    return "A/D  W/S";
}

static void send(uint8_t p, int8_t k) {
    if (st[p].sent == k) return;
    if (st[p].sent >= 0) unregister_code(pairs[p][st[p].sent]);
    if (k >= 0) register_code(pairs[p][k]);
    st[p].sent = k;
}

bool snaptap_process(uint16_t keycode, keyrecord_t *record) {
    if (!active || record->event.type != KEY_EVENT) return true;

    for (uint8_t p = 0; p < NPAIRS; p++) {
        int8_t i = keycode == pairs[p][0] ? 0 : keycode == pairs[p][1] ? 1 : -1;
        if (i < 0) continue;
        int8_t j = 1 - i;

        st[p].held[i] = record->event.pressed;
        if (record->event.pressed) {
            send(p, st[p].held[j] && mode == SNAPTAP_NEUTRAL ? -1 : i);
        } else {
            send(p, st[p].held[j] ? j : -1);
        }
        return false;
    }
    return true;
}

bool snaptap_indicators(uint8_t led_min, uint8_t led_max) {
    if (!flash_on) return false;
    if (timer_elapsed(flash_at) > SNAPTAP_FLASH_MS) {
        flash_on = false;
        return false;
    }
    uint8_t r = 0, g = 0, b = 0;
    if ((timer_elapsed(flash_at) / 125) % 2 == 0) {
        r = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
        g = b = active ? r : 0;
    }
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint8_t led = g_led_config.matrix_co[row][col];
            if (led == NO_LED || led < led_min || led >= led_max) continue;
            uint16_t kc = keymap_key_to_keycode(0, (keypos_t){.row = row, .col = col});
            for (uint8_t p = 0; p < NPAIRS; p++) {
                if (kc == pairs[p][0] || kc == pairs[p][1]) rgb_matrix_set_color(led, r, g, b);
            }
        }
    }
    return true;
}
