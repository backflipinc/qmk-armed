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
| `A6` | `52`, `53` | ⚠️ unknown |
| `A7` | 0–100 | battery level, % — the module reports it to the host |
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
| `5C vv SS` | ⚠️ accepted, no action in the stock firmware |
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
