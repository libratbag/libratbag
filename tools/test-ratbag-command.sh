#!/bin/bash
#
# Script to test ratbag-command for valid argument parsing. Must be run
# against a device file.
#

declare -A cmds

command="`dirname $0`/ratbag-command"
device=$1
if ! [[ -e "$device" ]]; then
	echo "Invalid device path or path missing"
	exit 1
fi

# no device argument
cmds[0]="list"

for i in "${cmds[@]}"; do
	echo "Testing arguments '$i $device'"
	eval $command "$i"
	if [[ $? == 2 ]]; then
		echo "Invalid command: $i"
		exit 1
	fi
	sleep 1
done

# commands with device argument
cmds[0]="info"
cmds[1]="switch-etekcity"
cmds[2]="profile active get"
cmds[3]="profile active set 0"
cmds[4]="resolution active get"
cmds[5]="resolution active set 0"
cmds[6]="dpi get"
cmds[7]="dpi set 800"
cmds[8]="rate get"
cmds[9]="rate set 500"
cmds[10]="profile 0 resolution active get"
cmds[11]="profile 0 resolution active set 0"
cmds[12]="resolution 0 dpi get"
cmds[13]="resolution 0 dpi set 800"
cmds[14]="resolution 0 rate get"
cmds[15]="resolution 0 rate set 500"
cmds[16]="profile 0 resolution 0 dpi get"
cmds[17]="profile 0 resolution 0 dpi set 800"
cmds[18]="profile 0 resolution 0 rate get"
cmds[19]="profile 0 resolution 0 rate set 500"

for i in "${cmds[@]}"; do
	echo "Testing arguments '$i $device'"
	eval $command "$i" $device
	if [[ $? == 2 ]]; then
		echo "Invalid command: $i"
		exit 1
	fi
	sleep 1
done

