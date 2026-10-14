# Development notes

How the port is put together, what was tricky, and how to change things.

## Layout of the keyboard folder

| File | What it does |
|---|---|
| `keyboard.json` | MCU, USB ids, layout (82 keys + 2 knob presses), RGB Matrix LED list, effects, encoders |
| `ledscan.c` | **Key matrix + RGB** in one timer interrupt (see below); custom RGB Matrix driver |
| `rgb_matrix_kb.inc` | custom two-colour effects, plus the theme slots from `quantum/armed/armed_theme_effects.inc` |
| `strip.c` | the 9-LED light bar under the display |
| `ui.c` | display UI: home screen, menu, toasts, confirmations |
| `lcd_spi.c` | Quantum Painter ↔ ST7789 transport on raw SPI0 registers |
| `spiflash.c` | the two SPI NOR flash chips on raw SPI1 registers (read, erase, program, power-down) |
| `screen.c` | the user animation: stock flash layout, playback by SPI-to-SPI DMA in the background, qmk-armed upload hooks |
| `type84_pro.c` | board init (SWD off, PFPA, pull-ups), display init, colours, Fn-layer overlay, USB battery report, hooks |
| `wireless.c` | radio module: mode switch, BT slots, pairing, reports, NKRO over the air, auto-sleep |
| `battery_cw2017.c` | fuel gauge over bit-banged I²C, VBUS / charge status |
| `rtc_bm8563.c` | the RTC chip on the same I²C bus: the HUD clock, set by Vial Armed |
| `safety.c` | Esc-on-plug bootloader check, watchdog, `bootloader_jump()` |
| `snaptap.c` | the SnapTap switch: SOCD for A/D and W/S |
| `settings.c` | keyboard settings in the EEPROM kb datablock |
| `armed_kb.c` | qmk-armed features for Vial Armed: colours, Fn colour, light bar, auto-sleep, main screen, theme storage in the user flash chip (built with VIA / Vial only) |
| `chconf.h`, `halconf.h`, `mcuconf.h`, `config.h`, `rules.mk` | configuration |
| `graphics/` | QFF fonts generated from `graphics/fonts/` |
| `docs/`, `udev/`, `flash.sh`, `tools/` | documentation, udev rules, flasher wrapper, `vial.json` generator, `screen.py` animation uploader |

The keyboard relies on these qmk-armed core changes:

- USB HID battery report (`HID_BATTERY_ENABLE`);
- Vial: the core diff of vial-qmk `merge-2025-12-28` against its QMK base (`acbeec29da`, an ancestor
  of the SonixQMK commit the fork starts from), without keyboards, docs and the STM32 `vibl` bootloader;
- the [qmk-armed protocol](../../../../docs/armed.md) for Vial Armed, including the screen animation upload;
- `raw_hid_task()` handles at most `RAW_HID_REPORTS_PER_TASK` (4) reports per main loop pass.

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
| UPower shows the USB battery as "gaming input" | QMK's System Control report spans Generic Desktop axes → udev tags the interface as a joystick; the USB battery report narrows it |
| UPower doesn't pick up the USB battery | The kernel reads the battery with GET_REPORT at probe time; QMK didn't answer it → qmk-armed stores the report |
| Keyboard drops into the bootloader during a long screen upload / dump | QMK's `raw_hid_task()` drained raw HID reports in a `while` loop; a host that keeps the pipeline full never let it return, so housekeeping didn't feed the watchdog → capped per pass in the core |
| LED bar under the display shows random colours while a host talks raw HID (Vial, `screen.py`) | The LED engine's CT16B3 interrupt had the lowest priority and USB (priority 1) cut into its phases → `ledscan.c` runs it at priority 0 |
| Animation slow when the CPU copies flash → display (~2 fps) | SPI0 has an SPI-to-SPI DMA bridge (`DMACTRL`/`DMACNT`): it clocks the flash on SPI1 straight into the panel, a frame in 37 ms. It only completes with SPI1 `RXFIFOTHIE` and SPI0 `DMATCIE` set, even with no IRQ taken (as stock) |
| With `BLUETOOTH_ENABLE`, PC sleep isn't handled | QMK builds with `NO_USB_STARTUP_CHECK` → the USB suspend loop is repeated in `wireless.c` (USB mode only) |

## Settings in EEPROM

EEPROM is emulated in the MCU's own flash (`wear_leveling`, `embedded_flash`): 16 KiB of
flash back 4 KiB of EEPROM, which QMK mirrors in RAM. The keyboard's own settings live in the
QMK *kb datablock* as one struct, `kb_settings_t` (`settings.h`, 32 bytes reserved):

| Field | Setting |
|---|---|
| `bt_slot` | Bluetooth slot (1–5) |
| `sleep_idx` | auto-sleep timer index |
| `accent`, `screen_timeout` | UI accent colour, screen-off timeout |
| `color2`, `fn_color` | colour 2 and Fn overlay colour (preset index) |
| `strip_mode`, `strip_color`, `strip_caps_alert` | light bar |
| `snaptap_mode` | SnapTap: last input / neutral |
| `screen_mode`, `clock_format` | main screen (HUD / animation), HUD clock (24H / 12H / off) |

Changing the struct layout means bumping `EECONFIG_KB_DATA_VERSION` in `config.h`: a version
mismatch resets these settings to defaults. Changing the wear-leveling sizes moves the
storage and resets **all** settings once.

The RGB Matrix, keymap (NKRO, Win lock) and connection settings use QMK's own EEPROM areas.

## Settings in Vial Armed

[Vial Armed](https://github.com/backflipinc/vial-gui-armed) reads and changes settings over the
[qmk-armed protocol](../../../../docs/armed.md). The core covers RGB Matrix; `armed_kb.c` declares
`ARMED_F_COLOR2`, `ARMED_F_FN_COLOR`, `ARMED_F_STRIP`, `ARMED_F_SLEEP` and `ARMED_F_SCREEN` and maps them onto the
settings above. All custom effects from `rgb_matrix_kb.inc` except the theme slots are flagged as two-colour.
`ARMED_THEMES 4` in `config.h` turns on the lighting themes; they are stored at 0xFFC000 on the user flash chip.

## Keymaps

| Keymap | Use |
|---|---|
| `default` | everyday firmware with Vial (live remapping over USB); `vial.json` is generated from `keyboard.json` by `tools/vial_json.py` |
| `novial` | the same layout without Vial |
| `debug` | `novial` + QMK console (`qmk console`), USB stays connected in BT/2.4G so you can watch radio frames (`WL >` / `WL <`) and battery readings. For the same log with Vial (e.g. the dongle tunnel): `make io/type84_pro:default CONSOLE_ENABLE=yes EXTRAFLAGS="-DWIRELESS_KEEP_USB -DWL_DEBUG_CONSOLE"` |
| `safe` | minimal: no display, console with a pin sniffer, Fn+Backspace = watchdog test |

Custom keycodes for your own keymap (`type84_pro.h`): `BT_1`…`BT_5`, `RF_PAIR`, `SLEEP_T`,
`UI_UP` / `UI_DOWN` / `UI_LEFT` / `UI_RIGHT` / `UI_OK` / `UI_BACK`.

## Regenerating fonts

```sh
G=keyboards/io/type84_pro/graphics
qmk painter-make-font-image --font $G/fonts/ShareTechMono-Regular.ttf --size 20 -o tech20.png
qmk painter-convert-font-image --input tech20.png -f mono4
mv tech20.qff.* $G/
```

(`tech14` = Share Tech Mono 14, `raj44` = Rajdhani Bold 44.) Run `qmk` from the root of the
tree, otherwise it doesn't find the painter commands.
