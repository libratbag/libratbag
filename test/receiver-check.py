#!/usr/bin/env python3
#
# Copyright © 2018 Red Hat, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

import argparse
import os
import pathlib
import sys
import configparser

# see the IDs from
# https://github.com/torvalds/linux/blob/master/drivers/hid/hid-ids.h#L772
# https://github.com/torvalds/linux/blob/master/drivers/hid/hid-logitech-dj.c#L1826
# https://pwr-solaar.github.io/Solaar/devices.html
logitech_receivers = [
    0xC50C,  # USB_DEVICE_ID_S510_RECEIVER
    0xC517,  # USB_DEVICE_ID_S510_RECEIVER_2
    0xC512,  # USB_DEVICE_ID_LOGITECH_CORDLESS_DESKTOP_LX500
    0xC513,  # USB_DEVICE_ID_MX3000_RECEIVER
    0xC51B,  # USB_DEVICE_ID_LOGITECH_27MHZ_MOUSE_RECEIVER
    0xC52B,  # USB_DEVICE_ID_LOGITECH_UNIFYING_RECEIVER
    0xC52F,  # USB_DEVICE_ID_LOGITECH_NANO_RECEIVER
    0xC532,  # USB_DEVICE_ID_LOGITECH_UNIFYING_RECEIVER_2
    0xC534,  # USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_2
    0xC539,  # USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1
    0xC53F,  # USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1_1
    0xC53A,  # USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_POWERPLAY
    0xC545,  # USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1_2
    0xC547,  # USB_DEVICE_ID_LOGITECH_NANO_RECEIVER_LIGHTSPEED_1_3
    0xC548,  # Bolt receiver
]
RECEIVERS = [f"usb:046d:{r:04x}" for r in logitech_receivers]


def parse_data_file(path):
    data = configparser.ConfigParser(strict=True)
    # Don't convert to lowercase
    data.optionxform = lambda option: option
    data.read(path)

    matches = data["Device"]["DeviceMatch"]
    return matches.split(";")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Checks that no unifying receiver ID is in the device files"
    )
    parser.add_argument("directory")
    args = parser.parse_args()
    receiver_found = False
    for path in pathlib.Path(args.directory).glob("*.device"):
        matches = parse_data_file(path)
        fname = os.path.basename(path)
        for m in matches:
            if m in RECEIVERS:
                print(f"Receiver ID {m} found in file {fname}")
                receiver_found = True

    if receiver_found:
        sys.exit(1)
