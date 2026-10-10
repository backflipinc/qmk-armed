#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

enum snaptap_modes { SNAPTAP_LAST, SNAPTAP_NEUTRAL, SNAPTAP_MODES };

void        snaptap_init(void);
void        snaptap_task(void);
bool        snaptap_active(void);
uint8_t     snaptap_mode(void);
const char *snaptap_mode_name(uint8_t m);
void        snaptap_set_mode(uint8_t m);
const char *snaptap_keys_name(void);
bool        snaptap_process(uint16_t keycode, keyrecord_t *record);
bool        snaptap_indicators(uint8_t led_min, uint8_t led_max);
