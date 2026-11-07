#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# Monolith Micro OSD (Zero-Latency Visual Feedback for Volume & Brightness)
# Uses SwayNC synchronous private notification replacement (< 1ms toast)
# ══════════════════════════════════════════════════════════════════════════════
set -e

ACTION="${1:-}"

case "$ACTION" in
    vol_up)
        wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ 5%+
        ;;
    vol_down)
        wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ 5%-
        ;;
    vol_mute)
        wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle
        ;;
    mic_mute)
        wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle
        ;;
    bright_up)
        brightnessctl set 5%+
        ;;
    bright_down)
        brightnessctl set 5%-
        ;;
esac

if [[ "$ACTION" == *"vol"* ]]; then
    VOL_RAW=$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || echo "Volume: 0.0")
    MUTED=$(echo "$VOL_RAW" | grep -o '\[MUTED\]' || true)
    VOL_FLOAT=$(echo "$VOL_RAW" | awk '{print $2}')
    VOL=$(awk -v v="$VOL_FLOAT" 'BEGIN { printf "%.0f", v * 100 }')

    if [ -n "$MUTED" ]; then
        notify-send -a "Monolith" -i "audio-volume-muted" -h string:x-canonical-private-synchronous:osd -h int:value:0 -t 900 "Volume: Muted"
    else
        notify-send -a "Monolith" -i "audio-volume-high" -h string:x-canonical-private-synchronous:osd -h int:value:"$VOL" -t 900 "Volume: ${VOL}%"
    fi
elif [[ "$ACTION" == *"mic"* ]]; then
    MIC_RAW=$(wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null || echo "Volume: 0.0")
    MUTED=$(echo "$MIC_RAW" | grep -o '\[MUTED\]' || true)
    if [ -n "$MUTED" ]; then
        notify-send -a "Monolith" -i "microphone-sensitivity-muted" -h string:x-canonical-private-synchronous:osd -t 900 "Microphone: Muted"
    else
        notify-send -a "Monolith" -i "audio-input-microphone" -h string:x-canonical-private-synchronous:osd -t 900 "Microphone: Unmuted"
    fi
elif [[ "$ACTION" == *"bright"* ]]; then
    BRIGHT=$(brightnessctl -m 2>/dev/null | cut -d, -f4 | tr -d '%' || echo "50")
    notify-send -a "Monolith" -i "display-brightness" -h string:x-canonical-private-synchronous:osd -h int:value:"$BRIGHT" -t 900 "Brightness: ${BRIGHT}%"
fi
