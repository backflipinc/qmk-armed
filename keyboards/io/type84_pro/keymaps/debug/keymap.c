// IO Type 84 Pro Wireless — debug keymap (default + QMK console, USB stays up in BT/2.4G)

#include QMK_KEYBOARD_H
#include "type84_pro.h"

enum layers { _BASE, _FN };

// Key order follows keyboard.json:
// row 0: Esc F1..F12 PrtSc [knob press, Esc spot] [knob press, PrtSc spot]
// row 1: ` 1..0 - = Bksp Ins PgUp
// row 2: Tab Q..] \ Del PgDn
// row 3: Caps A..' Enter
// row 4: LShift Z../ RShift Up
// row 5: LCtrl LGui LAlt Space RAlt Fn RCtrl Left Down Right
//
// Fn layer: 1-5 BT slot (hold 3 s = pair), 6 2.4G pair (hold), S auto-sleep timer,
// H/J hue, K/L effect speed, Space lighting on/off, Enter/Ctrl/arrows = screen menu
// (outside the menu: Up/Down brightness, Left/Right effect), N NKRO, Win = Win lock.
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
        _______, KC_BRID, KC_BRIU, _______, _______, KC_MPRV, KC_MPLY, KC_MNXT, _______, KC_MUTE, KC_VOLD, KC_VOLU, _______, _______, _______, _______,
        _______, BT_1,    BT_2,    BT_3,    BT_4,    BT_5,    RF_PAIR, _______, _______, _______, _______, _______, _______, _______, _______, KC_HOME,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_END,
        _______, _______, SLEEP_T, _______, _______, _______, RM_HUED, RM_HUEU, RM_SPDD, RM_SPDU, _______, _______, UI_OK,
        _______, _______, _______, _______, _______, _______, NK_TOGG, _______, _______, _______, _______, _______, UI_UP,
        UI_BACK, GU_TOGG, _______,                   RM_TOGG,                   _______, _______, UI_BACK, UI_LEFT, UI_DOWN, UI_RIGHT
    ),
};

#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [_BASE] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [_FN]   = { ENCODER_CCW_CW(KC_MPRV, KC_MNXT), ENCODER_CCW_CW(KC_MPRV, KC_MNXT) },
};
#endif

void keyboard_post_init_user(void) {
    debug_enable = true; // radio UART frames etc. -> `qmk console`
}
