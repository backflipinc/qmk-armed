// Матрица клавиш + RGB-подсветка: один движок в прерывании таймера, как в стоке.
//
// Колонки клавиш и колонки светодиодов — одни и те же 16 пинов, поэтому скан
// и ШИМ чередуются. Сток: ISR CT16B3 sub_07b80, ШИМ sub_07800, строки sub_078fe,
// таймеры sub_08d48 / sub_08cd8 / sub_08e20, скан колонки sub_04ad6.
//
// Таймеры CT16B0/1/2/3: предделитель 24 -> 2 МГц. CT16B3 MR0 = 255 -> прерывание
// каждые 128 мкс (фаза). 21 фаза = 3 цвета x (6 LED-строк + 1 пустая), кадр 2,7 мс.
// На каждой фазе:
//   1) гасим 18 пинов строк, стоп ШИМ-таймеров, пины колонок -> GPIO;
//   2) сканируем ОДНУ колонку клавиш (16 фаз = полный скан, ~2 мс);
//   3) грузим скважности колонок для следующей строки/цвета в MR,
//      возвращаем пины колонок ШИМу, зажигаем пин строки (HIGH), старт таймеров.
// Светодиод: анод — пин строки (HIGH), катод — колонка; ШИМ держит колонку LOW,
// пока TC < MR (режим ШИМ 0) -> скважность = яркость.
//
// Регистры MR и PWMCTRL на SN32F290 пишутся с ключом 0x5A в старшем байте.

#include "quantum.h"
#include "matrix.h"
#include "ledscan.h"

#define CT16B0_BASE 0x40000000UL
#define CT16B1_BASE 0x40002000UL
#define CT16B2_BASE 0x40004000UL
#define CT16B3_BASE 0x40006000UL
#define REG(base, off) (*(volatile uint32_t *)((base) + (off)))

#define CT_TMRCTRL 0x00
#define CT_TC 0x04
#define CT_PRE 0x08
#define CT_MCTRL 0x14
#define CT_MR0 0x20
#define CT_PWMCTRL 0x98
#define CT1_PWMENB 0xA0
#define CT1_PWMIOENB 0xA4
#define CT_IC 0xAC

#define KEY 0x5A000000UL

// P2 описан в заголовке своим типом; раскладка регистров (MODE/BSET/BCLR) та же, что у P0/P1/P3
#define SN_GPIO2_P ((SN_GPIO1_Type *)SN_GPIO2)
#define TMR_CEN 1UL
#define TMR_CRST 2UL

#define PHASES 21
#define LED_ROWS 6

// SYS1->AHBCLKEN: CT16B0..2 (биты 5-7), CT16B3 (бит 8)
#define AHBCLKEN_CT16B0_3 ((1UL << 5) | (1UL << 6) | (1UL << 7) | (1UL << 8))

#ifndef LEDSCAN_MAX_DUTY
#    define LEDSCAN_MAX_DUTY 240 // из 256 тиков фазы: запас на работу ISR в начале фазы
#endif

/* --------------------------------------------------------------------------
 * Пины
 * ------------------------------------------------------------------------ */

// Колонка -> регистр MR её ШИМ-канала (сток sub_07800, PFPA sub_00cc4)
static const struct {
    uint32_t base;
    uint8_t  mr;
} col_pwm[MATRIX_COLS] = {
    {CT16B1_BASE, 0x30}, // C0  P0.4  CT16B1 PWM4
    {CT16B1_BASE, 0x34}, // C1  P0.5  CT16B1 PWM5
    {CT16B1_BASE, 0x40}, // C2  P2.0  CT16B1 PWM8
    {CT16B2_BASE, 0x24}, // C3  P2.1  CT16B2 PWM1
    {CT16B2_BASE, 0x28}, // C4  P2.2  CT16B2 PWM2
    {CT16B2_BASE, 0x2C}, // C5  P2.3  CT16B2 PWM3
    {CT16B1_BASE, 0x38}, // C6  P0.6  CT16B1 PWM6
    {CT16B1_BASE, 0x3C}, // C7  P0.7  CT16B1 PWM7
    {CT16B1_BASE, 0x44}, // C8  P2.4  CT16B1 PWM9
    {CT16B2_BASE, 0x20}, // C9  P2.5  CT16B2 PWM0
    {CT16B0_BASE, 0x20}, // C10 P2.6  CT16B0 PWM0
    {CT16B1_BASE, 0x48}, // C11 P2.7  CT16B1 PWM10
    {CT16B0_BASE, 0x2C}, // C12 P2.14 CT16B0 PWM3
    {CT16B0_BASE, 0x28}, // C13 P2.8  CT16B0 PWM2
    {CT16B0_BASE, 0x24}, // C14 P2.9  CT16B0 PWM1
    {CT16B1_BASE, 0x20}, // C15 P0.0  CT16B1 PWM0
};

