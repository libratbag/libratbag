#!/bin/env python3
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#

import bs4
import os
import re
import sys


class Bug:
    def __init__(self):
        self.line_number = None
        self.cfile = None
        self.sa_type = None
        self.sa_descr = None


def main(argv):
    if len(argv) < 2:
        print(f"usage {argv[0]} PATH")
        sys.exit(1)

    scanbuild_path = argv[1]

    ignored = []
    bugs = []

    line_re = re.compile(r"line (\d+), column (\d+)")

    for root, _dirs, files in os.walk(scanbuild_path):
        for filename in files:
            if not filename.startswith("report"):
                continue

            with open(os.path.join(root, filename)) as f:
                soup = bs4.BeautifulSoup(f, "html.parser")

                # the first table is the summary
                summary = soup.table
                bug = Bug()

                # iterate over the table
                for tr in summary("tr"):
                    if "File:" in tr.contents[0].string:
                        bug.cfile = os.path.abspath(tr.contents[1].string)
                    else:
                        bug.sa_type = tr.contents[0].string.rstrip(":").lower()
                        for s in tr.contents[1].strings:
                            # retrieve the line number and the description
                            m = line_re.match(s)
                            if m is None:
                                bug.sa_descr = s
                            else:
                                bug.line_number = int(m.group(1))

                # retrieve the html line corresponding to the code
                code = soup.find("td", id=f"LN{bug.line_number}").parent

                # fetching any comments in this line
                try:
                    comments = code.find("span", class_="comment").string
                except AttributeError:
                    # no comments on the line
                    bugs.append(bug)
                else:
                    if "ignore_clang_sa_" in comments:
                        ignored.append(bug)
                    else:
                        bugs.append(bug)

    if len(ignored) > 0:
        print(f"{len(ignored)} bugs are ignored:")
        for b in ignored:
            print(f"    {b.sa_type}: {b.sa_descr} at {b.cfile}:{b.line_number}")

        if len(bugs) > 0:
            print()

    if len(bugs) > 0:
        print(f"ERROR: {len(bugs)} bugs found:")
        for b in bugs:
            print(f"  * {b.sa_type.upper()}: {b.sa_descr} at {b.cfile}:{b.line_number}")
        sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":
    main(sys.argv)
