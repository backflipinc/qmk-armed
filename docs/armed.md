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
| `ARMED_F_SCREEN` | — | what the main screen shows; plus the screen animation upload below |
| `ARMED_F_THEMES` (set by the core) | — | lighting themes, see below |
| `ARMED_F_CLOCK` | — | clock format; plus the clock commands below |

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

`armed_effect_flags_kb(mode)` marks effects that use the second colour (`ARMED_EFFECT_COLOR2`); the core adds
`ARMED_EFFECT_THEME` to theme slots.
See `keyboards/io/type84_pro/armed_kb.c` for a keyboard that implements all of them.

## Clock

The time travels as seconds since 1970-01-01 **in the host's time zone**, so the keyboard needs no zone or DST
rules; the core converts it to a date and calls `armed_clock_get_kb` / `armed_clock_set_kb`, the keyboard keeps it
(the IO Type 84 Pro has an RTC chip). Hosts send it right after a second ticks over: the keyboard counts whole
seconds. `armed_clock_now()` gives the date with the weekday for drawing it.

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
| `5` SCREEN_INFO | — | `[2..3]` width, `[4..5]` height (LE), `[6]` pixel format, `[7]` max frames to upload, `[8]` frames stored, `[9..12]` capacity in bytes, `[13..14]` header size |
| `6` SCREEN_BEGIN | `[2..5]` animation size in bytes | upload started |
| `7` SCREEN_DATA | `[2]` sequence number, `[3]` length (≤ 28), `[4..]` stream | `[2]` sequence number, `[3]` status (0 = ok), `[4..7]` bytes decoded so far |
| `8` SCREEN_END | `[2]` 1 = commit, 0 = abort | animation stored |
| `9` SCREEN_READ | `[2..5]` offset, `[6]` length (≤ 25) | `[7..]` bytes of the stored animation |
| `10` THEME_INFO | — | `[2]` slots, `[3]` layers per theme, `[4]` LEDs, `[5..6]` theme size (LE), `[7]` format (1), `[8..]` QMK mode id of each slot |
| `11` THEME_READ | `[2]` slot, `[3..4]` offset, `[5]` length (≤ 26) | `[6..]` bytes of the theme |
| `12` THEME_WRITE | `[2]` slot, `[3..4]` offset, `[5]` length (≤ 26), `[6..]` bytes | shown at once, not stored |
| `13` THEME_SAVE | `[2]` slot | stored |
| `14` THEME_REVERT | `[2]` slot | edits dropped, the stored theme is back |
| `15` CLOCK_GET | — | `[2]` 1 = the time is set, `[3..6]` local time (LE) |
| `16` CLOCK_SET | `[2..5]` local time (LE) | stored |

Lists:

| Id | List | Entry |
|---|---|---|
| `0` | effects | `[4]` QMK mode id, `[5]` flags, `[6..]` name |
| `1` | colour presets | `[4]` H, `[5]` S, `[6..]` name; H=0 S=1 means black |
| `2` | light bar modes | `[4..]` name |
| `3` | sleep options | `[4]` minutes, 0 = off |
| `4` | LEDs | `[4]` row, `[5]` col (`0xFF` if not under a key), `[6]` x, `[7]` y, `[8]` flags |

Values: `0` lighting on, `1` effect (QMK mode id), `2` colour 2, `3` Fn colour, `4` light bar mode,
`5` light bar colour, `6` Caps alert, `7` sleep option, `8` hue, `9` saturation, `10` brightness, `11` speed,
`12` main screen (0 = HUD, 1 = the uploaded animation), `13` clock (0 = 24H, 1 = 12H, 2 = off).
Colours are indexes into the colour preset list.

Hue, saturation, brightness and speed skip EEPROM writes, so a slider can stream them; the app sends SAVE
when the slider is released. `ARMED_PROTOCOL` changes only when the wire format breaks; new features and
values are added without a bump.

## Screen animation

A keyboard with `ARMED_F_SCREEN` stores one animation and plays it on its screen. The animation is a
256-byte header followed by the frames:

- header: `[0]` frame count, `[1 + i]` delay after frame i in 2 ms units, the rest `0xFF`;
- frames: `width × height` pixels row by row, RGB565 with the low byte first (format `1`).

This is the layout the IO Type 84 Pro's stock firmware keeps in its SPI flash, so the keyboard stores it as is.

SCREEN_DATA carries the animation compressed, as a stream of operations on 16-bit pixels. A control byte
is `op << 6 | n` with `n` = 1..63 pixels; `n = 0` means a 16-bit length (LE) follows.

