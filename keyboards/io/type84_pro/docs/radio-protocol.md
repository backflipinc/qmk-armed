# MCU ↔ radio module protocol

The keyboard has a separate Bluetooth + 2.4 GHz module; the MCU talks to it over UART.
Reverse-engineered from the stock firmware v1.32 and implemented in
`keyboards/io/type84_pro/wireless.c`. Everything in the tables below is confirmed on the
keyboard unless marked ⚠️ (seen in the stock code, meaning not verified).

## Physical layer

| | |
|---|---|
| Port | UART2 — MCU TX **P1.7**, RX **P1.6** (PFPA `UART=0x0F00`) |
| Format | 115200 baud 8N1 (divider 26 at 48 MHz → 115385 baud), FIFO on |

## Framing

```
[CMD] [DATA ...] [SUM]        SUM = (CMD + ΣDATA) & 0xFF
```

The length is implied by `CMD` (there are no delimiters). Every valid frame is
acknowledged by the receiver with `61 0D 0A` (`"a\r\n"`, no checksum). The MCU resends a
frame up to 3 times if no ACK arrives; if the module stays silent it is assumed asleep
and the next frame is preceded by a wake-up `A6 00 A6`.

## MCU → module

| CMD | Data | Meaning |
|---|---|---|
| `A1` | 8 bytes: boot keyboard report (mods, 0, 6 keys) | keyboard |
| `A2` | 14 bytes: bitmap of key codes `0x00–0x6F` (bit `code & 7` of byte `code >> 3`) | **extra** keys for NKRO — the first 6 keys still go in `A1`, the module merges both |
| `A3` | 2 bytes: consumer usage, little-endian | media keys |
| `A4` | 1 byte | system control ⚠️ (not used) |
| `A6` | `11` USB · `30` 2.4 GHz · `31`–`35` Bluetooth slot 1–5 | select mode / slot |
| `A6` | `51` | **start pairing** on the selected slot / 2.4 GHz (send right after the slot code) |
| `A6` | `55` | going to sleep (Bluetooth) |
| `A6` | `00` | wake-up poke |
| `A6` | `52`, `53` | ⚠️ unknown; in the related Hangsheng module `52` clears the pairings and `53` asks for the battery voltage (reply `5C`), see below |
| `A7` | 0–100 | battery level, % — the module reports it to the host over Bluetooth; the 2.4 GHz dongle doesn't expose it |
| `A8` | 5 bytes: buttons, X, Y, wheel, … | mouse ⚠️ (not used) |
| `A9` | `n` + name | Bluetooth device name ⚠️ (not used; the module keeps its own) |
| `AB` | `A6 FF 01` | sent after waking up in 2.4 GHz mode |
| `91` | 32 bytes | raw HID reply through the dongle (Vial / VIA / qmk-armed); the module never ACKs it — send once |

## Module → MCU

Three-byte frames (several may arrive back to back), plus one 34-byte frame type.

| Frame | Meaning |
|---|---|
| `61 0D 0A` | ACK |
| `5A vv SS` | connected; `vv` = host LED state (Num / Caps / Scroll) when `< 8` |
| `5B 31 SS` | disconnected |
| `5B 32 SS` | connected |
| `5B 33 SS` | pairing |
| `5B 34 SS` | searching / reconnecting (arrives before `33` / `32`) |
| `5B 23 SS` | sent at module start-up (ignored) |
| `5B 42 9D` | ping, reply with ACK |
| `5C vv SS` | ⚠️ accepted, no action in the stock firmware (battery level in the Hangsheng module) |
| `81` + 32 bytes + SUM | raw HID request through the dongle (2.4 GHz only), see below |

## Sequences

| Action | Frames |
|---|---|
| Switch to Bluetooth slot 2 | `A6 32` |
| Pair slot 2 | `A6 32`, then `A6 51` |
| Pair the dongle | `A6 30`, then `A6 51` |
| Report battery | `A7 57` (87 %) — resend after each (re)connect, the module drops it while unlinked |
| Sleep (BT) / wake | `A6 55` … deep sleep … `A6 00` (and `AB A6 FF 01` in 2.4 GHz) |

## Raw HID through the 2.4 GHz dongle

The dongle (USB `0c45:fefe`) has a raw HID interface identical to QMK's: usage page `0xFF60`, usage `0x61`,
32-byte reports, no report ID. Whatever the host writes there reaches the keyboard as an `81` frame, byte for
byte (no `AA` header needed), and the 32 bytes of a `91` frame come back as an input report. So Vial, VIA and
qmk-armed work over the air unchanged: `wireless.c` feeds `81` to `raw_hid_receive()` and sends replies from
`bluetooth_send_raw_hid()` as `91`. Bluetooth has no such channel.

