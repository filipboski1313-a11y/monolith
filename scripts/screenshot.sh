#!/usr/bin/env bash
# Monolith Instant Screenshot Tool
set -e

PICTURES_DIR="$(xdg-user-dir PICTURES 2>/dev/null)"
DIR="${PICTURES_DIR:-$HOME/Pictures}/Screenshots"
mkdir -p "$DIR"
TIMESTAMP="$(date +'%Y-%m-%d_%H-%M-%S')"
FILE="${DIR}/screenshot_${TIMESTAMP}.png"

MODE="${1:-area}"

case "$MODE" in
    full)
        grim "$FILE"
        wl-copy < "$FILE"
        notify-send -a "Monolith" -i camera-photo "Screenshot" "Saved fullscreen: ${FILE##*/}" -t 2000
        ;;
    area)
        # Capture selection with slurp
        GEOM="$(slurp -d -b 0a0a0c80 -c 71717aff -s 00000000 -w 1)"
        if [ -n "$GEOM" ]; then
            grim -g "$GEOM" "$FILE"
            wl-copy < "$FILE"
            notify-send -a "Monolith" -i camera-photo "Screenshot" "Saved selection: ${FILE##*/}" -t 2000
        fi
        ;;
    *)
        echo "Usage: $0 [full|area]"
        exit 1
        ;;
esac
