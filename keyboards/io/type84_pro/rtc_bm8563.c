// RTC BM8563 (PCF8563-compatible) on the battery gauge's I2C bus, address 0xA2.
// Stock v1.32: reads 7 BCD registers from 0x02 (sub_07cbc), sets them from the configurator (cmd 0x34).

#include "quantum.h"

#ifdef ARMED_ENABLE

#    include "armed.h"
#    include "battery_cw2017.h"

#    define RTC_ADDR 0xA2
#    define RTC_REG_CTRL1 0x00
#    define RTC_REG_SECONDS 0x02
#    define RTC_VL 0x80 // seconds bit 7: the oscillator stopped, the time is not valid

static uint8_t bcd(uint8_t v) {
    return (v / 10) << 4 | v % 10;
}

static uint8_t unbcd(uint8_t v) {
    return (v >> 4) * 10 + (v & 0x0F);
}

bool armed_clock_get_kb(armed_time_t *t) {
    uint8_t r[7];
    if (!kb_i2c_read(RTC_ADDR, RTC_REG_SECONDS, r, sizeof(r)) || r[0] & RTC_VL) return false;
    t->second = unbcd(r[0] & 0x7F);
    t->minute = unbcd(r[1] & 0x7F);
    t->hour   = unbcd(r[2] & 0x3F);
    t->day    = unbcd(r[3] & 0x3F);
    t->month  = unbcd(r[5] & 0x1F);
    t->year   = 2000 + unbcd(r[6]);
    return t->month >= 1 && t->month <= 12 && t->day >= 1 && t->hour < 24 && t->minute < 60 && t->second < 60;
}

bool armed_clock_set_kb(const armed_time_t *t) {
    if (t->year < 2000 || t->year > 2099) return false;
    uint8_t ctrl = 0;
    uint8_t r[7] = {bcd(t->second), bcd(t->minute), bcd(t->hour), bcd(t->day), (t->weekday + 1) % 7, bcd(t->month), bcd(t->year - 2000)};
    return kb_i2c_write(RTC_ADDR, RTC_REG_CTRL1, &ctrl, 1) && kb_i2c_write(RTC_ADDR, RTC_REG_SECONDS, r, sizeof(r));
}

#endif