// Колонки на портах (как в safety.c / wireless.c)
#define COLS_P0 0x00F1UL // P0.0, P0.4-P0.7
#define COLS_P2 0x43FFUL // P2.0-P2.9, P2.14

static const pin_t col_pins[MATRIX_COLS] = {A4, A5, C0, C1, C2, C3, A6, A7, C4, C5, C6, C7, C14, C8, C9, A0};

// Строки клавиш: вход без подтяжки (внешние резисторы к земле), HIGH = нажато
static const pin_t row_pins[MATRIX_ROWS] = {B14, B15, B19, D19, A19, A18};

// Пины LED-строк: [цвет-буфер][строка]. Буфер 0/1/2 — порядок стока (0x2000217C/22A2/23C8),
// по документации R/G/B; если цвета перепутаны — поменять LEDSCAN_COLOR_ORDER.
static const struct {
    SN_GPIO1_Type *port;
    uint32_t       bit;
} led_row_pin[3][LED_ROWS] = {
    {{SN_GPIO1, 1UL << 5}, {SN_GPIO3, 1UL << 8}, {SN_GPIO3, 1UL << 11}, {SN_GPIO3, 1UL << 16}, {SN_GPIO2_P, 1UL << 10}, {SN_GPIO2_P, 1UL << 13}},
    {{SN_GPIO0, 1UL << 11}, {SN_GPIO0, 1UL << 8}, {SN_GPIO3, 1UL << 9}, {SN_GPIO3, 1UL << 12}, {SN_GPIO3, 1UL << 17}, {SN_GPIO2_P, 1UL << 11}},
    {{SN_GPIO1, 1UL << 4}, {SN_GPIO0, 1UL << 9}, {SN_GPIO3, 1UL << 10}, {SN_GPIO3, 1UL << 13}, {SN_GPIO3, 1UL << 18}, {SN_GPIO2_P, 1UL << 12}},
};

// Все 18 пинов строк по портам (сток sub_07b34)
#define LEDROWS_P0 ((1UL << 8) | (1UL << 9) | (1UL << 11))
#define LEDROWS_P1 ((1UL << 4) | (1UL << 5))
#define LEDROWS_P2 ((1UL << 10) | (1UL << 11) | (1UL << 12) | (1UL << 13))
#define LEDROWS_P3 ((0x3FUL << 8) | (7UL << 16))

/* --------------------------------------------------------------------------
 * Состояние
 * ------------------------------------------------------------------------ */

// Скважность [цвет][строка][колонка], 0..LEDSCAN_MAX_DUTY.
// duty — показывает прерывание; duty_back — сюда пишут эффекты, в duty копируется
// готовый кадр целиком (ledscan_update_lit). Без этого эффект и наложения
// (подсветка Fn-слоя) рисуются поверх друг друга прямо на клавиатуре — мерцание.
static uint8_t duty[3][LED_ROWS][MATRIX_COLS];
static uint8_t duty_back[3][LED_ROWS][MATRIX_COLS];
static uint8_t row_lit[3][LED_ROWS]; // в строке есть хоть один ненулевой канал

static volatile matrix_row_t raw_cols[MATRIX_COLS]; // бит r = строка r нажата
static uint8_t               phase;
static volatile uint32_t     isr_count;
static uint8_t               scan_col;
static bool                  running;

/* --------------------------------------------------------------------------
 * Низкий уровень
 * ------------------------------------------------------------------------ */

