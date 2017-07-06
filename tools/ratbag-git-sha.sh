#!/bin/sh

GIT_DIR="$MESON_SOURCE_ROOT/.git"
export GIT_DIR

git log --format="format:%h" -1 2>/dev/null

if test $? -ne 0; then
	echo "unknown"
fi
