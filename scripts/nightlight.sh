#!/usr/bin/env bash
# Monolith Night Light Toggle (Hyprsunset)
set -e

if pgrep -x "hyprsunset" > /dev/null; then
    pkill -x "hyprsunset"
    notify-send -a "Monolith" -i display "Night Light" "Blue light filter disabled" -t 1500
else
    hyprsunset -t 4200 &
    notify-send -a "Monolith" -i display "Night Light" "Warm color profile enabled (4200K)" -t 1500
fi
