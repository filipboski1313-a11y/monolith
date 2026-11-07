#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# Monolith Shelf Manager (Window Stash / Extra Tab)
# Pushes active window to lower Extra Tab (special:extra) or pops it back
# ══════════════════════════════════════════════════════════════════════════════

ACTION="${1:-push}"

CUR_POS=$(hyprctl cursorpos 2>/dev/null | tr -d ',')
CUR_X=$(echo "$CUR_POS" | awk '{print $1}')
CUR_Y=$(echo "$CUR_POS" | awk '{print $2}')
MON_JSON=$(hyprctl monitors -j 2>/dev/null)

TARGET_MON=$(echo "$MON_JSON" | jq -c --argjson cx "${CUR_X:-0}" --argjson cy "${CUR_Y:-0}" '
  (.[] | select($cx >= .x and $cx < (.x + .width) and $cy >= .y and $cy < (.y + .height))) // (.[] | select(.focused)) // .[0]
')
TARGET_NAME=$(echo "$TARGET_MON" | jq -r '.name // ""')
CUR_REG_WS=$(echo "$TARGET_MON" | jq -r '.activeWorkspace.id // 1')

ACTIVE_WIN_JSON=$(hyprctl activewindow -j 2>/dev/null)
WIN_ADDR=$(echo "$ACTIVE_WIN_JSON" | jq -r '.address // empty')
WIN_TITLE=$(echo "$ACTIVE_WIN_JSON" | jq -r '.title // "Window"')
WIN_CLASS=$(echo "$ACTIVE_WIN_JSON" | jq -r '.class // "App"')
CURRENT_WS=$(echo "$ACTIVE_WIN_JSON" | jq -r '.workspace.name // empty')

if [ "$ACTION" = "push" ]; then
    if [ -n "$WIN_ADDR" ] && [ "$WIN_ADDR" != "null" ]; then
        # Move active window silently to special:extra
        hyprctl dispatch movetoworkspacesilent special:extra
        
        # Display clean notification
        TITLE_TRUNC=$(echo "$WIN_TITLE" | cut -c 1-28)
        notify-send -a Monolith -i emblem-shared -t 1500 "Extra Shelf" "Stashed window: ${TITLE_TRUNC:-$WIN_CLASS} [SUPER + S]"
    fi
elif [ "$ACTION" = "pop" ]; then
    TARGET_WIN=""
    
    # If currently inside special:extra, pop the currently focused window
    if [ "$CURRENT_WS" = "special:extra" ] && [ -n "$WIN_ADDR" ]; then
        TARGET_WIN="$WIN_ADDR"
    else
        # Otherwise pull the most recently stashed window from special:extra
        TARGET_WIN=$(hyprctl clients -j 2>/dev/null | jq -r '[.[] | select(.workspace.name == "special:extra")] | last | .address // empty')
    fi
    
    if [ -n "$TARGET_WIN" ] && [ "$TARGET_WIN" != "null" ]; then
        hyprctl dispatch movetoworkspace "$CUR_REG_WS,address:$TARGET_WIN"
        notify-send -a Monolith -i emblem-shared -t 1200 "Extra Shelf" "Restored window to workspace $CUR_REG_WS"
    else
        notify-send -a Monolith -i emblem-shared -t 1000 "Extra Shelf" "No windows in Extra shelf"
    fi
elif [ "$ACTION" = "toggle" ]; then
    if [ -n "$TARGET_NAME" ]; then
        hyprctl dispatch focusmonitor "$TARGET_NAME" >/dev/null 2>&1
    fi
    hyprctl dispatch togglespecialworkspace extra
fi
