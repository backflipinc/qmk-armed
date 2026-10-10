// IO Type 84 Pro Wireless — код клавиатуры
// Карта железа: docs/hardware-map.md

#include "quantum.h"
#include "usb_util.h"
#include "wireless.h"
#include "type84_pro.h"
#include "battery.h"
#include "battery_cw2017.h"
#include "usb_main.h"
#include "strip.h"
#include "snaptap.h"
#include "settings.h"

void safety_wdt_feed(void); // safety.c

#ifdef QUANTUM_PAINTER_ENABLE
#    include "qp.h"
#    include "qp_comms.h"
#    include "qp_internal.h"
extern const painter_comms_with_command_vtable_t lcd_spi_comms_vtable; // lcd_spi.c
#    include "ui.h"
#endif

/* --------------------------------------------------------------------------
 * Пины
 * ------------------------------------------------------------------------ */

// Пины выбора LED-строк (R,G,B для строк 0..5), активный HIGH.
// Пока нет драйвера подсветки — держим LOW, иначе LED могут засвечиваться
// во время сканирования матрицы (колонки общие).
static const pin_t led_row_pins[] = {
    B5,  A11, B4,  // строка 0
    D8,  A8,  A9,  // строка 1
    D11, D9,  D10, // строка 2
    D16, D12, D13, // строка 3
    C10, D17, D18, // строка 4
    C13, C11, C12, // строка 5
};

// Входы с подтяжкой вверх — как в стоке (sub_08c60 ставит всем CFG=0).
// ChibiOS по умолчанию оставляет пины без подтяжки, и свободные входы «плавают».
static const pin_t pullup_pins[] = {
    A3,  B0,  B1,  B3,  C15, D3,  D4,  // не используются (B3 = P1.3 — кандидат на BOOT)
    A10, B2,                           // входы пробуждения неизвестного назначения
    B16, B17,                          // статус зарядки, детект VBUS
    A14, A15,                          // I²C счётчика заряда (SCL/SDA)
    B6,  B7,                           // UART2 к радиомодулю (RX/TX)
    A12, A13, B9, B10, B11,            // SPI1 флеш-памяти: SCK, CS#2, CS#1, MISO, MOSI — CS не должны плавать
#ifndef QUANTUM_PAINTER_ENABLE
    B8,  D14, A17,                     // дисплей: CS, D/C, RESET (без QP никто их не ведёт)
#endif
};

/* --------------------------------------------------------------------------
 * PFPA — переназначение пинов периферии (значения из стока, sub_00cc4)
 * ------------------------------------------------------------------------ */

static void pfpa_init(void) {
    // SPI: MISO0=P3.1 MOSI0=P3.2 SCK0=P3.0 SEL0=P1.8 | MISO1=P1.10 MOSI1=P1.11 SCK1=P0.12 SEL1=P0.13
    SN_PFPA->SPI = 0x0000F5BF;
    // UART2: TX=P1.7 RX=P1.6 (радиомодуль)
    SN_PFPA->UART = 0x00000F00;
    // Таймеры ШИМ подсветки (CT16B0/1/2) — в ledscan_start()
}

#ifdef QUANTUM_PAINTER_ENABLE
/* --------------------------------------------------------------------------
 * Дисплей
 * ------------------------------------------------------------------------ */

static painter_device_t lcd;

// Инициализация панели как в стоке (sub_10598), формат QP:
// команда, задержка (мс), число байт данных, данные...
// Аппаратный сброс уже сделан в lcd_spi_comms_init(). Отличия от штатной
// qp_st7789_init(): нет SWRESET, после SLPOUT ждём 150 мс (QP — 5 мс), и вся
// подстройка питания/гаммы идёт до DISPON.
static const uint8_t lcd_stock_init[] = {
    0x11, 150, 0,                             // SLPOUT
    0x3A, 0, 1, 0x05,                         // COLMOD: RGB565
    0xB2, 0, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33, // PORCTRL
    0xB7, 0, 1, 0x17,                         // GCTRL
    0xBB, 0, 1, 0x35,                         // VCOMS
    0xC0, 0, 1, 0x2C,                         // LCMCTRL
    0xC2, 0, 1, 0x01,                         // VDVVRHEN
    0xC3, 0, 1, 0x13,                         // VRHS
    0xC4, 0, 1, 0x20,                         // VDVS
    0xC6, 0, 1, 0x18,                         // FRCTRL2
    0xD0, 0, 2, 0xA7, 0xA1,                   // PWCTRL1 (вариант 1)
    0xD0, 0, 2, 0xA4, 0xA1,                   // PWCTRL1
    0xD6, 0, 1, 0xA1,
    0xE0, 0, 14, 0xF0, 0x00, 0x04, 0x04, 0x04, 0x05, 0x29, 0x33, 0x3E, 0x38, 0x12, 0x12, 0x28, 0x30, // PVGAMCTRL
    0xE1, 0, 14, 0xF0, 0x07, 0x0A, 0x0D, 0x0B, 0x07, 0x28, 0x33, 0x3E, 0x36, 0x14, 0x14, 0x29, 0x32, // NVGAMCTRL
    0x21, 0, 0,                               // INVON — в стоке инверсия включена
};

