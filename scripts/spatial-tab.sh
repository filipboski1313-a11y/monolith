#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# Monolith Spatial Tab Controller
# Enforces strictly 1-step vertical transitions between Spotify / Desktop / Extra
# Prevents opening `special:extra` when empty (eliminating the invisible overlay trap)
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

CUR_POS=$(hyprctl cursorpos 2>/dev/null | tr -d ',')
CUR_X=$(echo "$CUR_POS" | awk '{print $1}')
CUR_Y=$(echo "$CUR_POS" | awk '{print $2}')
MON_JSON=$(hyprctl monitors -j 2>/dev/null)

TARGET_MON=$(echo "$MON_JSON" | jq -c --argjson cx "${CUR_X:-0}" --argjson cy "${CUR_Y:-0}" '
  (.[] | select($cx >= .x and $cx < (.x + .width) and $cy >= .y and $cy < (.y + .height))) // (.[] | select(.focused)) // .[0]
')
TARGET_NAME=$(echo "$TARGET_MON" | jq -r '.name // ""')
CUR_SPECIAL=$(echo "$TARGET_MON" | jq -r '.specialWorkspace.name // ""')
if [ "$CUR_SPECIAL" = "null" ]; then
    CUR_SPECIAL=""
fi

# Ensure focus is on the monitor containing cursor so togglespecialworkspace targets this exact screen
if [ -n "$TARGET_NAME" ]; then
    hyprctl dispatch focusmonitor "$TARGET_NAME" >/dev/null 2>&1
fi

# Always close ramvault if it ever got opened
if [ "$CUR_SPECIAL" = "special:ramvault" ]; then
    hyprctl dispatch togglespecialworkspace ramvault >/dev/null 2>&1
    CUR_SPECIAL=""
fi

EXTRA_COUNT=$(hyprctl clients -j 2>/dev/null | jq '[.[] | select(.workspace.name == "special:extra")] | length' 2>/dev/null || echo 0)

case "$ACTION" in
    up)
        if [ "$CUR_SPECIAL" = "special:extra" ]; then
            hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert -100% ; dispatch togglespecialworkspace extra" >/dev/null 2>&1
        elif [ -z "$CUR_SPECIAL" ]; then
            if ! pgrep -x spotify >/dev/null 2>&1 && ! pgrep -f "spotify-launcher" >/dev/null 2>&1; then
                spotify >/dev/null 2>&1 &
                sleep 0.25
            fi
            hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert 100% ; dispatch togglespecialworkspace spotify" >/dev/null 2>&1
        fi
        ;;

    down)
        if [ "$CUR_SPECIAL" = "special:spotify" ]; then
            hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert 100% ; dispatch togglespecialworkspace spotify" >/dev/null 2>&1
        elif [ -z "$CUR_SPECIAL" ]; then
            if [ "${EXTRA_COUNT:-0}" -gt 0 ]; then
                hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert -100% ; dispatch togglespecialworkspace extra" >/dev/null 2>&1
            else
                notify-send -a Monolith -i emblem-shared -t 1500 "Extra Shelf is Empty" "Stash window: SUPER + Alt + S"
            fi
        fi
        ;;

    toggle_spotify)
        if [ "$CUR_SPECIAL" = "special:spotify" ]; then
            hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert 100% ; dispatch togglespecialworkspace spotify" >/dev/null 2>&1
        else
            if [ -n "$CUR_SPECIAL" ]; then
                hyprctl dispatch togglespecialworkspace "${CUR_SPECIAL#special:}" >/dev/null 2>&1
            fi
            if ! pgrep -x spotify >/dev/null 2>&1 && ! pgrep -f "spotify-launcher" >/dev/null 2>&1; then
                spotify >/dev/null 2>&1 &
                sleep 0.25
            fi
            hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert 100% ; dispatch togglespecialworkspace spotify" >/dev/null 2>&1
        fi
        ;;

    toggle_extra)
        if [ "$CUR_SPECIAL" = "special:extra" ]; then
            hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert -100% ; dispatch togglespecialworkspace extra" >/dev/null 2>&1
        elif [ "${EXTRA_COUNT:-0}" -gt 0 ]; then
            if [ -n "$CUR_SPECIAL" ]; then
                hyprctl dispatch togglespecialworkspace "${CUR_SPECIAL#special:}" >/dev/null 2>&1
            fi
            hyprctl --batch "keyword animation specialWorkspace, 1, 1.4, micro, slidevert -100% ; dispatch togglespecialworkspace extra" >/dev/null 2>&1
        else
            notify-send -a Monolith -i emblem-shared -t 1500 "Extra Shelf is empty" "Stash a window: SUPER + Alt + S"
        fi
        ;;
esac

pkill -RTMIN+8 waybar 2>/dev/null || true