static inline void leds_rows_off(void) {
    SN_GPIO0->BCLR   = LEDROWS_P0;
    SN_GPIO1->BCLR   = LEDROWS_P1;
    SN_GPIO2_P->BCLR = LEDROWS_P2;
    SN_GPIO3->BCLR   = LEDROWS_P3;
}

static inline void pwm_timers_stop(void) {
    REG(CT16B0_BASE, CT_TMRCTRL) = 0;
    REG(CT16B1_BASE, CT_TMRCTRL) = 0;
    REG(CT16B2_BASE, CT_TMRCTRL) = 0;
    REG(CT16B0_BASE, CT_TC)      = 0;
    REG(CT16B1_BASE, CT_TC)      = 0;
    REG(CT16B2_BASE, CT_TC)      = 0;
}

// Пины колонок отдать GPIO (сток sub_08e20)
static inline void pwm_io_off(void) {
    REG(CT16B1_BASE, CT1_PWMIOENB) = 0;
    REG(CT16B0_BASE, CT_PWMCTRL)   = KEY;
    REG(CT16B2_BASE, CT_PWMCTRL)   = KEY;
}

// Пины колонок вернуть ШИМу (сток sub_08cd8)
static inline void pwm_io_on(void) {
    REG(CT16B0_BASE, CT_PWMCTRL)   = KEY | 0x00F0000FUL; // PWM0-3 EN + IOEN
    REG(CT16B2_BASE, CT_PWMCTRL)   = KEY | 0x00F0000FUL;
    REG(CT16B1_BASE, CT1_PWMIOENB) = 0x7F1; // каналы 0, 4-10
}

static inline void pwm_load(uint8_t color, uint8_t row) {
    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        REG(col_pwm[c].base, col_pwm[c].mr) = KEY | duty[color][row][c];
    }
}

static inline void pwm_zero(void) {
    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        REG(col_pwm[c].base, col_pwm[c].mr) = KEY;
    }
}

// Сток sub_06f9c(8): ~5 мкс — строки успевают разрядиться через резисторы к земле
// после фазы ШИМ, где колонки стояли в HIGH
static inline void short_delay(void) {
    for (volatile uint8_t i = 0; i < 30; i++) {
    }
}

// Скан одной колонки (сток sub_04ad6): колонки — выходы LOW, выбранная HIGH
static inline matrix_row_t scan_one(uint8_t c) {
    SN_GPIO0->BCLR   = COLS_P0;
    SN_GPIO2_P->BCLR = COLS_P2;
    SN_GPIO0->MODE |= COLS_P0;
    SN_GPIO2_P->MODE |= COLS_P2;
    gpio_write_pin_high(col_pins[c]);
    short_delay();
    matrix_row_t rows = 0;
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (gpio_read_pin(row_pins[r])) rows |= (matrix_row_t)1 << r;
    }
    gpio_write_pin_low(col_pins[c]);
    return rows;
}

OSAL_IRQ_HANDLER(SN32_CT16B3_HANDLER) {
    OSAL_IRQ_PROLOGUE();

    REG(CT16B3_BASE, CT_IC) = 1; // MR0
    isr_count++;

    leds_rows_off();
    pwm_timers_stop();
    pwm_zero();
    pwm_io_off();

    raw_cols[scan_col] = scan_one(scan_col);
    if (++scan_col >= MATRIX_COLS) scan_col = 0;

    phase = (phase + 1) % PHASES;
    uint8_t color = phase / (LED_ROWS + 1);
    uint8_t row   = phase % (LED_ROWS + 1);

    pwm_io_on();
    if (row < LED_ROWS && row_lit[color][row]) {
        pwm_load(color, row);
        led_row_pin[color][row].port->BSET = led_row_pin[color][row].bit;
    }
    REG(CT16B0_BASE, CT_TMRCTRL) = TMR_CEN;
    REG(CT16B1_BASE, CT_TMRCTRL) = TMR_CEN;
    REG(CT16B2_BASE, CT_TMRCTRL) = TMR_CEN;

    OSAL_IRQ_EPILOGUE();
}

/* --------------------------------------------------------------------------
 * Пуск/останов (сток sub_08d48)
 * ------------------------------------------------------------------------ */

