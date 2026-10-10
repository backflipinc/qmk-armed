// Счётчик заряда CellWise CW2017 (совместим с CW2015) на программном I²C.
// Драйвер батареи QMK (BATTERY_DRIVER = custom): quantum/battery/battery.c
// раз в BATTERY_SAMPLE_INTERVAL вызывает battery_driver_sample_percent().
//
// Восстановлено из стока v1.32 (sub_13f38..sub_142e0, I²C sub_07ed4/sub_07f50):
//   адрес 0x63 (0xC6/0xC7), SCL P0.14, SDA P0.15;
//   рег. 0x00 — версия, 0x02-0x03 — напряжение (LSB = 5/16 мВ),
//   0x04-0x05 — заряд (процент = raw16 * 100 / 0x6000, т.е. 96 % чипа = 100 %),
//   0x06 — температура, 0x08 — режим (0x00 работа, 0x30/0xF0 сон/сброс),
//   0x0B бит 7 — профиль загружен, 0x10-0x5F — профиль батареи (80 байт).

#include "quantum.h"
#include "battery_driver.h"
#include "battery_cw2017.h"

#define CW_ADDR_W 0xC6
#define CW_ADDR_R 0xC7

#define CW_REG_VERSION 0x00
#define CW_REG_VCELL 0x02
#define CW_REG_SOC 0x04
#define CW_REG_MODE 0x08
#define CW_REG_CONFIG 0x0B
#define CW_REG_PROFILE 0x10
#define CW_PROFILE_LEN 80
#define CW_CONFIG_UPDATE_FLAG 0x80

#define CW_SCL A14
#define CW_SDA A15

#define CHARGE_PIN B16 // P1.16: HIGH — заряд завершён (сток sub_02494)
#define VBUS_PIN B17   // P1.17: LOW — есть питание USB (сток sub_02494)

void safety_wdt_feed(void);

// Профиль батареи из стока (данные .data @ 0x20000290, распакованы из образа)
static const uint8_t cw_profile[CW_PROFILE_LEN] = {
    0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0, 0xC3, 0xC1, 0xBB, 0x9F, 0xA1, 0xE3, 0xDC,
    0xD4, 0xB3, 0x9B, 0x83, 0x76, 0x57, 0x49, 0x41, 0x3F, 0x3A, 0x35, 0x91, 0x8E, 0xD1, 0xF3, 0xFF,
    0xEF, 0xD4, 0xD0, 0xCD, 0xCB, 0xC7, 0xC5, 0xC0, 0xB6, 0xAD, 0xAB, 0x96, 0x80, 0x6F, 0x68, 0x68,
    0x72, 0x82, 0x95, 0xAA, 0xBE, 0xC6, 0xA3, 0x28, 0x00, 0x00, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18,
};

/* --------------------------------------------------------------------------
 * Программный I²C: «1» — вход с подтяжкой, «0» — выход LOW
 * ------------------------------------------------------------------------ */

// Не wait_us(): с периодическим тиком 10 кГц (chconf.h) он спит минимум 100 мкс
static inline void i2c_delay(void) {
    for (volatile uint8_t i = 0; i < 30; i++) { // ~5 мкс при 48 МГц -> ~100 кГц
    }
}

static inline void line_release(pin_t pin) {
    gpio_set_pin_input_high(pin);
}

static inline void line_low(pin_t pin) {
    gpio_write_pin_low(pin);
    gpio_set_pin_output(pin);
}

static void i2c_start(void) {
    line_release(CW_SDA);
    line_release(CW_SCL);
    i2c_delay();
    line_low(CW_SDA);
    i2c_delay();
    line_low(CW_SCL);
}

static void i2c_stop(void) {
    line_low(CW_SDA);
    i2c_delay();
    line_release(CW_SCL);
    i2c_delay();
    line_release(CW_SDA);
    i2c_delay();
}

static bool i2c_write_byte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 0x80) {
            line_release(CW_SDA);
        } else {
            line_low(CW_SDA);
        }
        b <<= 1;
        i2c_delay();
        line_release(CW_SCL);
        i2c_delay();
        line_low(CW_SCL);
    }
    line_release(CW_SDA);
    i2c_delay();
    line_release(CW_SCL);
    i2c_delay();
    bool ack = !gpio_read_pin(CW_SDA);
    line_low(CW_SCL);
    return ack;
}

static uint8_t i2c_read_byte(bool ack) {
    uint8_t b = 0;
    line_release(CW_SDA);
    for (uint8_t i = 0; i < 8; i++) {
        i2c_delay();
        line_release(CW_SCL);
        i2c_delay();
        b = (b << 1) | (gpio_read_pin(CW_SDA) ? 1 : 0);
        line_low(CW_SCL);
    }
    if (ack) {
        line_low(CW_SDA);
    } else {
        line_release(CW_SDA);
    }
    i2c_delay();
    line_release(CW_SCL);
    i2c_delay();
    line_low(CW_SCL);
    line_release(CW_SDA);
    return b;
}