// Заменяет слабую qp_st7789_init() из QP: последовательность стока вместо штатной
bool qp_st7789_init(painter_device_t device, painter_rotation_t rotation) {
    static const uint8_t madctl[] = {
        [QP_ROTATION_0]   = 0x00,
        [QP_ROTATION_90]  = 0x60, // MX | MV — как в стоке
        [QP_ROTATION_180] = 0xC0, // MX | MY
        [QP_ROTATION_270] = 0xA0, // MV | MY
    };
    if (!qp_comms_bulk_command_sequence(device, lcd_stock_init, sizeof(lcd_stock_init))) {
        return false;
    }
    if (!qp_comms_command_databyte(device, 0x36, madctl[rotation])) { // MADCTL
        return false;
    }
    qp_comms_command(device, 0x13); // NORON
    qp_comms_command(device, 0x29); // DISPON
    wait_ms(20);
    return true;
}

static bool lcd_init_ok; // результат qp_init (для отладки)

static void lcd_init(void) {
    lcd = qp_st7789_make_spi_device(LCD_NATIVE_WIDTH, LCD_NATIVE_HEIGHT, LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_SPI_DIVISOR, LCD_SPI_MODE);

    // Свой слой связи на прямых регистрах SPI0 вместо spi_master (см. lcd_spi.c)
    ((painter_driver_t *)lcd)->comms_vtable = &lcd_spi_comms_vtable.base;

    // QP_ROTATION_270 = MADCTL 0xA0 (MV | MY). С 0x60 (QP_ROTATION_90) картинка на этой панели перевёрнута на 180°
    lcd_init_ok = qp_init(lcd, QP_ROTATION_270);
    qp_set_viewport_offsets(lcd, LCD_OFFSET_X, LCD_OFFSET_Y);

    qp_power(lcd, true);
    ui_init(lcd); // интерфейс: главный экран, меню (ui.c)

    gpio_write_pin_high(LCD_BL_PIN);
}

void type84_lcd_toast(const char *msg) {
    ui_toast(msg);
}

// Сон панели как в стоке (sub_10882): SLPIN, затем подсветка off. Содержимое
// GRAM сохраняется — после SLPOUT картинка та же, перерисовка не нужна.
void type84_lcd_sleep(bool sleep) {
    if (sleep) {
        gpio_write_pin_low(LCD_BL_PIN);
        qp_comms_start(lcd);
        qp_comms_command(lcd, 0x10); // SLPIN
        qp_comms_stop(lcd);
        wait_ms(5);
    } else {
        qp_comms_start(lcd);
        qp_comms_command(lcd, 0x11); // SLPOUT
        qp_comms_stop(lcd);
        wait_ms(120);
        gpio_write_pin_high(LCD_BL_PIN);
    }
}

// Для тестовых раскладок (keymaps/lcdtest)
painter_device_t type84_lcd(void) {
    return lcd;
}

void type84_lcd_redraw(void) {
    ui_redraw();
}

#else
void type84_lcd_toast(const char *msg) {}
void type84_lcd_sleep(bool sleep) {
    if (sleep) {
        gpio_write_pin_low(LCD_BL_PIN);
    }
}
#endif // QUANTUM_PAINTER_ENABLE

/* --------------------------------------------------------------------------
 * Хуки QMK
 * ------------------------------------------------------------------------ */

void keyboard_pre_init_kb(void) {
    // Как сток (sub_00994): отключить SWD. Один из выводов SWD — P3.6, это канал B
    // крутилки на месте PrtSc; пока SWD включён, P3.6 всегда читается как 0.
    // После сброса SWD снова включён до этой строки — отладчик можно подключать
    // «под сбросом» (nRESET = P3.7).
    SN_SYS0->SWDCTRL |= 1UL; // SWDDIS

    pfpa_init();

    for (uint8_t i = 0; i < ARRAY_SIZE(pullup_pins); i++) {
        gpio_set_pin_input_high(pullup_pins[i]);
    }

    for (uint8_t i = 0; i < ARRAY_SIZE(led_row_pins); i++) {
        gpio_set_pin_output(led_row_pins[i]);
        gpio_write_pin_low(led_row_pins[i]);
    }

    gpio_set_pin_output(LCD_BL_PIN);
    gpio_write_pin_low(LCD_BL_PIN); // подсветку включаем после инициализации, чтобы не было мусора

    keyboard_pre_init_user();
}

