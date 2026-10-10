// Двухцветные эффекты подсветки: цвет 1 — основной цвет RGB Matrix (меню COLOR / HUE / SATUR),
// цвет 2 — меню COLOR 2 (type84_color2(), хранится в eeconfig_kb). Яркость общая.
// «Чёрный» в меню хранится как H=0, S=1 (type84_is_black) и здесь превращается в выключенный LED.

RGB_MATRIX_EFFECT(TWO_TONE)
RGB_MATRIX_EFFECT(GRADIENT_2)
RGB_MATRIX_EFFECT(CROSSFADE_2)
RGB_MATRIX_EFFECT(SCANNER_2)
RGB_MATRIX_EFFECT(STARLIGHT_2)
#ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
RGB_MATRIX_EFFECT(REACTIVE_2)
RGB_MATRIX_EFFECT(RIPPLE_2)
#endif

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

hsv_t type84_color2(void);         // type84_pro.c
bool  type84_is_black(hsv_t c);    // type84_pro.c

static inline uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return a + (((int16_t)b - a) * t) / 255;
}

static rgb_t lerp_rgb(rgb_t a, rgb_t b, uint8_t t) {
    rgb_t c = {.r = lerp8(a.r, b.r, t), .g = lerp8(a.g, b.g, t), .b = lerp8(a.b, b.b, t)};
    return c;
}

static rgb_t to_rgb(hsv_t c) {
    if (type84_is_black(c)) {
        rgb_t k = {0, 0, 0};
        return k;
    }
    c.v = rgb_matrix_config.hsv.v;
    return rgb_matrix_hsv_to_rgb(c);
}

static rgb_t c1_rgb(void) {
    return to_rgb(rgb_matrix_config.hsv);
}

static rgb_t c2_rgb(void) {
    return to_rgb(type84_color2());
}

// Буквы — цвет 1, модификаторы (флаг LED_FLAG_MODIFIER в keyboard.json) — цвет 2
static bool TWO_TONE(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    rgb_t c1 = c1_rgb(), c2 = c2_rgb();
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        rgb_t c = HAS_FLAGS(g_led_config.flags[i], LED_FLAG_MODIFIER) ? c2 : c1;
        rgb_matrix_set_color(i, c.r, c.g, c.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// Переход цвет 1 -> цвет 2 слева направо; волна медленно ходит туда-обратно (скорость — SPEED)
static bool GRADIENT_2(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    rgb_t   c1 = c1_rgb(), c2 = c2_rgb();
    uint8_t shift = scale16by8(g_rgb_timer, rgb_matrix_config.speed / 8 + 1);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint8_t pos = (uint8_t)(g_led_config.point[i].x * 255 / 224) + shift;
        uint8_t t   = pos < 128 ? pos * 2 : (255 - pos) * 2; // треугольник: без скачка на стыке
        rgb_t   c   = lerp_rgb(c1, c2, t);
        rgb_matrix_set_color(i, c.r, c.g, c.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// Вся подсветка плавно перетекает цвет 1 <-> цвет 2
static bool CROSSFADE_2(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    uint8_t t = sin8(scale16by8(g_rgb_timer, rgb_matrix_config.speed / 4 + 1));
    rgb_t   c = lerp_rgb(c1_rgb(), c2_rgb(), t);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        rgb_matrix_set_color(i, c.r, c.g, c.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// Радар: полоса цвета 2 с мягким хвостом ходит слева направо и обратно по фону цвета 1
static bool SCANNER_2(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    rgb_t   c1  = c1_rgb(), c2 = c2_rgb();
    uint8_t ph  = scale16by8(g_rgb_timer, rgb_matrix_config.speed / 4 + 1);
    uint8_t tri = ph < 128 ? ph * 2 : (255 - ph) * 2;
    int16_t pos = (int16_t)tri * 224 / 255;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        int16_t d = abs((int16_t)g_led_config.point[i].x - pos);
        uint8_t t = d >= 48 ? 0 : 255 - d * 255 / 48;
        rgb_t   c = lerp_rgb(c1, c2, t);
        rgb_matrix_set_color(i, c.r, c.g, c.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// Звёзды: на фоне цвета 1 случайные клавиши вспыхивают цветом 2 и гаснут (частота — SPEED)
static uint8_t star_level[RGB_MATRIX_LED_COUNT];

static bool STARLIGHT_2(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (params->init) memset(star_level, 0, sizeof(star_level));
    rgb_t   c1    = c1_rgb(), c2 = c2_rgb();
    uint8_t spark = rgb_matrix_config.speed / 32 + 1; // шанс вспышки на проход, из 256*4
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        if (star_level[i]) {
            star_level[i] = qsub8(star_level[i], 3);
        } else if (random8() < spark && random8() < 64) {
            star_level[i] = 255;
        }
        rgb_t c = lerp_rgb(c1, c2, star_level[i]);
        rgb_matrix_set_color(i, c.r, c.g, c.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

#    ifdef RGB_MATRIX_KEYREACTIVE_ENABLED
// Фон — цвет 1, нажатая клавиша вспыхивает цветом 2 и плавно возвращается
static bool REACTIVE_2(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    rgb_t    c1       = c1_rgb(), c2 = c2_rgb();
    uint16_t max_tick = 65535 / qadd8(rgb_matrix_config.speed, 1);
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint16_t tick = max_tick;
        for (int8_t j = g_last_hit_tracker.count - 1; j >= 0; j--) {
            if (g_last_hit_tracker.index[j] == i && g_last_hit_tracker.tick[j] < tick) {
                tick = g_last_hit_tracker.tick[j];
                break;
            }
        }
        uint16_t offset = scale16by8(tick, qadd8(rgb_matrix_config.speed, 1));
        uint8_t  t      = offset > 255 ? 0 : 255 - offset; // 255 — только что нажата
        rgb_t    c      = lerp_rgb(c1, c2, t);
        rgb_matrix_set_color(i, c.r, c.g, c.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}

// Рябь: от каждого нажатия расходится кольцо цвета 2 по фону цвета 1
static bool RIPPLE_2(effect_params_t *params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    rgb_t   c1    = c1_rgb(), c2 = c2_rgb();
    uint8_t count = g_last_hit_tracker.count;
    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        uint8_t t = 0;
        for (uint8_t j = 0; j < count; j++) {
            int16_t  dx   = g_led_config.point[i].x - g_last_hit_tracker.x[j];
            int16_t  dy   = g_led_config.point[i].y - g_last_hit_tracker.y[j];
            uint8_t  dist = sqrt16(dx * dx + dy * dy);
            uint16_t tick = scale16by8(g_last_hit_tracker.tick[j], qadd8(rgb_matrix_config.speed, 1));
            if (tick > 255 + 64) continue;                    // кольцо ушло за край
            int16_t d    = abs((int16_t)tick - dist);         // расстояние до фронта кольца
            uint8_t fade = tick > 255 ? 0 : 255 - (uint8_t)tick; // кольцо тускнеет по мере роста
            if (d < 24) t = qadd8(t, scale8(255 - d * 10, fade));
        }
        rgb_t c = lerp_rgb(c1, c2, t);
        rgb_matrix_set_color(i, c.r, c.g, c.b);
    }
    return rgb_matrix_check_finished_leds(led_max);
}
#    endif

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
