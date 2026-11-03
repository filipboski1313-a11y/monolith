#!/usr/bin/env bash
# Monolith Minimalist Power Menu
set -e

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/monolith"

CHOICE=$(printf "󰌾  Lock Screen\n󰤄  Suspend\n󰜉  Reboot\n󰐥  Power Off\n󰍃  Log Out" | \
    fuzzel --config "$CONF/fuzzel/fuzzel.ini" --dmenu --lines=5 --width=24 --prompt="  󰐥  " --placeholder="Power menu...")

case "$CHOICE" in
    *"Lock"*)
        pidof hyprlock || hyprlock --config "$CONF/hyprlock.conf"
        ;;
    *"Suspend"*)
        systemctl suspend
        ;;
    *"Reboot"*)
        systemctl reboot
        ;;
    *"Power Off"*)
        systemctl poweroff
        ;;
    *"Log Out"*)
        hyprctl dispatch exit
        ;;
esac