| Op | Meaning |
|---|---|
| `0` LIT | `n` pixels follow, 2 bytes each |
| `1` RUN | one pixel follows, repeated `n` times |
| `2` PREV | `n` pixels copied from the previous frame at the same place (not in the header or the first frame) |

An operation may span several reports. The header goes through the stream too, usually as one LIT.
Requests can be pipelined: the host may send several SCREEN_DATA reports before reading the replies, which
come back in order with their sequence numbers. A failed report answers `0xFF` and the rest of the upload
is refused; send SCREEN_END with 0. The new animation becomes visible only after a successful SCREEN_END,
an aborted upload leaves none.

[Vial Armed](https://github.com/backflipinc/vial-gui-armed) has a Screen page for it;
`keyboards/io/type84_pro/tools/screen.py` is a command line uploader (images, GIFs, video).
Keyboard side: implement `armed_screen_info_kb()`, `armed_screen_begin_kb()`, `armed_screen_write_kb()`
(the decoded animation in order), `armed_screen_end_kb()` and `armed_screen_read_kb()`; see
`keyboards/io/type84_pro/screen.c`.

## Lighting themes

A theme is a stack of up to 8 layers drawn bottom to top, plus a colour for every LED. Each slot is an
RGB Matrix effect (`THEME_1`, ...), so it is picked like any other effect; global brightness applies.
[Vial Armed](https://github.com/backflipinc/vial-gui-armed) has a Themes page that edits them live.

Keyboard side: define `ARMED_THEMES` (1..8 slots) in `config.h`, add `#include "armed_theme_effects.inc"` to
`rgb_matrix_kb.inc` (under `#ifdef ARMED_ENABLE`) and implement `armed_theme_load_kb()` / `armed_theme_save_kb()`
for storage (a fixed-size blob per slot). Without storage, themes work until the keyboard is unplugged.
A slot with nothing valid stored shows a built-in default. See `keyboards/io/type84_pro/armed_kb.c`.

Theme layout, `N` = LED count, `M = (N + 7) / 8`:

| Offset | Size | |
|---|---|---|
| 0 | 1 | magic `0xA7` |
| 1 | 1 | format, `1` |
| 2 | 1 | layers used |
| 3 | 1 | 0 |
| 4 | 16 | name, ASCII, zero-terminated |
| 20 | 8 × (20 + M) | layers |
| 20 + 8 × (20 + M) | N × 3 | paint: R, G, B of every LED |

Layer:

| Byte | |
|---|---|
| 0 | type: 0 none, 1 solid, 2 gradient, 3 rainbow, 4 pulse, 5 wave, 6 sparkle, 7 reactive, 8 ripple, 9 paint |
| 1 | blend: 0 normal, 1 add, 2 multiply |
| 2 | opacity |
| 3 | speed: the pattern moves by `speed / 256` of its cycle per ms, 0 = still |
| 4 | direction, a full turn in 256 steps: 0 left to right, 64 top to bottom |
| 5 | size |
| 6 | flags: `0x01` from a point (`cx`, `cy`) instead of a direction, `0x02` mirror / bounce, `0x04` reverse, `0x80` hidden |
| 7 | show when: `0` always, `1` Caps Lock on, `2` Caps Lock off, `0x10 \| n` layer n on, `0x20 \| n` layer n off (n = 0: no layer above the base) |
| 8..10 | colour A |
| 11..13 | colour B |
| 14 | param |
| 15, 16 | `cx`, `cy` in LED coordinates (0..224, 0..64) |
| 17..19 | 0 |
| 20.. | M bytes, bit i: the layer covers LED i |

Each layer gives every LED it covers a colour and a coverage, then blends it over what is below:

| Type | Colour | Coverage |
|---|---|---|
| solid | A | full |
| gradient | A → B along the position; `size` 128 = once across the keyboard, mirror = A → B → A | full |
| rainbow | hue along the position, `param` = saturation | full |
| pulse | A ↔ B on a sine wave, shifted by position when `size` > 0 | full |
| wave | A | a band `size / 2` wide that moves along the position |
| sparkle | random mix of A and B | random LEDs flash; `speed` = how often, `size` = how slowly they fade |
| reactive | A | pressed keys, fading in `100 + 8 × size` ms; `param` = radius around the key, 0 = the key only |
| ripple | A | rings from key presses, `4 + size / 8` wide, growing at `speed` |
| paint | the LED's paint colour | full |

`armed_theme.c` is the reference; Vial Armed's `theme_render.py` draws the same frames bit for bit (sparkle aside,
it is random).