/* --------------------------------------------------------------------------
 * Цвета подсветки: готовые цвета, второй цвет двухцветных эффектов, цвет Fn-слоя.
 * ------------------------------------------------------------------------ */

const type84_color_t type84_colors[] = {
    {"RED", 0, 255},    {"ORANGE", 18, 255}, {"AMBER", 28, 255},   {"YELLOW", 43, 255}, {"GREEN", 85, 255}, {"CYAN", 128, 255},
    {"BLUE", 170, 255}, {"PURPLE", 191, 255}, {"PINK", 220, 200},  {"WHITE", 0, 0},     {"BLACK", 0, 1},
};
const uint8_t type84_colors_count = ARRAY_SIZE(type84_colors);

#define COLOR_BLUE 6
#define COLOR_WHITE 9

static uint8_t color2_idx = COLOR_BLUE, fn_color_idx = COLOR_WHITE;

int8_t type84_color_find(uint8_t h, uint8_t sat) {
    for (uint8_t i = 0; i < ARRAY_SIZE(type84_colors); i++) {
        const type84_color_t *c = &type84_colors[i];
        if (c->s == 0 ? sat == 0 : (c->h == h && c->s == sat)) return i;
    }
    return -1;
}

static void colors_save(void) {
    kb_settings()->color2   = color2_idx;
    kb_settings()->fn_color = fn_color_idx;
    kb_settings_save();
}

static void colors_load(void) {
    color2_idx   = kb_settings()->color2;
    fn_color_idx = kb_settings()->fn_color;
    if (color2_idx >= ARRAY_SIZE(type84_colors)) color2_idx = COLOR_BLUE;
    if (fn_color_idx >= ARRAY_SIZE(type84_colors)) fn_color_idx = COLOR_WHITE;
}

static hsv_t preset_hsv(uint8_t i) {
    hsv_t c = {type84_colors[i].h, type84_colors[i].s, 255};
    return c;
}

hsv_t type84_color2(void) {
    return preset_hsv(color2_idx);
}
uint8_t type84_color2_index(void) {
    return color2_idx;
}
void type84_color2_set_index(uint8_t i) {
    color2_idx = i % ARRAY_SIZE(type84_colors);
    colors_save();
}
hsv_t type84_fn_color(void) {
    return preset_hsv(fn_color_idx);
}
uint8_t type84_fn_color_index(void) {
    return fn_color_idx;
}
void type84_fn_color_set_index(uint8_t i) {
    fn_color_idx = i % ARRAY_SIZE(type84_colors);
    colors_save();
}

// «Чёрный» из меню цветов: H=0, S=1 (в QMK нет цвета с нулевой яркостью отдельно от общей)
bool type84_is_black(hsv_t c) {
    return c.h == 0 && c.s == 1;
}

// Встроенные одноцветные эффекты не знают про «чёрный» — гасим подсветку сами.
// Двухцветные (rgb_matrix_kb.inc) и радужные эффекты обрабатывают цвет сами / не используют его.
// Подсветка Fn-слоя: пока Fn зажат, горят только клавиши, на которые в этом слое что-то
// назначено (и сама Fn), цветом FN COLOR; остальные гаснут. Карта — из раскладки на лету.
static bool fn_layer_overlay(uint8_t led_min, uint8_t led_max) {
    uint8_t layer = get_highest_layer(layer_state);
    if (layer == 0) return false;

    uint8_t v = rgb_matrix_get_val();
    if (v < 100) v = 100; // видно даже при малой яркости
    hsv_t c   = type84_fn_color(); // один цвет: меню LIGHTING > FN COLOR
    rgb_t fn  = {0, 0, 0};
    if (!type84_is_black(c)) {
        c.v = v;
        fn  = hsv_to_rgb(c);
    }
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint8_t i = g_led_config.matrix_co[row][col];
            if (i == NO_LED || i < led_min || i >= led_max) continue;
            uint16_t kc = keymap_key_to_keycode(layer, (keypos_t){.row = row, .col = col});
            if (kc == KC_TRNS || kc == KC_NO) {
                // сама клавиша, держащая слой (MO(n) в базовом слое), светится тоже
                uint16_t base = keymap_key_to_keycode(0, (keypos_t){.row = row, .col = col});
                if (!(IS_QK_MOMENTARY(base) && QK_MOMENTARY_GET_LAYER(base) == layer)) {
                    rgb_matrix_set_color(i, 0, 0, 0);
                    continue;
                }
            }
            rgb_matrix_set_color(i, fn.r, fn.g, fn.b);
        }
    }
    return true;
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) return false;
    if (fn_layer_overlay(led_min, led_max)) return true;
    if (snaptap_indicators(led_min, led_max)) return true;
    if (!type84_is_black(rgb_matrix_get_hsv())) return true;
    uint8_t m = rgb_matrix_get_mode();
    if (m >= RGB_MATRIX_CUSTOM_TWO_TONE) return true; // свои эффекты — после встроенных
    switch (m) {
        case RGB_MATRIX_CYCLE_ALL:
        case RGB_MATRIX_CYCLE_LEFT_RIGHT:
        case RGB_MATRIX_CYCLE_UP_DOWN:
        case RGB_MATRIX_RAINBOW_MOVING_CHEVRON:
        case RGB_MATRIX_CYCLE_OUT_IN:
        case RGB_MATRIX_CYCLE_PINWHEEL:
        case RGB_MATRIX_CYCLE_SPIRAL:
        case RGB_MATRIX_RAINBOW_BEACON:
        case RGB_MATRIX_DIGITAL_RAIN:
        case RGB_MATRIX_TYPING_HEATMAP:
            return true;
    }
    for (uint8_t i = led_min; i < led_max; i++) rgb_matrix_set_color(i, 0, 0, 0);
    return true;
}

