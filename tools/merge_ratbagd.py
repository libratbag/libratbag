#!/usr/bin/env python3
#
# Copyright 2017 Red Hat, Inc.
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

import argparse
import os
import stat
import sys


def print_ratbagctl(ratbagctl_path, ratbagd_path, version_string):
    with open(ratbagctl_path, 'r', encoding='utf-8') as ratbagctl, open(ratbagd_path, 'r', encoding='utf-8') as ratbagd:
        for line in ratbagctl.readlines():
            if line.startswith("from ratbagd import "):
                headers = True
                for r in ratbagd.readlines():
                    if not r.startswith('#') and r.strip():
                        headers = False
                    if not headers:
                        print(r.rstrip('\n'))
            else:
                if '@version@' in line:
                    line = line.replace('@version@', version_string)
                print(line.rstrip('\n'))


def main(argv):
    parser = argparse.ArgumentParser(description="merge ratbagd.py into ratbagctl")
    parser.add_argument("ratbagctl", action='store')
    parser.add_argument("ratbagd", action='store')
    parser.add_argument("--output", action="store")
    parser.add_argument("--version", action="store", default="git_master")
    ns = parser.parse_args(sys.argv[1:])
    if ns.output:
        ns.output_file = open(ns.output, 'w', encoding='utf-8')
        st = os.stat(ns.output)
        os.chmod(ns.output, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        sys.stdout = ns.output_file
    print_ratbagctl(ns.ratbagctl, ns.ratbagd, ns.version)
    try:
        ns.output_file.close()
    except AttributeError:
        pass


if __name__ == "__main__":
    main(sys.argv[1:])
