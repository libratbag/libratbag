#!/bin/sh

if [ -z "$MESON_SOURCE_ROOT" ]; then
	echo >&2 "Expected \$MESON_SOURCE_ROOT to be set. Are you sure you are running this through ninja?"
	exit 100
fi

cd "$MESON_SOURCE_ROOT" || exit 101

files=$(git ls-files "*.py" "*.py.in")
if [ -z "$files" ]; then
	echo >&2 "Git didn't find any files"
	exit 77
fi

command -v ruff >/dev/null 2>&1 || {
	echo >&2 "ruff is not installed"
	exit 77
}

ruff check $files
