// IO Type 84 Pro Wireless — "safe" bring-up keymap: no display, QMK console diagnostics

#include QMK_KEYBOARD_H

enum layers { _BASE, _FN };

// Watchdog test (Fn+Backspace): deliberately hangs the firmware; after ~4 s the WDT resets into the ROM bootloader
enum custom_keycodes { WDT_TEST = SAFE_RANGE };

// Key order follows keyboard.json:
// row 0: Esc F1..F12 PrtSc [knob press, Esc spot] [knob press, PrtSc spot]
// row 1: ` 1..0 - = Bksp Ins PgUp
// row 2: Tab Q..] \ Del PgDn
// row 3: Caps A..' Enter
// row 4: LShift Z../ RShift Up
// row 5: LCtrl LGui LAlt Space RAlt Fn RCtrl Left Down Right
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT(
        KC_ESC,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  KC_PSCR, KC_MUTE, KC_MUTE,
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC, KC_INS,  KC_PGUP,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS, KC_DEL,  KC_PGDN,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT, KC_ENT,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT, KC_UP,
        KC_LCTL, KC_LGUI, KC_LALT,                   KC_SPC,                    KC_RALT, MO(_FN), KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT
    ),
    [_FN] = LAYOUT(
        QK_BOOT, KC_BRID, KC_BRIU, _______, _______, KC_MPRV, KC_MPLY, KC_MNXT, _______, KC_MUTE, KC_VOLD, KC_VOLU, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, WDT_TEST, _______, KC_HOME,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_END,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, NK_TOGG, _______, _______, _______, _______, _______, _______,
        _______, GU_TOGG, _______,                   _______,                   _______, _______, _______, _______, _______, _______
    ),
};

#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_BASE] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [_FN]   = { ENCODER_CCW_CW(KC_MPRV, KC_MNXT), ENCODER_CCW_CW(KC_MPRV, KC_MNXT) },
};
#endif

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode == WDT_TEST && record->event.pressed) {
        while (1) {
        } // main loop stuck -> WDT not fed -> reset -> early code jumps to ROM
    }
    return true;
}

void keyboard_post_init_user(void) {
    // Diagnostics: print every matrix change to the QMK console
    debug_enable = true;
    debug_matrix = false; // positions are known; enable when needed
}

// Diagnostics: "pin sniffer" — prints changes of every input (except matrix rows)
// Matrix rows: P1.14 P1.15 P1.19 P3.19 P0.19 P0.18
static const uint32_t sniff_exclude[4] = {
    (1UL << 18) | (1UL << 19),               // P0
    (1UL << 14) | (1UL << 15) | (1UL << 19), // P1
    0,                                       // P2
    (1UL << 19),                             // P3
};

void housekeeping_task_user(void) {
    static uint32_t prev[4];
    static bool     primed = false;
    SN_GPIO1_Type  *ports[4] = {(SN_GPIO1_Type *)SN_GPIO0, (SN_GPIO1_Type *)SN_GPIO1, (SN_GPIO1_Type *)SN_GPIO2, (SN_GPIO1_Type *)SN_GPIO3};

    for (uint8_t p = 0; p < 4; p++) {
        uint32_t inputs = ~ports[p]->MODE & ~sniff_exclude[p] & 0x000FFFFFUL;
        uint32_t now    = ports[p]->DATA & inputs;
        if (primed && now != prev[p]) {
            uint32_t diff = now ^ prev[p];
            for (uint8_t b = 0; b < 20; b++) {
                if (diff & (1UL << b)) {
                    uprintf("PIN P%u.%u -> %u\n", p, b, (unsigned)((now >> b) & 1));
                }
            }
        }
        prev[p] = now;
    }
    primed = true;

    // One-off port configuration dump 3 s after start + P3.5/P3.6 levels every 2 s
    static bool     dumped = false;
    static uint32_t last_lvl;
    if (!dumped && timer_read32() > 3000) {
        dumped = true;
        for (uint8_t p = 0; p < 4; p++) {
            uprintf("P%u MODE=%08lx CFG=%08lx DATA=%08lx\n", p, ports[p]->MODE, ports[p]->CFG, ports[p]->DATA);
        }
        uprintf("P0 CFG1=%08lx P1 CFG1=%08lx P3 CFG1=%08lx\n", ((SN_GPIO1_Type *)SN_GPIO0)->CFG1, ((SN_GPIO1_Type *)SN_GPIO1)->CFG1, ((SN_GPIO1_Type *)SN_GPIO3)->CFG1);
        uprintf("PFPA UART=%08lx SPI=%08lx I2C=%08lx I2S=%08lx\n", SN_PFPA->UART, SN_PFPA->SPI, SN_PFPA->I2C, SN_PFPA->I2S);

        // SWD pin hunt: disable SWD (as the stock firmware does) and see which pins change
        uint32_t before[4], after[4], cfg_before[4];
        for (uint8_t p = 0; p < 4; p++) {
            before[p]     = ports[p]->DATA;
            cfg_before[p] = ports[p]->CFG;
        }
        uprintf("SWDCTRL before=%08lx\n", SN_SYS0->SWDCTRL);
        SN_SYS0->SWDCTRL |= 1UL; // SWDDIS
        wait_ms(5);
        uprintf("SWDCTRL after=%08lx\n", SN_SYS0->SWDCTRL);
        for (uint8_t p = 0; p < 4; p++) {
            after[p] = ports[p]->DATA;
            uint32_t diff = (before[p] ^ after[p]) & 0x000FFFFFUL;
            uprintf("SWDDIS P%u DATA %08lx -> %08lx diff=%08lx CFG=%08lx\n", p, before[p], after[p], diff, cfg_before[p]);
        }
    }
    if (timer_elapsed32(last_lvl) > 2000) {
        last_lvl = timer_read32();
        uint32_t d = ((SN_GPIO1_Type *)SN_GPIO3)->DATA;
        uprintf("LVL P3.5=%u P3.6=%u\n", (unsigned)((d >> 5) & 1), (unsigned)((d >> 6) & 1));
    }
}
