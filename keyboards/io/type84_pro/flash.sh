#!/usr/bin/env bash
# Flash a firmware image with SonixFlasherC (https://github.com/SonixQMK/SonixFlasherC).
#
# Usage: ./flash.sh [firmware.bin]   (default: io_type84_pro_default.bin in the qmk-armed root)
#
# Over USB the script reboots the keyboard into its bootloader itself (QMK: tools/bootloader.py,
# stock: the stock request). Otherwise: Fn+Enter -> SYSTEM -> BOOTLOADER, or hold Esc while plugging in.
# The mode switch must be on USB (in BT/2.4G the keyboard disconnects from USB).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../../../io_type84_pro_default.bin}"
FLASHER="${SONIXFLASHER:-sonixflasher}"

KBD="0c45/8009"   # keyboard (stock and QMK)
BOOT="0c45/7140"  # SN32F290 ROM bootloader

[ -f "$BIN" ] || { echo "firmware not found: $BIN" >&2; exit 1; }
command -v "$FLASHER" >/dev/null || { echo "sonixflasher not found (set SONIXFLASHER=/path/to/sonixflasher)" >&2; exit 1; }

present() { lsusb | grep -qi "${1/\//:}"; }

if present "$BOOT"; then
    DEV="$BOOT"
elif present "$KBD"; then
    echo "Keyboard found, asking it to reboot into the bootloader..."
    python3 "$HERE/tools/bootloader.py" 2>/dev/null || "$FLASHER" -v "$KBD" -r hfd >/dev/null 2>&1 || true
    for _ in $(seq 10); do present "$BOOT" && break; sleep 0.3; done
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
