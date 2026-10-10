#include "quantum.h"
#include "settings.h"

_Static_assert(sizeof(kb_settings_t) <= EECONFIG_KB_DATA_SIZE, "kb_settings_t does not fit EECONFIG_KB_DATA_SIZE");

static const kb_settings_t defaults = {
    .bt_slot          = 1,
    .color2           = 6, // BLUE
    .fn_color         = 9, // WHITE
    .strip_caps_alert = true,
};

static kb_settings_t cfg;
static bool          loaded;

// Lazy: bluetooth_init() runs before keyboard_post_init_kb()
kb_settings_t *kb_settings(void) {
    if (!loaded) {
        loaded = true;
        if (eeconfig_is_kb_datablock_valid()) {
            eeconfig_read_kb_datablock(&cfg, 0, sizeof(cfg));
        } else {
            cfg = defaults;
            kb_settings_save();
        }
    }
    return &cfg;
}

void kb_settings_save(void) {
    eeconfig_update_kb_datablock(&cfg, 0, sizeof(cfg));
}

void eeconfig_init_kb(void) {
    cfg    = defaults;
    loaded = true;
    kb_settings_save();
    eeconfig_init_user();
}
