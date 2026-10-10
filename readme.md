# qmk-armed

A [QMK](https://qmk.fm) fork built on [SonixQMK](https://github.com/SonixQMK/qmk_firmware) (`sn32_develop`), with
[Vial](https://get.vial.today) merged in and its own companion app, [**Vial Armed**](https://github.com/backflipinc/vial-gui-armed).

It started as the firmware for the IO Type 84 Pro Wireless and grew enough core patches to become a fork of its own.

## What it adds to QMK

| | |
|---|---|
| 🧩 **Vial** | The core of [vial-qmk](https://github.com/vial-kb/vial-qmk): live remapping, macros, combos, tap dance, key overrides, QMK settings |
| 🎛️ **qmk-armed protocol** | Raw HID channel for what VIA / Vial can't set: every RGB Matrix effect including custom ones, a second colour, Fn colour, light bar, auto-sleep. Each keyboard reports its own features — [docs/armed.md](docs/armed.md) |
| 🔋 **USB battery** | Battery level and charging state over USB HID (`HID_BATTERY_ENABLE = yes`), picked up by Linux and UPower |

[Vial Armed](https://github.com/backflipinc/vial-gui-armed) detects a qmk-armed keyboard by the protocol, not by its
name, and shows the options that keyboard reports. Regular Vial keeps working too.

## Keyboards

| Keyboard | MCU | Features |
|---|---|---|
| [IO Type 84 Pro Wireless](keyboards/io/type84_pro/) | Sonix SN32F290 | LCD HUD and menu, Bluetooth / 2.4 GHz, light bar, two-colour effects, SnapTap, auto-sleep |

Any other keyboard in the tree builds as usual; with VIA or Vial enabled it also gets the RGB Matrix part
of the protocol.

## Building

You need `git`, `make`, Python 3 with [`qmk`](https://pypi.org/project/qmk/) and an `arm-none-eabi-gcc` toolchain.

```sh
git clone --recurse-submodules --shallow-submodules https://github.com/backflipinc/qmk-armed.git
cd qmk-armed
make io/type84_pro:default
```

Ready-made firmware is attached to every [release](https://github.com/backflipinc/qmk-armed/releases).
Flashing and everything else keyboard-specific is in the keyboard's readme.

## Branches

| Branch | |
|---|---|
| `armed` | the fork, default |
| `sn32_develop` | SonixQMK as forked |

## Credits

- [QMK](https://github.com/qmk/qmk_firmware) and [SonixQMK](https://github.com/SonixQMK/qmk_firmware) — the firmware and the SN32 port
- [Vial](https://get.vial.today) / [vial-qmk](https://github.com/vial-kb/vial-qmk) — live remapping
- [ChibiOS](https://www.chibios.org) / ChibiOS-Contrib — the RTOS and HAL

## License

GPL-2.0-or-later, like QMK — see [LICENSE](LICENSE).
