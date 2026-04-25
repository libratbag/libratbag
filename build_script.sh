#!/bin/bash
cd /workspaces/libratbag
meson setup build
ninja -C build