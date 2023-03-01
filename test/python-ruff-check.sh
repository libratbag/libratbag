#!/bin/sh

if [ -z "$MESON_SOURCE_ROOT" ]; then
	echo "Expected \$MESON_SOURCE_ROOT to be set. Are you sure you are running this through ninja?"
	exit 100
fi

cd "$MESON_SOURCE_ROOT" || exit 101

ruff check $(git ls-files "*.py" "*.py.in")
