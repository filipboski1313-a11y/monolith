#!/usr/bin/env bash
# Monolith Instant Clipboard Picker
set -e

SELECTION=$(cliphist list | fuzzel --config "${XDG_CONFIG_HOME:-$HOME/.config}/monolith/fuzzel/fuzzel.ini" --dmenu --prompt="  󰅍  " --placeholder="Clipboard history...")

if [ -n "$SELECTION" ]; then
    echo "$SELECTION" | cliphist decode | wl-copy
fi
