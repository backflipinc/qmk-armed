// Счётчик заряда CW2017 + статус питания (battery_cw2017.c)
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool     battery_on_usb_power(void); // P1.17 LOW
bool     battery_charging(void);     // USB, P1.16 LOW и < 100 %
uint16_t battery_millivolts(void);
