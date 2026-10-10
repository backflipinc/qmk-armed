#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t bt_slot;
    uint8_t sleep_idx;
    uint8_t accent;
    uint8_t screen_timeout;
    uint8_t color2;
    uint8_t fn_color;
    uint8_t strip_mode;
    uint8_t strip_color;
    bool    strip_caps_alert;
    uint8_t snaptap_mode;
} kb_settings_t;

kb_settings_t *kb_settings(void);
void           kb_settings_save(void);