static void timer_reset_start(uint32_t base) {
    REG(base, CT_TMRCTRL) = TMR_CRST;
    while (REG(base, CT_TMRCTRL) & TMR_CRST) {
    }
    REG(base, CT_TMRCTRL) = TMR_CEN;
}

#ifdef LEDSCAN_DEBUG
#    define LDBG(...) uprintf(__VA_ARGS__)
#else
#    define LDBG(...)
#endif

void ledscan_start(void) {
    LDBG("LS: start\n");
    SN_SYS1->AHBCLKEN |= AHBCLKEN_CT16B0_3;

    // PFPA: каналы ШИМ на пины колонок (сток sub_00cc4)
    SN_PFPA->CT16B0 = 0x3302;
    SN_PFPA->CT16B1 = 0x0700;
    SN_PFPA->CT16B2 = 0x0001;

    REG(CT16B0_BASE, CT_PRE) = 23;
    REG(CT16B1_BASE, CT_PRE) = 23;
    REG(CT16B2_BASE, CT_PRE) = 23;
    REG(CT16B3_BASE, CT_PRE) = 23;

    leds_rows_off();
    pwm_zero();
    REG(CT16B1_BASE, CT_PWMCTRL) = KEY;          // режимы каналов 0..15 = 0
    REG(CT16B1_BASE, CT1_PWMENB) = KEY | 0x7F1; // каналы 0, 4-10
    pwm_io_on();

    // CT16B3: MR0 = 255, прерывание + сброс по совпадению
    REG(CT16B3_BASE, CT_MR0)   = KEY | 0xFF;
    REG(CT16B3_BASE, CT_MCTRL) = KEY | 3;
    REG(CT16B3_BASE, CT_IC)    = 1;

    LDBG("LS: regs set, starting timers\n");
    timer_reset_start(CT16B0_BASE);
    timer_reset_start(CT16B1_BASE);
    timer_reset_start(CT16B2_BASE);
    timer_reset_start(CT16B3_BASE);
    LDBG("LS: timers running, TC3=%lu MCTRL3=%08lx\n", REG(CT16B3_BASE, CT_TC), REG(CT16B3_BASE, CT_MCTRL));

    running = true;
    nvicEnableVector(CT16B3_IRQn, 3);
    LDBG("LS: irq on\n");
}

#ifdef LEDSCAN_DEBUG
// Отладка: старт через 5 с, затем раз в секунду — число прерываний и состояние CT16B3
void ledscan_debug_task(void) {
    static uint32_t last;
    static uint32_t last_count;
    if (!running && timer_read32() > 5000) {
        ledscan_start();
        last = timer_read32();
    }
    if (running && timer_elapsed32(last) > 1000) {
        uint32_t n = isr_count;
        uprintf("LS: isr/s=%lu TC3=%lu RIS3=%08lx\n", n - last_count, REG(CT16B3_BASE, CT_TC), REG(CT16B3_BASE, 0xA8));
        last_count = n;
        last       = timer_read32();
    }
}
#endif

// Для глубокого сна: всё погасить, колонки — GPIO-выходы LOW
void ledscan_stop(void) {
    if (!running) return;
    nvicDisableVector(CT16B3_IRQn);
    REG(CT16B3_BASE, CT_TMRCTRL) = 0;
    leds_rows_off();
    pwm_timers_stop();
    pwm_io_off();
    SN_GPIO0->BCLR   = COLS_P0;
    SN_GPIO2_P->BCLR = COLS_P2;
    SN_GPIO0->MODE |= COLS_P0;
    SN_GPIO2_P->MODE |= COLS_P2;
    running = false;
}

/* --------------------------------------------------------------------------
 * Матрица QMK (custom_lite)
 * ------------------------------------------------------------------------ */

void matrix_init_custom(void) {
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        gpio_set_pin_input(row_pins[r]);
    }
    // пины LED-строк — выходы LOW
    SN_GPIO0->BCLR = LEDROWS_P0;
    SN_GPIO0->MODE |= LEDROWS_P0;
    SN_GPIO1->BCLR = LEDROWS_P1;
    SN_GPIO1->MODE |= LEDROWS_P1;
    SN_GPIO2_P->BCLR = LEDROWS_P2;
    SN_GPIO2_P->MODE |= LEDROWS_P2;
    SN_GPIO3->BCLR = LEDROWS_P3;
    SN_GPIO3->MODE |= LEDROWS_P3;

