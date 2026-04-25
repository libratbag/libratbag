#!/bin/bash
cd /workspaces/libratbag
rm -rf build
meson setup build --prefix=/usr/local -Dsystemd=true
if [ $? -eq 0 ]; then
    echo "Meson setup successful"
    ninja -C build
    if [ $? -eq 0 ]; then
        echo "Ninja build successful"
        sudo ninja -C build install
        if [ $? -eq 0 ]; then
            echo "Installation successful"
        else
            echo "Installation failed"
        fi
    else
        echo "Ninja build failed"
    fi
else
    echo "Meson setup failed"
fi