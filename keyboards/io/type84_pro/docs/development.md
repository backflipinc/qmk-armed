# Development notes

How the port is put together, what was tricky, and how to change things.

## Layout of the keyboard folder

| File | What it does |
|---|---|
| `keyboard.json` | MCU, USB ids, layout (82 keys + 2 knob presses), RGB Matrix LED list, effects, encoders |
| `ledscan.c` | **Key matrix + RGB** in one timer interrupt (see below); custom RGB Matrix driver |
| `rgb_matrix_kb.inc` | custom two-colour effects |
| `strip.c` | the 9-LED light bar under the display |
| `ui.c` | display UI: home screen, menu, toasts, confirmations |
| `lcd_spi.c` | Quantum Painter ↔ ST7789 transport on raw SPI0 registers |
| `type84_pro.c` | board init (SWD off, PFPA, pull-ups), display init, colours, Fn-layer overlay, USB battery report, hooks |
| `wireless.c` | radio module: mode switch, BT slots, pairing, reports, NKRO over the air, auto-sleep |
| `battery_cw2017.c` | fuel gauge over bit-banged I²C, VBUS / charge status |
| `safety.c` | Esc-on-plug bootloader check, watchdog, `bootloader_jump()` |
| `chconf.h`, `halconf.h`, `mcuconf.h`, `config.h`, `rules.mk` | configuration |
| `graphics/` | QFF fonts generated from `fonts/` |

The repo also carries one QMK core patch, `patches/hid-battery.patch` (applied by `setup.sh`).

## Boot order

1. `early_hardware_init_pre()` (`safety.c`) — first code after reset, before clocks:
   watchdog off; reset caused by the watchdog → ROM bootloader; Esc held → ROM bootloader;
   QMK's `QK_BOOT` magic → ROM bootloader.
2. Clocks (PLL 48 MHz), then `early_hardware_init_post()` starts a ~4 s watchdog.
3. `keyboard_pre_init_kb()` — SWD off (frees P3.6 for the knob), pin remapping, pull-ups.
4. Matrix init starts the LED/scan engine; USB; radio (`bluetooth_init`); battery.
5. `keyboard_post_init_kb()` — settings, display, UI.

The watchdog is fed from `housekeeping_task_kb()` and the USB suspend loop, and is
stopped during deep sleep.

## RGB and matrix engine

LED columns and key columns are the same 16 pins, so scanning and PWM are interleaved, the
same way the stock firmware does it:

- CT16B0/1/2 drive the column PWM, CT16B3 fires every 128 µs (all timers at 2 MHz).
- 21 phases per frame = 3 colours × (6 LED rows + 1 idle), ~2.7 ms per frame (~370 Hz).
- Each phase: all row pins low → stop PWM, pins back to GPIO → **scan one key column**
  (~5 µs settle) → load the next row's duties → pins back to PWM → row pin high → start.
  16 phases give a full key scan (~2 ms); QMK's debounce runs on top.
- Effects render into a back buffer; `flush()` copies the whole frame, so overlays (the
  Fn-layer highlight) don't flicker. The light bar writes straight to the front buffer.

## Gotchas worth knowing

| Symptom | Cause / fix |
|---|---|
| Phantom keys with `COL2ROW` | Rows have external pull-downs; the chip has no internal ones → custom scanning |
| Knob in the PrtSc spot doesn't turn | P3.6 is an SWD pin until `SYS0->SWDCTRL \|= 1` |
| Display stays black with Quantum Painter | QP's generic `qp_st7789_init()` (5 ms after sleep-out, tuning after display-on) doesn't work for this panel → overridden with the vendor sequence. Also `spi_master` on SN32 left the panel dark; `lcd_spi.c` talks to SPI0 directly |
| Picture upside down | Use `QP_ROTATION_270` (MADCTL `0xA0`) |
| Firmware hangs when the RGB engine starts | ChibiOS on SN32F290 keeps system time on **CT16B0** (tick-less mode), which the LED PWM needs → `chconf.h` switches to a periodic SysTick (10 kHz). Side effect: `wait_us()` now sleeps ≥ 100 µs, use busy loops for µs delays |
| Display goes black after 30 s, backlight stays on | Quantum Painter's own display timeout → `QUANTUM_PAINTER_DISPLAY_TIMEOUT 0` |
| Colours shift at low brightness | `hsv_to_rgb()` applies a CIE curve and integer maths — dim the finished RGB instead, and round PWM duties up |
| UPower shows the USB battery as "gaming input" | QMK's System Control report spans Generic Desktop axes → udev tags the interface as a joystick; the core patch narrows it |
| UPower doesn't pick up the USB battery | The kernel reads the battery with GET_REPORT at probe time; QMK didn't answer it → the core patch stores the report |
| With `BLUETOOTH_ENABLE`, PC sleep isn't handled | QMK builds with `NO_USB_STARTUP_CHECK` → the USB suspend loop is repeated in `wireless.c` (USB mode only) |

## Settings in EEPROM (`eeconfig_kb`, 32 bits)

| Bits | Setting |
|---|---|
| 0–2 | Bluetooth slot (1–5) |
| 3–5 | auto-sleep timer index |
| 6–8 | UI accent colour |
| 9–11 | screen-off timeout |
| 12 | one-off migration done (NKRO default) |
| 13 | legacy colour-2 format |
| 14 | colour settings in v2 format |
| 16–19 | colour 2 (preset index) |
| 20–23 | Fn overlay colour (preset index) |
| 24–26 | light bar mode |
| 27–30 | light bar colour (preset index) |
| 31 | light bar Caps alert **off** |

The RGB Matrix, keymap (NKRO, Win lock) and connection settings use QMK's own EEPROM areas.

## Keymaps

| Keymap | Use |
|---|---|
| `default` | everyday firmware |
| `debug` | `default` + QMK console (`qmk console`), USB stays connected in BT/2.4G so you can watch radio frames (`WL >` / `WL <`) and battery readings |
| `safe` | minimal: no display, console with a pin sniffer, Fn+Backspace = watchdog test |

Custom keycodes for your own keymap (`type84_pro.h`): `BT_1`…`BT_5`, `RF_PAIR`, `SLEEP_T`,
`UI_UP` / `UI_DOWN` / `UI_LEFT` / `UI_RIGHT` / `UI_OK` / `UI_BACK`.

## Regenerating fonts

```sh
cd qmk_firmware
qmk painter-make-font-image --font ../fonts/ShareTechMono-Regular.ttf --size 20 -o tech20.png
qmk painter-convert-font-image --input tech20.png -f mono4
mv tech20.qff.* ../keyboards/io/type84_pro/graphics/
```

(`tech14` = Share Tech Mono 14, `raj44` = Rajdhani Bold 44.) Run `qmk` from inside the
`qmk_firmware` tree, otherwise it doesn't find the painter commands.
