#!/usr/bin/env python3
#
# Copyright © 2017 Red Hat, Inc.
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
# Device data verification script
#

import argparse
import configparser
import pathlib
import re
import sys
import traceback

from typing import Collection, TypeVar


T = TypeVar("T")


def assertIn(element: T, collection: Collection[T]):
    if element not in collection:
        raise ValueError(f"{element} must be in {collection}")


def assertNotIn(element: T, collection: Collection[T]):
    if element in collection:
        raise ValueError(f"{element} must not be in {collection}")


def check_match_str(string: str):
    bustypes = ["usb", "bluetooth"]

    matches = string.split(";")
    for match in matches:
        if not match:  # empty string if trailing ;
            continue

        parts = match.split(":")
        assert len(parts) == 3
        assertIn(parts[0], bustypes)
        vid = parts[1]
        assert vid == f"{int(vid, 16):04x}"
        pid = parts[2]
        assert pid == f"{int(pid, 16):04x}"


def check_devicetype_str(string):
    permitted_types = ["mouse", "keyboard", "other"]
    assertIn(string, permitted_types)


def check_section_device(section: configparser.SectionProxy):
    required_keys = ["Name", "Driver", "DeviceMatch", "DeviceType"]

    for key in section:
        assertIn(key, required_keys)

    for r in required_keys:
        assertIn(r, section)

    check_devicetype_str(section["DeviceType"])

    check_match_str(section["DeviceMatch"])


def check_dpi_range_str(string: str):
    m = re.search("^([0-9]+):([0-9]+)@([0-9.]+)$", string)
    assert m is not None
    min = int(m.group(1))
    max = int(m.group(2))
    steps = float(m.group(3))

    assert min >= 0 and min <= 400
    assert max >= 2000 and max <= 36000
    assert steps > 0 and steps <= 100

    if int(steps) == steps:
        steps = int(steps)

    assert string == f"{min}:{max}@{steps}"


def check_dpi_list_str(string: str):
    entries = string.split(";")
    # Remove possible empty last entry if trailing with a ;
    if not entries[len(entries) - 1]:
        entries = entries[:-1]

    for idx, entry in enumerate(entries):
        dpi = int(entry)
        assert dpi >= 0 and dpi <= 12000
        if idx > 0:
            prev = entries[idx - 1]
            prev_dpi = int(prev)
            assert dpi > prev_dpi


def check_profile_type_str(string: str):
    types = ["G9", "G500", "G700"]
    assertIn(string, types)


def check_section_asus(section: configparser.SectionProxy):
    permitted_keys = (
        "ButtonMapping",
        "Buttons",
        "DpiRange",
        "Dpis",
        "Leds",
        "Profiles",
        "Quirks",
        "Wireless",
    )
    for key in section:
        assertIn(key, permitted_keys)

    try:
        check_dpi_range_str(section["DpiRange"])
    except KeyError:
        # No such section - not an error.
        pass

    try:
        quirks = (
            "DOUBLE_DPI",
            "STRIX_PROFILE",
        )
        for quirk in section["Quirks"].split(";"):
            assertIn(quirk, quirks)
    except KeyError:
        # No such section - not an error.
        pass


def check_section_hidpp10(section: configparser.SectionProxy):
    permitted = [
        "Profiles",
        "ProfileType",
        "DpiRange",
        "DpiList",
        "DeviceIndex",
        "Leds",
    ]
    for key in section:
        assertIn(key, permitted)

    try:
        nprofiles = int(section["Profiles"])
        # 10 is arbitrarily chosen
        assert nprofiles > 0 and nprofiles < 10
    except KeyError:
        # No such section - not an error.
        pass

    try:
        index = int(section["DeviceIndex"], 16)
        assert index > 0 and index <= 0xFF
    except KeyError:
        # No such section - not an error.
        pass

    try:
        check_dpi_range_str(section["DpiRange"])
        assertNotIn("DpiList", section.keys())
    except KeyError:
        # No such section - not an error.
        pass

    try:
        check_dpi_list_str(section["DpiList"])
        assertNotIn("DpiRange", section.keys())
    except KeyError:
        # No such section - not an error.
        pass

    try:
        check_profile_type_str(section["ProfileType"])
    except KeyError:
        # No such section - not an error.
        pass

    try:
        leds = int(section["Leds"])
        # 10 is arbitrarily chosen
        assert leds > 0 and leds < 10
    except KeyError:
        # No such section - not an error.
        pass


def check_section_hidpp20(section: configparser.SectionProxy):
    permitted = ["Buttons", "DeviceIndex", "Leds", "ReportRate", "Quirk"]
    for key in section:
        assertIn(key, permitted)

    try:
        index = int(section["DeviceIndex"], 16)
        assert index > 0 and index <= 0xFF
    except KeyError:
        # No such section - not an error.
        pass


def check_section_steelseries(section: configparser.SectionProxy):
    permitted_keys = (
        "Buttons",
        "DeviceVersion",
        "DpiList",
        "DpiRange",
        "Leds",
        "MacroLength",
        "Quirk",
    )
    for key in section:
        assertIn(key, permitted_keys)

    try:
        check_dpi_list_str(section["DpiList"])
        assertNotIn("DpiRange", section.keys())
    except KeyError:
        # No such section - not an error.
        pass

    try:
        check_dpi_range_str(section["DpiRange"])
        assertNotIn("DpiList", section.keys())
    except KeyError:
        # No such section - not an error.
        pass

    try:
        quirks = ("Rival100", "SenseiRAW")
        assertIn(section["Quirk"], quirks)
    except KeyError:
        # No such section - not an error.
        pass


def check_section_driver(driver: str, section: configparser.SectionProxy):
    if driver == "asus":
        check_section_asus(section)
        return

    if driver == "hidpp10":
        check_section_hidpp10(section)
        return

    if driver == "hidpp20":
        check_section_hidpp20(section)
        return

    if driver == "steelseries":
        check_section_steelseries(section)
        return

    raise ValueError(f"Unsupported driver section {driver}")


def validate_data_file_name(path: str):
    # Matching any of the characters in the regular expression will throw an
    # error. Currently only tests the square brackets [], parentheses, and curly
    # braces.
    illegal_characters_regex = "([\\[\\]\\{\\}\\(\\)])"
    found_characters = re.findall(illegal_characters_regex, path)
    if found_characters:
        raise ValueError(
            "data file name '{}' contains illegal characters: '{}'".format(
                path, "".join(found_characters)
            )
        )


def parse_data_file(path: str):
    print(f"Parsing file {path}")
    data = configparser.ConfigParser(strict=True)
    # Don't convert to lowercase
    data.optionxform = lambda option: option
    data.read(path)

    assertIn("Device", data.sections())
    check_section_device(data["Device"])

    driver = data["Device"]["Driver"]
    driver_section = f"Driver/{driver}"

    permitted_sections = ["Device", driver_section]
    # FIXME: remove this once `sinowealth` driver has a better way of defining data.
    if driver == "sinowealth":
        print("Skipping `Driver` section check for a `sinowealth` device")
    else:
        for s in data.sections():
            assertIn(s, permitted_sections)

    if data.has_section(driver_section):
        check_section_driver(driver, data[driver_section])


def main() -> None:
    is_error = False

    parser = argparse.ArgumentParser(description="Device data-file checker")
    parser.add_argument("directory")
    args = parser.parse_args()
    for path in pathlib.Path(args.directory).glob("*.device"):
        path_str = str(path)
        try:
            validate_data_file_name(path_str)
            parse_data_file(path_str)
        except ValueError as e:
            is_error = True
            traceback.print_exception(e, file=sys.stdout)

    if is_error:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
