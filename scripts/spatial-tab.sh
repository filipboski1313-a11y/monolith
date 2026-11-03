#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# Monolith Spatial Tab Controller
# Enforces strictly 1-step vertical transitions between Spotify / Desktop / Extra
# Direction-aware animations:
#   - Spotify: slidevert 100%
#   - Extra: slidevert -100%
# ══════════════════════════════════════════════════════════════════════════════

ACTION="$1"
LOCK_FILE="/tmp/.monolith_spatial_tab.lock"
NOW=$(date +%s%3N 2>/dev/null || date +%s000)

if [ -f "$LOCK_FILE" ]; then
    LAST=$(cat "$LOCK_FILE" 2>/dev/null || echo 0)
    DIFF=$((NOW - LAST))
    if [ "$DIFF" -ge 0 ] && [ "$DIFF" -lt 260 ]; then
        exit 0
    fi
fi
echo "$NOW" > "$LOCK_FILE"

MON_JSON=$(hyprctl monitors -j 2>/dev/null)
CUR_SPECIAL=$(echo "$MON_JSON" | jq -r '.[0].specialWorkspace.name // ""')
if [ "$CUR_SPECIAL" = "null" ]; then
    CUR_SPECIAL=""
fi

case "$ACTION" in
    up)
        if [ "$CUR_SPECIAL" = "special:extra" ]; then
            hyprctl keyword animation "specialWorkspace, 1, 1.4, micro, slidevert -100%" >/dev/null 2>&1
            hyprctl dispatch togglespecialworkspace extra
        elif [ -z "$CUR_SPECIAL" ]; then
            hyprctl keyword animation "specialWorkspace, 1, 1.4, micro, slidevert 100%" >/dev/null 2>&1
            if ! pgrep -x spotify >/dev/null 2>&1 && ! pgrep -f "spotify-launcher" >/dev/null 2>&1; then
                spotify >/dev/null 2>&1 &
                sleep 0.3
            fi
            hyprctl dispatch togglespecialworkspace spotify
        fi
        ;;

    down)
        if [ "$CUR_SPECIAL" = "special:spotify" ]; then
            hyprctl keyword animation "specialWorkspace, 1, 1.4, micro, slidevert 100%" >/dev/null 2>&1
            hyprctl dispatch togglespecialworkspace spotify
        elif [ -z "$CUR_SPECIAL" ]; then
            hyprctl keyword animation "specialWorkspace, 1, 1.4, micro, slidevert -100%" >/dev/null 2>&1
            hyprctl dispatch togglespecialworkspace extra
        fi
        ;;

    toggle_spotify)
        hyprctl keyword animation "specialWorkspace, 1, 1.4, micro, slidevert 100%" >/dev/null 2>&1
        if [ "$CUR_SPECIAL" = "special:spotify" ]; then
            hyprctl dispatch togglespecialworkspace spotify
        else
            if [ -n "$CUR_SPECIAL" ]; then
                hyprctl dispatch togglespecialworkspace "${CUR_SPECIAL#special:}"
            fi
            if ! pgrep -x spotify >/dev/null 2>&1 && ! pgrep -f "spotify-launcher" >/dev/null 2>&1; then
                spotify >/dev/null 2>&1 &
                sleep 0.3
            fi
            hyprctl dispatch togglespecialworkspace spotify
        fi
        ;;

    toggle_extra)
        hyprctl keyword animation "specialWorkspace, 1, 1.4, micro, slidevert -100%" >/dev/null 2>&1
        if [ "$CUR_SPECIAL" = "special:extra" ]; then
            hyprctl dispatch togglespecialworkspace extra
        else
            if [ -n "$CUR_SPECIAL" ]; then
                hyprctl dispatch togglespecialworkspace "${CUR_SPECIAL#special:}"
            fi
            hyprctl dispatch togglespecialworkspace extra
        fi
        ;;
esac

# Signal Waybar to immediately refresh spatial tab badge
pkill -RTMIN+8 waybar 2>/dev/null || true
