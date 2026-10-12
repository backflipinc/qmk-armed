// Copyright 2026 backflipinc
// SPDX-License-Identifier: GPL-2.0-or-later

// One RGB Matrix effect per theme slot; include from rgb_matrix_kb.inc.

#if defined(ARMED_ENABLE) && defined(ARMED_THEMES) && ARMED_THEMES > 0
RGB_MATRIX_EFFECT(THEME_1)
#    if ARMED_THEMES > 1
RGB_MATRIX_EFFECT(THEME_2)
#    endif
#    if ARMED_THEMES > 2
RGB_MATRIX_EFFECT(THEME_3)
#    endif
#    if ARMED_THEMES > 3
RGB_MATRIX_EFFECT(THEME_4)
#    endif
#    if ARMED_THEMES > 4
RGB_MATRIX_EFFECT(THEME_5)
#    endif
#    if ARMED_THEMES > 5
RGB_MATRIX_EFFECT(THEME_6)
#    endif
#    if ARMED_THEMES > 6
RGB_MATRIX_EFFECT(THEME_7)
#    endif
#    if ARMED_THEMES > 7
RGB_MATRIX_EFFECT(THEME_8)
#    endif

#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS
bool armed_theme_render(uint8_t slot, effect_params_t *params);

static bool THEME_1(effect_params_t *params) {
    return armed_theme_render(0, params);
}
#        if ARMED_THEMES > 1
static bool THEME_2(effect_params_t *params) {
    return armed_theme_render(1, params);
}
#        endif
#        if ARMED_THEMES > 2
static bool THEME_3(effect_params_t *params) {
    return armed_theme_render(2, params);
}
#        endif
#        if ARMED_THEMES > 3
static bool THEME_4(effect_params_t *params) {
    return armed_theme_render(3, params);
}
#        endif
#        if ARMED_THEMES > 4
static bool THEME_5(effect_params_t *params) {
    return armed_theme_render(4, params);
}
#        endif
#        if ARMED_THEMES > 5
static bool THEME_6(effect_params_t *params) {
    return armed_theme_render(5, params);
}
#        endif
#        if ARMED_THEMES > 6
static bool THEME_7(effect_params_t *params) {
    return armed_theme_render(6, params);
}
#        endif
#        if ARMED_THEMES > 7
static bool THEME_8(effect_params_t *params) {
    return armed_theme_render(7, params);
}
#        endif
#    endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#endif
