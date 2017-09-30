#!/usr/bin/env python3
# vim: set expandtab shiftwidth=4 tabstop=4:
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
import os
import configparser

# Set on commandline with --svg-dir
svg_dirs = []


def check_svg_str(string):
    assert(string.endswith('.svg'))

    svg_file_found = False
    for svg_dir in svg_dirs:
        files = os.listdir(svg_dir)
        if string in files:
            svg_file_found = True
            break
    assert(svg_file_found)


def check_match_str(string):
    bustypes = ['usb', 'bluetooth']

    matches = string.split(';')
    for match in matches:
        if not match:  # empty string if trailing ;
            continue

        parts = match.split(':')
        assert(len(parts) == 3)
        assert(parts[0] in bustypes)
        vid = parts[1]
        assert(vid == '{:04x}'.format(int(vid, 16)))
        pid = parts[2]
        assert(pid == '{:04x}'.format(int(pid, 16)))


def check_section_device(section):
    required_keys = ['Name', 'Driver', 'DeviceMatch']
    permitted_keys = required_keys + ['Svg']

    for key in section.keys():
        assert(key in permitted_keys)

    for r in required_keys:
        assert(r in section)

    try:
        check_svg_str(section['Svg'])
    except KeyError:
        pass

    check_match_str(section['DeviceMatch'])


def check_dpi_range_str(string):
    import re

    m = re.search('^([0-9]+):([0-9]+)@([0-9\.]+)$', string)
    assert(m is not None)
    min = int(m.group(1))
    max = int(m.group(2))
    steps = float(m.group(3))

    assert(min >= 0 and min <= 400)
    assert(max >= 2000 and max <= 12000)
    assert(steps > 0 and steps <= 100)

    if int(steps) == steps:
        steps = int(steps)

    assert(string == '{}:{}@{}'.format(min, max, steps))


def check_dpi_list_str(string):
    entries = string.split(';')
    # Remove possible empty last entry if trailing with a ;
    if not entries[len(entries) - 1]:
        entries = entries[:-1]

    for idx, entry in enumerate(entries):
        dpi = int(entry)
        assert(dpi >= 0 and dpi <= 12000)
        if idx > 0:
            prev = entries[idx - 1]
            prev_dpi = int(prev)
            assert(dpi > prev_dpi)


def check_profile_type_str(string):
    types = ['G9', 'G500', 'G700']
    assert(string in types)


def check_section_hidpp10(section):
    permitted = ['Profiles', 'ProfileType', 'DpiRange', 'DpiList', 'DeviceIndex', 'Leds']
    for key in section.keys():
        assert(key in permitted)

    try:
        nprofiles = int(section['Profiles'])
        # 10 is arbitrarily chosen
        assert(nprofiles > 0 and nprofiles < 10)
    except KeyError:
        pass

    try:
        index = int(section['DeviceIndex'])
        # 10 is arbitrarily chosen
        assert(index > 0 and index < 10)
    except KeyError:
        pass

    try:
        check_dpi_range_str(section['DpiRange'])
        assert('DpiList' not in section.keys())
    except KeyError:
        pass

    try:
        check_dpi_list_str(section['DpiList'])
        assert('DpiRange' not in section.keys())
    except KeyError:
        pass

    try:
        check_profile_type_str(section['ProfileType'])
    except KeyError:
        pass

    try:
        leds = int(section['Leds'])
        # 10 is arbitrarily chosen
        assert(leds > 0 and leds < 10)
    except KeyError:
        pass


def check_section_hidpp20(section):
    permitted = ['DeviceIndex']
    for key in section.keys():
        assert(key in permitted)

    try:
        index = int(section['DeviceIndex'])
        # 10 is arbitrarily chosen
        assert(index > 0 and index < 10)
    except KeyError:
        pass


def check_section_driver(driver, section):
    if driver == 'hidpp10':
        check_section_hidpp10(section)
    elif driver == 'hidpp20':
        check_section_hidpp20(section)
    else:
        assert('Unsupported driver section {}'.format(driver))


def parse_data_file(path):
    print('Parsing file {}'.format(path))
    data = configparser.ConfigParser(strict=True)
    # Don't convert to lowercase
    data.optionxform = lambda option: option
    data.read(path)

    assert('Device' in data.sections())
    check_section_device(data['Device'])

    driver = data['Device']['Driver']
    driver_section = 'Driver/{}'.format(driver)

    permitted_sections = ['Device', driver_section]
    for s in data.sections():
        assert(s in permitted_sections)

    if data.has_section(driver_section):
        check_section_driver(driver, data[driver_section])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Device data-file checker")
    parser.add_argument('file', nargs='+')
    parser.add_argument('--svg-dir', metavar='dir', action='append',
                        type=str,
                        help='Directory to check for SVG files (may be given multiple times)')
    args = parser.parse_args()
    svg_dirs = args.svg_dir
    for path in args.file:
        parse_data_file(path)
