# Flashing

The keyboard has a Sonix **SN32F290** MCU with a mask-ROM USB bootloader. It cannot be
bricked by a bad firmware image in normal use: the bootloader lives in ROM and the
firmware has several ways back into it (below). All you need is
[SonixFlasherC](https://github.com/SonixQMK/SonixFlasherC) and the udev rule from `udev/`.

| USB id | What it is |
|---|---|
| `0c45:8009` | the keyboard (stock firmware **and** this QMK firmware) |
| `0c45:7140` | the SN32F290 ROM bootloader |

> [!IMPORTANT]
> Put the **mode switch on USB** before flashing. In Bluetooth / 2.4 GHz modes the
> keyboard disconnects from USB.

## First flash (from the stock firmware)

The stock firmware accepts the same "reboot to bootloader" request the official web
configurator uses, so no buttons are needed:

```sh
./flash.sh                                   # does all of the below
```

or by hand:

```sh
sonixflasher -v 0c45/8009 -r hfd             # stock -> ROM bootloader (nothing is erased)
sonixflasher -v 0c45/8009 -f qmk_firmware/io_type84_pro_default.bin
```

When entered from the stock firmware, the ROM bootloader keeps answering on the keyboard's
USB id (`0c45:8009`) instead of re-enumerating as `0c45:7140`, hence `-v 0c45/8009`.

## Updating QMK

1. Enter the bootloader, either way:
   - **Fn+Enter → SYSTEM → BOOTLOADER → Fn+Enter**, or
   - unplug the cable, **hold Esc**, plug it back in.
2. `./flash.sh path/to/firmware.bin` (or `sonixflasher -v 0c45/7140 -f firmware.bin`).

**Known quirk:** occasionally the chip boots straight back into the bootloader after
flashing. `sonixflasher -v 0c45/7140 -u` (which `flash.sh` runs automatically) or a replug fixes it.

## Recovery

| Situation | What to do |
|---|---|
| Firmware hangs, keys dead | It drops into the bootloader by itself after ~4 s (watchdog). Flash again. |
| Firmware crashes on start | Unplug, **hold Esc**, plug in → bootloader. The Esc check runs before anything else starts. |
| Stuck in the bootloader | `sonixflasher -v 0c45/7140 -u`, or flash again. |
| Nothing works | Not seen so far. The last resort is SWD with a CMSIS-DAP / ST-Link probe (nRESET is P3.7; see [hardware.md](hardware.md)). |

The "safe" keymap (`make io/type84_pro:safe`) is a minimal build without the display
— handy for bring-up and testing the recovery paths (Fn+Backspace hangs the firmware on
purpose to test the watchdog).

## Going back to stock

The ROM bootloader cannot **read** flash, so you can't back up the factory image from
the keyboard itself — use the official update instead:

1. Download the official firmware (Intel HEX). The vendor's web configurator
   ([web.io.vision](https://web.io.vision)) serves it from the site root; at the time of
   writing that is
   `https://web.io.vision/IO-Type-84-Pro-Wireless-Black_V1.32.hex` (v1.32 was used during development).
2. Convert it to a 256 KiB binary:
   ```sh
   arm-none-eabi-objcopy -I ihex -O binary --gap-fill 0xff IO-Type-84-Pro-Wireless-Black_V1.32.hex stock.bin
   ```
   (the image must end at `0x40000` — it contains the `0xAAAA5555` "firmware present" flag at `0x3FFFC`).
3. Enter the bootloader from QMK and flash it: `./flash.sh stock.bin`.

Settings are reset to factory defaults, exactly like an official update.

## Permissions

`udev/70-io-type84-pro.rules` gives the logged-in user access to the keyboard and the
bootloader (USB and hidraw), so neither `sonixflasher` nor `qmk console` needs sudo:

```sh
sudo cp udev/70-io-type84-pro.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

Replug the keyboard afterwards.
