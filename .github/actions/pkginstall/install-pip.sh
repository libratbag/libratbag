#!/bin/bash -x

# If called without arguments, just skip the rest
if [[ -z "$@" ]]; then
	exit
fi

python -m pip install --upgrade pip
python -m pip install --upgrade "$@"