#ifndef LEDSCAN_DEBUG
    ledscan_start();
#endif
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    matrix_row_t fresh[MATRIX_ROWS] = {0};
    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        matrix_row_t rows = raw_cols[c];
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            if (rows & ((matrix_row_t)1 << r)) fresh[r] |= (matrix_row_t)1 << c;
        }
    }
    bool changed = false;
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (current_matrix[r] != fresh[r]) {
            current_matrix[r] = fresh[r];
            changed           = true;
        }
    }
    return changed;
}

/* --------------------------------------------------------------------------
 * Драйвер RGB Matrix (RGB_MATRIX_DRIVER = custom)
 * ------------------------------------------------------------------------ */

#ifndef LEDSCAN_COLOR_ORDER
#    define LEDSCAN_COLOR_ORDER 0, 1, 2 // буфер для R, G, B
#endif
static const uint8_t color_buf[3] = {LEDSCAN_COLOR_ORDER};

void ledscan_set(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b) {
    if (row >= LED_ROWS || col >= MATRIX_COLS) return;
    const uint8_t v[3] = {r, g, b};
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t buf          = color_buf[i];
        // округление вверх: ненулевой канал не должен стать 0 (иначе на дне яркости цвет уезжает)
        duty_back[buf][row][col] = ((uint16_t)v[i] * LEDSCAN_MAX_DUTY + 254) / 255;
    }
}

// Светодиоды вне RGB Matrix (полоса под дисплеем, strip.c): сразу в выводимый буфер.
// RGB Matrix эти позиции не трогает, и копирование кадра (ledscan_update_lit) их сохраняет.
void ledscan_set_direct(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b) {
    if (row >= LED_ROWS || col >= MATRIX_COLS) return;
    ledscan_set(row, col, r, g, b);
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t buf          = color_buf[i];
        duty[buf][row][col]  = duty_back[buf][row][col];
        uint8_t lit          = 0;
        for (uint8_t c = 0; c < MATRIX_COLS; c++) lit |= duty[buf][row][c];
        row_lit[buf][row] = lit != 0;
    }
}

void ledscan_update_lit(void) {
    memcpy(duty, duty_back, sizeof(duty));
    for (uint8_t buf = 0; buf < 3; buf++) {
        for (uint8_t row = 0; row < LED_ROWS; row++) {
            uint8_t lit = 0;
            for (uint8_t c = 0; c < MATRIX_COLS; c++) lit |= duty[buf][row][c];
            row_lit[buf][row] = lit != 0;
        }
    }
}

#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix.h"

// индекс LED -> позиция в матрице (строка << 4 | колонка); LED (r,c) стоит под клавишей (r,c)
static uint8_t led_pos[RGB_MATRIX_LED_COUNT];

static void drv_init(void) {
    for (uint8_t mr = 0; mr < MATRIX_ROWS; mr++) {
        for (uint8_t mc = 0; mc < MATRIX_COLS; mc++) {
            uint8_t i = g_led_config.matrix_co[mr][mc];
            if (i < RGB_MATRIX_LED_COUNT) led_pos[i] = (mr << 4) | mc;
        }
    }
}

static void drv_set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= RGB_MATRIX_LED_COUNT) return;
    ledscan_set(led_pos[index] >> 4, led_pos[index] & 0x0F, r, g, b);
}

static void drv_set_color_all(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t mr = 0; mr < MATRIX_ROWS; mr++) {
        for (uint8_t mc = 0; mc < MATRIX_COLS; mc++) {
            if (g_led_config.matrix_co[mr][mc] != NO_LED) ledscan_set(mr, mc, r, g, b);
        }
    }
}

static void drv_flush(void) {
    ledscan_update_lit();
}

const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = drv_init,
    .flush         = drv_flush,
    .set_color     = drv_set_color,
    .set_color_all = drv_set_color_all,
};
#endif
