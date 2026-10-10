# IO Type 84 Pro Wireless

![IO Type 84 Pro Wireless UI](../../../docs/images/ui-mockup.png)

75% wireless keyboard by IO by Red Square with a 320×172 LCD, a light bar under it,
per-key RGB, a removable knob and a Bluetooth / 2.4 GHz radio.

* Keyboard Maintainer: [backflipinc](https://github.com/backflipinc) — sources: [backflipinc/io-type84-pro-qmk](https://github.com/backflipinc/io-type84-pro-qmk)
* Hardware Supported: IO Type 84 Pro Wireless (USB `0c45:8009`, Sonix SN32F290)
* Hardware Availability: IO by Red Square

Requires the [SonixQMK](https://github.com/SonixQMK/qmk_firmware) fork plus the core patches
from this repository (`./setup.sh` does both).

Make example for this keyboard (after setting up your build environment):

    make io/type84_pro:default    # with Vial; io/type84_pro:novial without

Flashing example (SonixFlasherC, keyboard in bootloader mode):

    sonixflasher -v 0c45/7140 -f io_type84_pro_default.bin

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the
[make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information.
Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 3 ways:

* **Menu**: Fn+Enter → SYSTEM → BOOTLOADER → Fn+Enter
* **Bootmagic reset**: hold Esc while plugging in the cable
* **From the stock firmware**: `sonixflasher -v 0c45/8009 -r hfd`

The mode switch must be on USB.
