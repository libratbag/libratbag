#!/bin/bash
#
# Script to test ratbag-command for valid argument parsing. Must be run
# against a device file.
#

command="`dirname $0`/ratbag-command"
device=$1
if ! [[ -e "$device" ]]; then
	echo "Invalid device path or path missing"
	exit 1
fi

# no device argument
cmds=()
cmds+=("list")

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
cmds=()
cmds+=("info")
cmds+=("switch-etekcity")
cmds+=("profile active get")
cmds+=("profile active set 0")
cmds+=("resolution active get")
cmds+=("resolution active set 0")
cmds+=("dpi get")
cmds+=("dpi set 800")
cmds+=("rate get")
cmds+=("rate set 500")
cmds+=("profile 0 resolution active get")
cmds+=("profile 0 resolution active set 0")
cmds+=("resolution 0 dpi get")
cmds+=("resolution 0 dpi set 800")
cmds+=("resolution 0 rate get")
cmds+=("resolution 0 rate set 500")
cmds+=("profile 0 resolution 0 dpi get")
cmds+=("profile 0 resolution 0 dpi set 800")
cmds+=("profile 0 resolution 0 rate get")
cmds+=("profile 0 resolution 0 rate set 500")
cmds+=("button count")
cmds+=("profile 0 button count")
cmds+=("button 0 action get")
cmds+=("button 0 action set button 1")
cmds+=("profile 0 button 0 action get")
cmds+=("profile 0 button 0 action set button 1")
cmds+=("button 0 action set key KEY_ENTER")
cmds+=("profile 0 button 0 action set key KEY_ENTER")
cmds+=("button 0 action set special doubleclick")
cmds+=("profile 0 button 0 action set special doubleclick")
cmds+=("profile 0 button 0 action set macro +KEY_ENTER t05 -KEY_ENTER")
cmds+=("profile 0 led 0 get")
cmds+=("profile 0 led side get")
cmds+=("profile 0 led 0 set mode on")
cmds+=("profile 0 led 0 set color ffffff")
cmds+=("profile 0 led 0 set rate 1")
cmds+=("profile 0 led 0 set brightness 1")
for i in "${cmds[@]}"; do
	echo "Testing arguments '$i $device'"
	eval $command "$i" $device
	if [[ $? == 2 ]]; then
		echo "Invalid command: $i"
		exit 1
	fi
	sleep 1
done

