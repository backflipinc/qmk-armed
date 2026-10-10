#!/usr/bin/env bash
# Flash a firmware image with SonixFlasherC (https://github.com/SonixQMK/SonixFlasherC).
#
# Usage: ./flash.sh [firmware.bin]   (default: qmk_firmware/io_type84_pro_default.bin)
#
# Put the keyboard into its bootloader first:
#   - from QMK:   Fn+Enter -> SYSTEM -> BOOTLOADER -> Fn+Enter, or hold Esc while plugging in;
#   - from stock: nothing to do, this script sends the stock "reboot to bootloader" request.
# The mode switch must be on USB (in BT/2.4G the keyboard disconnects from USB).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/qmk_firmware/io_type84_pro_default.bin}"
FLASHER="${SONIXFLASHER:-sonixflasher}"

KBD="0c45/8009"   # keyboard (stock and QMK)
BOOT="0c45/7140"  # SN32F290 ROM bootloader

[ -f "$BIN" ] || { echo "firmware not found: $BIN" >&2; exit 1; }
command -v "$FLASHER" >/dev/null || { echo "sonixflasher not found (set SONIXFLASHER=/path/to/sonixflasher)" >&2; exit 1; }

present() { lsusb | grep -qi "${1/\//:}"; }

if present "$BOOT"; then
    DEV="$BOOT"
elif present "$KBD"; then
    echo "Keyboard found, asking stock firmware to reboot into the bootloader..."
    echo "(on QMK firmware this does nothing — use the menu or hold Esc while plugging in)"
    "$FLASHER" -v "$KBD" -r hfd >/dev/null 2>&1 || true
    sleep 1
    # entered from stock, the ROM keeps answering on the keyboard's USB id
    if present "$BOOT"; then DEV="$BOOT"; else DEV="$KBD"; fi
else
    echo "Waiting for the bootloader (0c45:7140)... put the keyboard into bootloader mode now."
    until present "$BOOT"; do sleep 0.5; done
    DEV="$BOOT"
fi

echo "Flashing $(basename "$BIN") via $DEV"
"$FLASHER" -v "$DEV" -f "$BIN"

# Known quirk: sometimes the chip comes back up in the ROM bootloader after flashing.
sleep 3
if present "$BOOT"; then
    echo "Still in the bootloader, rebooting to the firmware"
    "$FLASHER" -v "$BOOT" -u || true
fi
echo "Done."
