#!/usr/bin/env python3
"""Reboot the keyboard (QMK, USB mode) into its ROM bootloader via the qmk-armed raw HID command."""
import glob
import os
import sys

VID, PID = 0x0C45, 0x8009
RAW_HID_PAGE = b"\x06\x60\xff"  # usage page 0xFF60
ARMED_COMMAND_ID, ARMED_BOOTLOADER = 0xAD, 17


def find_node():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        with open(d + "/device/uevent") as f:
            if f"HID_ID=0003:{VID:08X}:{PID:08X}" not in f.read():
                continue
        with open(d + "/device/report_descriptor", "rb") as f:
            if f.read().startswith(RAW_HID_PAGE):
                return "/dev/" + os.path.basename(d)
    return None


def main():
    node = find_node()
    if not node:
        sys.exit("keyboard raw HID interface not found (is the mode switch on USB?)")
    fd = os.open(node, os.O_RDWR)
    os.write(fd, bytes([0, ARMED_COMMAND_ID, ARMED_BOOTLOADER]).ljust(33, b"\0"))
    os.close(fd)
    print("bootloader requested")


if __name__ == "__main__":
    main()
