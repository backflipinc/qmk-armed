# qmk-armed protocol

A small Raw HID protocol for settings that VIA and Vial don't cover. [Vial Armed](https://github.com/backflipinc/vial-gui-armed)
probes it on connect and shows its Lighting and Device pages only for what the keyboard reports.

It is built into every keyboard compiled with `VIA_ENABLE = yes` (Vial included). Turn it off with
`ARMED_ENABLE = no` in `rules.mk`. The core lives in `quantum/armed/`.

## What the keyboard gets for free

With RGB Matrix enabled, the core handles everything RGB Matrix knows by itself:

- every effect the firmware is built with, by name, including the keyboard's custom ones;
- on / off, effect, hue, saturation, brightness (capped at `RGB_MATRIX_MAXIMUM_BRIGHTNESS`) and speed;
- the LED map (matrix position, x / y, flags), so the app can draw a live preview of the effect on the layout.

VialRGB maps effects it doesn't know to "off", so on a keyboard with custom effects stock Vial shows them as
*Disable*. Vial Armed uses this protocol instead and hides the VialRGB page.

## Adding keyboard features

Anything beyond RGB Matrix is a feature the keyboard declares and implements through weak hooks
(`quantum/armed/armed.h`). Only the declared features show up in the app.

| Feature | Lists | Values |
|---|---|---|
| `ARMED_F_RGB_MATRIX` (set by the core) | effects, LEDs | on / off, effect, hue, saturation, brightness, speed |
| `ARMED_F_COLOR2` | colours | second colour of the two-colour effects |
| `ARMED_F_FN_COLOR` | colours | colour of the keys lit while Fn is held |
| `ARMED_F_STRIP` | colours, light bar modes | light bar mode, colour, Caps Lock alert |
| `ARMED_F_SLEEP` | sleep options | auto-sleep option |

```c
#include "armed.h"

uint16_t armed_features_kb(void) {
    return ARMED_F_SLEEP;
}

uint8_t armed_list_count_kb(uint8_t list) {
    return list == ARMED_L_SLEEP ? 3 : 0;
}

bool armed_list_entry_kb(uint8_t list, uint8_t index, uint8_t *out, uint8_t length) {
    static const uint8_t minutes[] = {5, 30, 0};
    if (list != ARMED_L_SLEEP) return false;
    out[0] = minutes[index];
    return true;
}

bool armed_get_value_kb(uint8_t id, uint8_t *value) {
    if (id != ARMED_V_SLEEP) return false;
    *value = my_sleep_index();
    return true;
}

bool armed_set_value_kb(uint8_t id, uint8_t value) {
    if (id != ARMED_V_SLEEP || value >= 3) return false;
    my_sleep_set_index(value);
    return true;
}
```

`armed_effect_flags_kb(mode)` marks effects that use the second colour (`ARMED_EFFECT_COLOR2`).
See `keyboards/io/type84_pro/armed_kb.c` for a keyboard that implements all of them.

## Wire format

Requests and replies are 32 bytes: `[0xAD, command, ...]`. Command `0xAD` is unused by VIA and Vial.
A request the firmware can't serve comes back with `0xFF` (`id_unhandled`) in the first byte.

| Command | Request | Reply |
|---|---|---|
| `0` INFO | — | `[2..5]` `ARMD`, `[6]` protocol (1), `[7..9]` firmware version, `[10..11]` features (LE), `[12..16]` list sizes, `[17]` max brightness |
| `1` LIST | `[2]` list, `[3]` index | `[4..]` entry, see below |
| `2` GET | `[2]` value id | `[3]` value |
| `3` SET | `[2]` value id, `[3]` value | applied and saved (hue, saturation, brightness, speed: live only) |
| `4` SAVE | — | writes hue, saturation, brightness and speed to EEPROM |

Lists:

| Id | List | Entry |
|---|---|---|
| `0` | effects | `[4]` QMK mode id, `[5]` flags, `[6..]` name |
| `1` | colour presets | `[4]` H, `[5]` S, `[6..]` name; H=0 S=1 means black |
| `2` | light bar modes | `[4..]` name |
| `3` | sleep options | `[4]` minutes, 0 = off |
| `4` | LEDs | `[4]` row, `[5]` col (`0xFF` if not under a key), `[6]` x, `[7]` y, `[8]` flags |

Values: `0` lighting on, `1` effect (QMK mode id), `2` colour 2, `3` Fn colour, `4` light bar mode,
`5` light bar colour, `6` Caps alert, `7` sleep option, `8` hue, `9` saturation, `10` brightness, `11` speed.
Colours are indexes into the colour preset list.

Hue, saturation, brightness and speed skip EEPROM writes, so a slider can stream them; the app sends SAVE
when the slider is released. `ARMED_PROTOCOL` changes only when the wire format breaks; new features and
values are added without a bump.
