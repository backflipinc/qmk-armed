# Hardware map

Reverse-engineered from the stock firmware v1.32 and confirmed on the keyboard with this
firmware unless marked ⚠️. QMK pin names: `A` = P0, `B` = P1, `C` = P2, `D` = P3
(so `B14` is P1.14).

## MCU

| | |
|---|---|
| Chip | Sonix **SN32F290** family (SN32F299F, Cortex-M0, 256 KiB flash, 32 KiB RAM); the marking on the board is scrubbed — identified from the vendor configurator (`hexType 3` → "290") and the firmware |
| Clock | IHRC 12 MHz after reset, PLL 48 MHz in the firmware |
| Bootloader | mask ROM at `0x1FFF0000`, entry `0x1FFF0301`; USB `0c45:7140`; no flash read command |
| Firmware flag | `0xAAAA5555` at `0x3FFFC` (end of flash) |
| Code protection | CS0 (none) — don't change it, higher levels can lock ISP/SWD |
| Reset / debug | P3.7 = nRESET. SWD is enabled after reset and disabled by the firmware (`SYS0->SWDCTRL`); **P3.6 is an SWD pin**, the other one is unknown ⚠️ |
| BOOT pin | not published for SN32F290; candidate **P1.3** (that's BOOT on SN32F280) ⚠️ |

## Key matrix — 6 rows × 16 columns

Columns are driven **high** one at a time; rows are inputs with **external pull-down
resistors** (a pressed key reads high). The SN32F290 has no internal pull-downs, which is
why QMK's stock `COL2ROW` scanning doesn't work here and the port uses its own scanner.

| Row | Pin | | Col | Pin | | Col | Pin |
|---|---|---|---|---|---|---|---|
| 0 | P1.14 `B14` | | 0 | P0.4 `A4` | | 8 | P2.4 `C4` |
| 1 | P1.15 `B15` | | 1 | P0.5 `A5` | | 9 | P2.5 `C5` |
| 2 | P1.19 `B19` | | 2 | P2.0 `C0` | | 10 | P2.6 `C6` |
| 3 | P3.19 `D19` | | 3 | P2.1 `C1` | | 11 | P2.7 `C7` |
| 4 | P0.19 `A19` | | 4 | P2.2 `C2` | | 12 | P2.14 `C14` |
| 5 | P0.18 `A18` | | 5 | P2.3 `C3` | | 13 | P2.8 `C8` |
| | | | 6 | P0.6 `A6` | | 14 | P2.9 `C9` |
| | | | 7 | P0.7 `A7` | | 15 | P0.0 `A0` |

Layout by matrix position:

| R\C | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Esc | F1 | F2 | F3 | F4 | F5 | F6 | F7 | F8 | F9 | F10 | F11 | F12 | PrtSc | — | — |
| 1 | \` | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 | - | = | Bksp | Ins | PgUp |
| 2 | Tab | Q | W | E | R | T | Y | U | I | O | P | [ | ] | \\ | Del | PgDn |
| 3 | Caps | A | S | D | F | G | H | J | K | L | ; | ' | Enter | | | |
| 4 | LShift | — | Z | X | C | V | B | N | M | , | . | / | RShift | | ↑ | |
| 5 | LCtrl | Win | LAlt | knob¹ | knob² | | Space | | | | RAlt | Fn | RCtrl | ← | ↓ | → |

(0,14) / (0,15) are Home / End and (4,1) is ISO `\` on the shared PCB — not populated on
this model. ¹ knob press in the Esc spot, ² knob press in the PrtSc spot.

## Knob (rotary encoder)

One removable knob that fits in either of two spots:

| Spot | A / B pins | Press |
|---|---|---|
| Esc | P0.1 `A1` / P0.2 `A2` | matrix (5,3) |
| PrtSc | P3.5 `D5` / P3.6 `D6` | matrix (5,4) — P3.6 only works as GPIO after SWD is disabled |

## Switches

| Switch | Pins | Logic |
|---|---|---|
| Mode USB / BT / 2.4G | P1.12 `B12`, P1.13 `B13` | both high = USB, P1.12 low = Bluetooth, P1.13 low = 2.4 GHz |
| SnapTap | P3.15 `D15` | low = on ⚠️ (needs the internal pull-up; drives SnapTap in `snaptap.c`) |

## RGB LEDs

A multiplexed matrix that **shares its columns with the key matrix**: 6 LED rows × 3
colours = 18 row-select pins (active high), and the 16 key columns double as LED cathode
columns driven by timer PWM (column low while `TC < MR`). Scanning keys and lighting LEDs
are interleaved in one timer interrupt, like the stock firmware does (see
[development.md](development.md#rgb-and-matrix-engine)).

LED (row, col) sits under the key at matrix (row, col). 82 key LEDs + 9 light-bar LEDs.

Row-select pins:

| LED row | Red | Green | Blue |
|---|---|---|---|
| 0 | P1.5 `B5` | P0.11 `A11` | P1.4 `B4` |
| 1 | P3.8 `D8` | P0.8 `A8` | P0.9 `A9` |
| 2 | P3.11 `D11` | P3.9 `D9` | P3.10 `D10` |
| 3 | P3.16 `D16` | P3.12 `D12` | P3.13 `D13` |
| 4 | P2.10 `C10` | P3.17 `D17` | P3.18 `D18` |
| 5 | P2.13 `C13` | P2.11 `C11` | P2.12 `C12` |

Column PWM channels (timers prescaled to 2 MHz; PFPA remaps `CT16B0=0x3302`,
`CT16B1=0x0700`, `CT16B2=0x0001`; match registers need the `0x5A` key in the top byte):

| Col | Pin | Timer channel | | Col | Pin | Timer channel |
|---|---|---|---|---|---|---|
| 0 | P0.4 | CT16B1 PWM4 | | 8 | P2.4 | CT16B1 PWM9 |
| 1 | P0.5 | CT16B1 PWM5 | | 9 | P2.5 | CT16B2 PWM0 |
| 2 | P2.0 | CT16B1 PWM8 | | 10 | P2.6 | CT16B0 PWM0 |
| 3 | P2.1 | CT16B2 PWM1 | | 11 | P2.7 | CT16B1 PWM10 |
| 4 | P2.2 | CT16B2 PWM2 | | 12 | P2.14 | CT16B0 PWM3 |
| 5 | P2.3 | CT16B2 PWM3 | | 13 | P2.8 | CT16B0 PWM2 |
| 6 | P0.6 | CT16B1 PWM6 | | 14 | P2.9 | CT16B0 PWM1 |
| 7 | P0.7 | CT16B1 PWM7 | | 15 | P0.0 | CT16B1 PWM0 |

### Light bar under the display

9 RGB LEDs on otherwise empty positions of the LED matrix, left to right:

`(3,13) (3,14) (3,15) (4,13) (4,15) (5,3) (5,4) (5,5) (5,7)`

## Display

ST7789, **320×172** IPS, RGB565, on SPI0 (mode 0, CS by GPIO).

| Signal | Pin |
|---|---|
| SCK / MOSI / MISO | P3.0 `D0` / P3.2 `D2` / P3.1 `D1` (via PFPA `SPI=0xF5BF`) |
| CS | P1.8 `B8` |
| D/C | P3.14 `D14` (low = command) |
| RESET | P0.17 `A17` |
| Backlight | P0.16 `A16` (high = on) |

Landscape: MADCTL `0xA0`, offset (0, 34), inversion on. The panel needs the vendor init
sequence (see `lcd_stock_init` in `type84_pro.c`) with 150 ms after sleep-out.

## Radio module (Bluetooth + 2.4 GHz)

A separate module on **UART2: TX P1.7 `B7`, RX P1.6 `B6`**, 115200 8N1. Protocol:
[radio-protocol.md](radio-protocol.md).

## Power

| | |
|---|---|
| Fuel gauge | CellWise **CW2017**-compatible at I²C `0x63`, bit-banged: SCL P0.14 `A14`, SDA P0.15 `A15`. SoC = `(reg4·256 + reg5)·100 / 0x6000`, voltage = `reg2..3 × 5/16 mV`; needs the 80-byte battery profile in regs `0x10–0x5F` (in `battery_cw2017.c`) |
| USB power | P1.17 `B17`, **low** = VBUS present |
| Charge status | P1.16 `B16`, **high** = charge complete |
| Wake inputs | P0.10, P1.2 — wake the stock firmware from sleep but are never read; source unknown ⚠️ |

## Other

| | |
|---|---|
| SPI flash | two chips on SPI1 (SCK P0.12, MOSI P1.11, MISO P1.10, CS P1.9 / P0.13) — stock screen images and animations; not used by this firmware |
| Unused pins | P0.3, P1.0, P1.1, P1.3, P2.15, P3.3, P3.4 (pulled up like the stock firmware does) |