void keyboard_post_init_kb(void) {
    colors_load();
    strip_init();
    snaptap_init();
#ifdef QUANTUM_PAINTER_ENABLE
    safety_wdt_feed();
    lcd_init();
    safety_wdt_feed();
#endif
    keyboard_post_init_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
#ifdef QUANTUM_PAINTER_ENABLE
    if (!ui_process(keycode, record)) {
        return false;
    }
#endif
    if (!process_record_wireless(keycode, record->event.pressed)) {
        return false;
    }
    if (!process_record_user(keycode, record)) {
        return false;
    }
    return snaptap_process(keycode, record);
}

/* --------------------------------------------------------------------------
 * Заряд по USB: HID-отчёт Battery Strength (патч ядра qmk/patches/hid-battery.patch).
 * Linux показывает его как батарею устройства (upower, панель).
 * ------------------------------------------------------------------------ */

#ifdef HID_BATTERY_ENABLE
static void usb_battery_task(void) {
    static uint8_t  sent = 0xFF;
    static uint32_t active_since, sent_at;
    static bool     was_active;

    if (!usb_connected_state()) {
        was_active = false;
        return;
    }
    if (!was_active) {
        was_active   = true;
        active_since = timer_read32();
        sent         = 0xFF;
    }
    // Первый отчёт — сразу: он же ответ на GET_REPORT, которым ядро Linux читает
    // заряд при подключении. Повтор через 1 с — когда хост точно слушает.
    static bool sent_chg, resent;
    uint8_t     pct = battery_get_percent();
    bool        chg = battery_charging();
    if (sent != 0xFF && !resent && timer_elapsed32(active_since) > 1000) {
        resent = true;
        sent   = 0xFE; // форсировать отправку
    }
    if (sent == 0xFF) resent = false;
    if (pct == sent && chg == sent_chg && timer_elapsed32(sent_at) < 60000) return;

    struct {
        uint8_t report_id;
        uint8_t level;
        uint8_t charging; // бит 0
    } PACKED report = {REPORT_ID_BATTERY, pct, chg};
    send_report(USB_ENDPOINT_IN_SHARED, &report, sizeof(report));
    sent     = pct;
    sent_chg = chg;
    sent_at  = timer_read32();
}
#endif

void housekeeping_task_kb(void) {
    safety_wdt_feed();
    snaptap_task();
    strip_task();
#ifdef HID_BATTERY_ENABLE
    usb_battery_task();
#endif
#ifdef QUANTUM_PAINTER_ENABLE
    ui_task();
#endif
    housekeeping_task_user();
}

static bool lcd_asleep = false;

void suspend_power_down_kb(void) {
    safety_wdt_feed(); // в режиме сна QMK крутится в отдельном цикле без housekeeping
    if (!lcd_asleep) { // вызывается на каждом проходе цикла сна — гасим один раз
        lcd_asleep = true;
        gpio_write_pin_low(LCD_BL_PIN);
#ifdef QUANTUM_PAINTER_ENABLE
        qp_power(lcd, false);
#endif
    }
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    lcd_asleep = false;
#ifdef QUANTUM_PAINTER_ENABLE
    qp_power(lcd, true);
    gpio_write_pin_high(LCD_BL_PIN);
#endif
    suspend_wakeup_init_user();
}
