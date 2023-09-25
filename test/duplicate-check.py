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


def parse_data_file(path):
    data = configparser.ConfigParser(strict=True)
    # Don't convert to lowercase
    data.optionxform = lambda option: option
    data.read(path)

    matches = data["Device"]["DeviceMatch"]
    return matches.split(";")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Device duplicate match checker")
    parser.add_argument("directory")
    args = parser.parse_args()
    device_matches = {}
    duplicates = False
    for path in pathlib.Path(args.directory).glob("*.device"):
        matches = parse_data_file(path)
        fname = os.path.basename(path)
        for m in matches:
            if m in device_matches:
                print(f"Duplicate DeviceMatch={m} in {fname} and {device_matches[m]}")
                duplicates = True
            device_matches[m] = fname

    if duplicates:
        sys.exit(1)