- The module never ACKs `91`; retrying it only duplicates the reply on the host.
- A `91` sent right after our ACK of the `81` is lost more often: 3.3 % with a 2 ms gap, 0.7 % with 8 ms
  (`WL_TUNNEL_GAP_MS`). Round trip is ~20 ms, the first request after idle ~0.4–0.7 s. The host must time out
  and ask again, and drop replies that arrive late (vial-gui-armed does both).
- Tunnel traffic resets the auto-sleep timer.
- Debugging: build with `CONSOLE_ENABLE=yes EXTRAFLAGS="-DWIRELESS_KEEP_USB -DWL_DEBUG_CONSOLE"` to log the
  frames to `qmk console` (Vial builds otherwise force `NO_DEBUG`).

## The 2.4 GHz dongle

What can be seen from the host without opening it (it is sealed):

| | |
|---|---|
| USB | `0c45:fefe`, "IO Type 84 Pro BL 2.4G", bcdDevice 1.38, USB 1.1 full speed, EP0 8 bytes, no manufacturer or serial string |
| Interfaces | 0: boot keyboard (8 B) · 1: mouse (8 B) · 2: consumer / system / NKRO, report IDs 1–3, 5 (vendor `0xFFFF`, 3 B), 6 (digitizer) · 3: raw HID `0xFF60`, 32 B in/out — the tunnel above · 4: vendor `0xFF59`, 64 B in/out |
| Own replies | `55 FC 04` on interface 2 when the keyboard drops off (the stock configurator's `GET_24G_DISCONNECT_NOTIFY`); `0f 01 01 11` on `0xFF59` to a stock `AA 80` request. Unknown `0xFF59` commands were not tried: one may well unpair or erase |
| Battery | none: no Battery Strength usage, `GET_REPORT` stalls, nothing arrives when the keyboard (re)sends `A7`. Not on stock either — the stock configurator reads the level from the keyboard through the tunnel |
| Updates | no known way: the stock configurator's OTA commands (`AA 80`–`86`) are passed through to the keyboard, the update manifest has no dongle images. The name is baked into the dongle, the stock keyboard firmware doesn't send it |

The same VID/PID is used by dozens of brands (Aula, Ajazz, Womier, Fantech, … — see the device list in
web.io.vision), so it is an OEM dongle with per-brand names. The chip is unknown; there was no way to
dump the firmware over USB.

**Pairing.** The MCU side is all there is to it: `A6 30`, `A6 51`, then `5B 33` while the module searches
and `5B 32` once linked. The radio link itself is proprietary and undocumented. Comparable designs
(Panchip PAN1080, WCH CH58x) pair on a fixed public address and channel list during a few-second window,
after which the dongle keeps the module's unique address and both hop over a channel list with
retransmissions — an Enhanced ShockBurst-like link. Likely the same here, but unverified.

## Related modules

**Hangsheng** (hangshengkeji, used by Epomaker / Linker boards) publishes a QMK driver for a module of the
same family: [`keyboards/linker/wireless/module.h`](https://github.com/hangshengkeji/qmk_firmware/blob/tri-mode/keyboards/linker/wireless/module.h),
branch `tri-mode`. Same framing and `a\r\n` ACK, same `A1`–`A6` reports and mode codes (`A6 11`, `30`,
`31`–`35`, `51`), `5A` for host LEDs, `5B` for link state. The firmware generation differs:

| | Hangsheng | IO Type 84 Pro |
|---|---|---|
| Raw HID tunnel | `AF 61 <len> …` / `AF 60 <len> …` | `81` / `91` |
| Battery | the module measures it: `A6 53` → `5C <level>` | the MCU sends `A7 <percent>` |
| Link state `5B` | `31` pairing, `32` connected, `33` disconnected | `33` pairing, `32` connected, `31` disconnected |
| Dongle identity | set from the keyboard: `AB` manufacturer, `AC` product, `AD` VID/PID | fixed in the dongle (`AB` is used differently) |
| Extra `A6` codes | `52` clear pairings, `55`–`58` module sleep, `62`/`63` forced pairing, `64`–`66` charging state, `70` module firmware version | only `00`, `11`, `30`–`35`, `51`, `55` confirmed |

Their battery goes to the host over Bluetooth only as well.

**WCH** sells a generic tri-mode module with a published but different protocol (`55` header + length,
`81`–`8B` reports, `40`–`4A` control, `F0` ACK):
[scheme](https://www.cnblogs.com/azou/p/17503122.html), [protocol](https://www.cnblogs.com/azou/p/17195990.html).
It also lists battery display for Bluetooth only.
