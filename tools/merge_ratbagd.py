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
from typing import TextIO


def write_ratbagctl(
    output_file: TextIO, ratbagctl_path: str, ratbagd_path: str, version_string: str
) -> None:
    with open(ratbagctl_path, encoding="utf-8") as ratbagctl, open(
        ratbagd_path, encoding="utf-8"
    ) as ratbagd:
        for line in ratbagctl.readlines():
            if line.startswith("from ratbagd import "):
                headers = True
                for r in ratbagd.readlines():
                    if not r.startswith("#") and r.strip():
                        headers = False
                    if not headers:
                        output_file.write(r)
                continue

            if "@version@" in line:
                output_file.write(line.replace("@version@", version_string))
                continue

            output_file.write(line)


def main() -> None:
    parser = argparse.ArgumentParser(description="merge ratbagd.py into ratbagctl")
    parser.add_argument("ratbagctl", action="store")
    parser.add_argument("ratbagd", action="store")
    parser.add_argument("--output", action="store")
    parser.add_argument("--version", action="store", default="git_master")
    ns = parser.parse_args()
    if ns.output:
        with open(ns.output, "w", encoding="utf-8") as output_file:
            st = os.stat(ns.output)
            os.chmod(ns.output, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
            write_ratbagctl(output_file, ns.ratbagctl, ns.ratbagd, ns.version)


if __name__ == "__main__":
    main()
