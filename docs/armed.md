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
| `ARMED_F_HUD` (set by the core) | — | the main screen built from widgets and fed by the host, see [HUD](#hud) |
| `ARMED_F_KEYMAP` (set by the core) | — | NKRO (with `NKRO_ENABLE`), GUI lock |
| `ARMED_F_SNAPTAP` | SnapTap modes | SnapTap mode |
| `ARMED_F_DISPLAY` | UI accents, screen-off timeouts | UI accent, screen-off timeout |
| `ARMED_F_BT_SLOT` | — | Bluetooth slot (1..5) |

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
| `17` BOOTLOADER | — | echoed, then the keyboard reboots into its bootloader (no Vial unlock needed) |
| `18` HUD_INFO | — | `[2]` format (1), `[3]` max widgets, `[4]` widget size, `[5..6]` layout size, `[7..8]` label bytes, `[9]` host numbers, `[10]` host texts, `[11]` host text size, `[12]` fonts, `[13]` colour roles, `[14]` sources, `[15..16]` width, `[17..18]` height, `[19]` max graphs, `[20]` graph samples, `[21]` stored layouts (0 before 2.1.0: one), `[22]` the one shown |
| `19` HUD_SOURCE | `[2]` index (< sources) | `[3]` source id, `[4]` kind: 0 text, 1 number, 2 on / off, 3 multi-line text, `[5]` decimals (`0xFF`: set by the host), `[6..]` name |
| `20` HUD_VALUE | `[2]` source id | `[3]` valid, `[4]` suggested colour role, `[5]` decimals, `[6..9]` number (LE, signed), `[10..]` text |
| `21` HUD_FONT | `[2]` font, `[3..6]` offset, `[7]` length (≤ 24) | `[8..]` bytes of the font (QFF), for a preview drawn with the keyboard's own fonts |
| `22` HUD_PALETTE | `[2]` colour role | `[3..5]` H, S, V as the keyboard draws it now (the accent can change) |
| `23` HUD_READ | `[2..3]` offset, `[4]` length (≤ 27) | `[5..]` bytes of the layout the keyboard shows |
| `24` HUD_WRITE | `[2..3]` offset, `[4]` length (≤ 27), `[5..]` bytes | written; the HUD stops drawing until APPLY (3 s at most) |
| `25` HUD_APPLY | `[2]` 1 = also store | checked and shown (a repeated APPLY with no HUD_WRITE since does not redraw); a bad layout answers `0xFF` and the stored one comes back |
| `26` HUD_REVERT | `[2]` 0 = the stored layout, 1 = the built-in default | shown, not stored (no redraw when the layout does not change) |
| `27` HUD_PUSH | `[2]` seconds the values stay valid (0 = until replaced), `[3]` count (≤ 4), then per value `slot`, `decimals`, number (4 bytes LE, signed) | **no reply** |
| `28` HUD_TEXT | `[2]` slot, `[3]` seconds valid, `[4]` offset, `[5]` length (≤ 25), `[6]` 1 = last piece, `[7..]` ASCII | **no reply**; pieces in order, starting at offset 0 |
| `29` GET_MANY | `[2]` count (≤ 13), `[3..]` value ids | `[3..]` the values in place of their ids, `[16..17]` bit i set: value i answered (LE) |
| `30` ENTRIES | `[2]` Vial dynamic entry GET op (1 tap dance, 3 combo, 5 key override, 7 alt repeat key), `[3]` first index | `[2]` how many follow, `[4..]` the entries as Vial's GET returns them, packed (2 of 10 bytes, 4 of 6); fewer at the end, 0 past it |

Lists:

| Id | List | Entry |
|---|---|---|
| `0` | effects | `[4]` QMK mode id, `[5]` flags, `[6..]` name |
| `1` | colour presets | `[4]` H, `[5]` S, `[6..]` name; H=0 S=1 means black |
| `2` | light bar modes | `[4..]` name |
| `3` | sleep options | `[4]` minutes, 0 = off |
| `4` | LEDs | `[4]` row, `[5]` col (`0xFF` if not under a key), `[6]` x, `[7]` y, `[8]` flags |
| `5` | SnapTap modes | `[4..]` name |
| `6` | UI accents | `[4]` H, `[5]` S, `[6..]` name |
| `7` | screen-off timeouts | `[4..5]` seconds (LE), 0 = never, `[6..]` name |

Lists 5 and up aren't sized in INFO (that would move the bytes after the list sizes): read them until the keyboard
answers `0xFF`.

Values: `0` lighting on, `1` effect (QMK mode id), `2` colour 2, `3` Fn colour, `4` light bar mode,
`5` light bar colour, `6` Caps alert, `7` sleep option, `8` hue, `9` saturation, `10` brightness, `11` speed,
`12` main screen (0 = HUD, 1 = the uploaded animation), `13` clock (0 = 24H, 1 = 12H, 2 = off), `14` NKRO,
`15` GUI lock, `16` SnapTap mode, `17` UI accent, `18` screen-off timeout (indexes into their lists), `19` Bluetooth slot, `20` HUD layout shown (with `ARMED_F_HUD`;
firmware before 2.1.0 answers unhandled).
Colours are indexes into the colour preset list.

GET_MANY and ENTRIES save round trips over the 2.4 GHz tunnel, where each one costs about 20 ms; older
firmware answers `0xFF` and the app reads one by one.

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

[Vial Armed](https://github.com/backflipinc/vial-gui-armed) has it under Screen > Animation;
`keyboards/io/type84_pro/tools/screen.py` is a command line uploader (images, GIFs, video).
Keyboard side: implement `armed_screen_info_kb()`, `armed_screen_begin_kb()`, `armed_screen_write_kb()`
(the decoded animation in order), `armed_screen_end_kb()` and `armed_screen_read_kb()`; see
`keyboards/io/type84_pro/screen.c`.

## Lighting themes

A theme is a stack of up to 8 layers drawn bottom to top, plus a colour for every LED. Each slot is an
RGB Matrix effect (`THEME_1`, ...), so it is picked like any other effect; global brightness applies.
[Vial Armed](https://github.com/backflipinc/vial-gui-armed) edits them live under Lighting > Theme editor.

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
| 7 | show when: `0` always, `1` Caps Lock on, `2` Caps Lock off, `0x10 \| n` the active layer (the highest one on) is n, `0x20 \| n` it is not (n = 0: the base layer) |
| 8..10 | colour A |
| 11..13 | colour B |
| 14 | param |
| 15, 16 | `cx`, `cy` in LED coordinates (0..224, 0..64) |
| 17 | keys: `0` the mask below, `0x80` every key, `n` (1..15) the keys of keymap layer n: bound there (not `KC_TRNS` / `KC_NO`) or bringing it up (`MO`, `LT`, `TG`, `TT`, `OSL`, `LM`, `TO`, tri-layer keys) on another layer. The keyboard reads its keymap again every 250 ms, so a remap needs no theme edit |
| 18, 19 | 0 |
| 20.. | M bytes, bit i: the layer covers LED i. With keys ≠ 0 the editor keeps the keys it shows here, for firmware older than 2.0.0 |

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

## HUD

A keyboard with a Quantum Painter screen can let the host lay out its main screen: a list of up to 40 widgets
drawn in order, in the keyboard's own fonts and colours. [Vial Armed](https://github.com/backflipinc/vial-gui-armed)
has a HUD editor (Screen > HUD editor) with a pixel-exact preview (it reads the fonts with HUD_FONT and redraws what `armed_hud.c` draws)
and an agent that feeds the screen with the computer's metrics and the output of commands.

Keyboard side: call `armed_hud_init(device, fonts, count)` once the display is up and `armed_hud_draw(all)` where the
main screen is drawn; `all` means the whole screen, otherwise only widgets whose content changed are redrawn.
Optional hooks: `armed_hud_defaults_kb()` builds the default layout with `armed_hud_add()`,
`armed_hud_color_kb()` gives the colour roles their colours, `armed_hud_source_kb()` adds or overrides sources,
`armed_hud_load_kb(screen, …)` / `armed_hud_save_kb(screen, …)` store the layouts (1476 bytes each with the default
sizes). `ARMED_HUD_SCREENS` (default 1) is how many the keyboard keeps; `armed_hud_set_screen()` picks the one shown,
which the HUD commands read, write, store and revert. A layout never stored starts as the default one.
See `keyboards/io/type84_pro/ui.c` and `armed_kb.c`.

### Colours

Widgets name colour **roles**, not colours, so a layout keeps the keyboard's look and follows its accent setting:
`0` BG, `1` PANEL, `2` LINE, `3` DIM, `4` TEXT, `5` ACCENT, `6` OK, `7` WARN; `0x0F` AUTO takes the colour the
source suggests (accent on a layer above the base, OK while charging, the link state's colour, ...).

### Sources

| Id | Source | Kind |
|---|---|---|
| `1` | layer: `BASE`, `L1`, ... (the keyboard may name them) | text |
| `2` | WPM | number |
| `3` | battery, % | number |
| `4` | battery voltage | number |
| `5` | charging | on / off |
| `6` | link status, e.g. `BT1 LINKED` | text, number 1 when connected |
| `7` | link mode: `USB`, `BT1`, `2.4G` | text |
| `8`, `9` | time, date (needs `ARMED_F_CLOCK`) | text |
| `10`–`16` | Caps / Num / Scroll Lock, NKRO, lighting on, GUI lock, SnapTap | on / off |
| `17` | lighting effect or theme name | text |
| `18` | uptime | text |
| `19` | lighting brightness, % | number |
| `0x40`–`0x4F` | host numbers 1–16 (HUD_PUSH) | number |
| `0x60`–`0x63` | host texts 1–4, up to 159 characters (HUD_TEXT) | multi-line text |

Only the sources the keyboard has are listed by HUD_SOURCE. Numbers carry their decimal places: 425 with 1 decimal
is 42.5. Host values go stale after the seconds sent with them and show as `--`; the agent resends every second.
The fonts are ASCII: other bytes show as `?`. HUD_PUSH and HUD_TEXT get no reply, so an agent can feed the screen
while another program talks to the keyboard, and they don't count as activity: a wireless keyboard still falls
asleep.

### Layout

| Offset | Size | |
|---|---|---|
| 0 | 1 | magic `0xB7` |
| 1 | 1 | format, `1` |
| 2 | 1 | widgets used |
| 3 | 1 | 0 |
| 4 | 40 × 24 | widgets |
| 964 | 512 | labels: zero-terminated ASCII strings |

Widget, integers little-endian:

| Byte | |
|---|---|
| 0 | type: 1 text, 2 bar, 3 graph, 4 indicator, 5 battery icon, 6 frame, 7 text box |
| 1 | source |
| 2 | font index |
| 3, 4 | colour role of the foreground, of the background |
| 5 | flags: `0x03` align (0 left, 1 centre, 2 right), `0x04` WARN colour at `warn`, `0x08` ... or below it, `0x10` graph filled, `0x20` text box shows the last lines, `0x40` indicator is a dot only |
| 6..13 | x, y, width, height (signed 16-bit); width 0 = to the screen's edge, height 0 = one line of the font |
| 14..19 | `lo`, `hi`, `warn` (signed 16-bit) in the source's units before the decimal point: 425 = 42.5 |
| 20 | param, per type |
| 21 | 0 |
| 22..23 | offset of the label, `0xFFFF` = none |

| Type | Draws | Param |
|---|---|---|
| text | the label with the first `{}` replaced by the value; no label = the value, no source = a fixed label | digits: pad numbers with zeros |
| bar | an outlined bar filled between `lo` and `hi` (`hi ≤ lo`: 0–100 %) | segments, 0 = solid |
| graph | the last samples (newest on the right), a line or filled columns | sample period in 100 ms (0 = 1 s) |
| indicator | a tile with a dot and the label (or the source's name), lit while the source is non-zero | — |
| battery icon | the keyboard's battery icon, 25 × 11 px | — |
| frame | 0 corner brackets (`lo` = corner length, 0 = 6), 1 box, 2 horizontal line, 3 vertical line, 4 filled panel | style |
| text box | the source's text wrapped between words at the width | extra pixels between lines |

Text is cut to the widget's width. A widget that changes is cleared to its background and redrawn, and the lines and
frames under it (not panels) are redrawn too; overlapping changing widgets are the layout's problem. At most 4 graphs keep history.