static bool cw_read(uint8_t reg, uint8_t *val) {
    i2c_start();
    bool ok = i2c_write_byte(CW_ADDR_W) && i2c_write_byte(reg);
    if (ok) {
        i2c_start(); // повторный старт
        ok = i2c_write_byte(CW_ADDR_R);
        if (ok) *val = i2c_read_byte(false);
    }
    i2c_stop();
    return ok;
}

static bool cw_write(uint8_t reg, uint8_t val) {
    i2c_start();
    bool ok = i2c_write_byte(CW_ADDR_W) && i2c_write_byte(reg) && i2c_write_byte(val);
    i2c_stop();
    return ok;
}

/* --------------------------------------------------------------------------
 * Инициализация чипа (сток sub_141fe / sub_140f6 / sub_140a8)
 * ------------------------------------------------------------------------ */

static bool    cw_ok;
static uint8_t last_percent = 100;
static uint16_t last_mv;

static bool cw_load_profile(void) {
    dprintf("BAT: loading profile\n");
    for (uint8_t i = 0; i < CW_PROFILE_LEN; i++) {
        if (!cw_write(CW_REG_PROFILE + i, cw_profile[i])) return false;
    }
    uint8_t cfg;
    if (!cw_read(CW_REG_CONFIG, &cfg) || !cw_write(CW_REG_CONFIG, cfg | CW_CONFIG_UPDATE_FLAG)) return false;

    // перезапуск расчёта: 0xF0 -> 0x30 -> 0x00 с паузами по 10 мс
    static const uint8_t seq[] = {0xF0, 0x30, 0x00};
    for (uint8_t i = 0; i < sizeof(seq); i++) {
        if (!cw_write(CW_REG_MODE, seq[i])) return false;
        wait_ms(10);
    }
    // дождаться, пока чип посчитает заряд (сток: до 30 попыток по 10 мс)
    for (uint8_t i = 0; i < 30; i++) {
        uint8_t soc;
        safety_wdt_feed();
        if (cw_read(CW_REG_SOC, &soc) && soc <= 100) break;
        wait_ms(10);
    }
    return true;
}

static bool cw_init(void) {
    uint8_t mode, cfg;
    if (!cw_read(CW_REG_MODE, &mode) || !cw_read(CW_REG_CONFIG, &cfg)) return false;

    bool need = mode != 0 || !(cfg & CW_CONFIG_UPDATE_FLAG);
    for (uint8_t i = 0; !need && i < CW_PROFILE_LEN; i++) {
        uint8_t v;
        if (!cw_read(CW_REG_PROFILE + i, &v)) return false;
        need = v != cw_profile[i];
    }
    return need ? cw_load_profile() : true;
}

/* --------------------------------------------------------------------------
 * Драйвер батареи QMK
 * ------------------------------------------------------------------------ */

void battery_driver_init(void) {
    gpio_set_pin_input_high(CHARGE_PIN);
    gpio_set_pin_input_high(VBUS_PIN);
    line_release(CW_SCL);
    line_release(CW_SDA);
    cw_ok = cw_init();
    dprintf("BAT: init %s\n", cw_ok ? "ok" : "FAIL");
}

uint8_t battery_driver_sample_percent(void) {
    static uint8_t bad;
    uint8_t        hi, lo;

    if (!cw_ok) {
        cw_ok = cw_init();
        if (!cw_ok) return last_percent;
    }

    if (cw_read(CW_REG_VCELL, &hi) && cw_read(CW_REG_VCELL + 1, &lo)) {
        last_mv = (uint16_t)((((uint32_t)hi << 8 | lo) * 5) >> 4);
    }

    if (!cw_read(CW_REG_SOC, &hi) || !cw_read(CW_REG_SOC + 1, &lo)) return last_percent;
    if (hi > 100) {
        // мусор от чипа: после 5 подряд — перезапуск (сток sub_14226)
        if (++bad > 5) {
            bad = 0;
            cw_write(CW_REG_MODE, 0x30);
            cw_ok = false;
        }
        return last_percent;
    }
    bad = 0;

    uint32_t pct = ((uint32_t)hi << 8 | lo) * 100 / 0x6000;
    last_percent = pct > 100 ? 100 : (uint8_t)pct;
    dprintf("BAT: %u%% %u mV chg=%u vbus=%u\n", last_percent, last_mv, battery_charging(), battery_on_usb_power());
    return last_percent;
}

/* --------------------------------------------------------------------------
 * Состояние питания
 * ------------------------------------------------------------------------ */

bool battery_on_usb_power(void) {
    return !gpio_read_pin(VBUS_PIN);
}

// Как в стоке: от USB, P1.16 LOW и счётчик ещё не 100 % — идёт заряд
bool battery_charging(void) {
    return battery_on_usb_power() && !gpio_read_pin(CHARGE_PIN) && last_percent < 100;
}

uint16_t battery_millivolts(void) {
    return last_mv;
}
