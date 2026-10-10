// Светодиодная полоса под дисплеем (strip.c)
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STRIP_LEDS 9

enum strip_modes { STRIP_WPM, STRIP_WPM_BAR, STRIP_BATTERY, STRIP_LINK, STRIP_SCANNER, STRIP_OFF, STRIP_MODES };

void        strip_init(void);
void        strip_task(void);
uint8_t     strip_mode(void);
const char *strip_mode_name(uint8_t m);
void        strip_set_mode(uint8_t m);
uint8_t     strip_color_index(void); // номер в type84_colors
void        strip_set_color_index(uint8_t i);
bool        strip_caps_alert(void);
void        strip_set_caps_alert(bool on);
