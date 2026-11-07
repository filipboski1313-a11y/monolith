#!/usr/bin/env bash
# Monolith Smart Close ("Warm-Standby RAM Vault")
# Parks `load_to_ram: true` windows from regular workspaces into `special:ramvault` in <1ms
# and guarantees special workspaces never get stuck open when empty.

CFG="$HOME/.config/monolith/taskgraph/config.json"

CUR_POS=$(hyprctl cursorpos 2>/dev/null | tr -d ',')
CUR_X=$(echo "$CUR_POS" | awk '{print $1}')
CUR_Y=$(echo "$CUR_POS" | awk '{print $2}')
MON_JSON=$(hyprctl monitors -j 2>/dev/null)
TARGET_MON=$(echo "$MON_JSON" | jq -c --argjson cx "${CUR_X:-0}" --argjson cy "${CUR_Y:-0}" '
  (.[] | select($cx >= .x and $cx < (.x + .width) and $cy >= .y and $cy < (.y + .height))) // (.[] | select(.focused)) // .[0]
')
CUR_SPECIAL=$(echo "$TARGET_MON" | jq -r '.specialWorkspace.name // ""')
if [ "$CUR_SPECIAL" = "special:ramvault" ]; then
    hyprctl dispatch togglespecialworkspace ramvault >/dev/null 2>&1
fi

ACTIVE_JSON=$(hyprctl activewindow -j 2>/dev/null)
if [ -z "$ACTIVE_JSON" ] || [ "$ACTIVE_JSON" = "{}" ]; then
    if [ "$CUR_SPECIAL" = "special:extra" ]; then
        hyprctl dispatch togglespecialworkspace extra >/dev/null 2>&1
    fi
    exit 0
fi

ACTIVE_CLASS=$(printf '%s' "$ACTIVE_JSON" | jq -r '.class // empty' 2>/dev/null)
ACTIVE_ADDR=$(printf '%s' "$ACTIVE_JSON" | jq -r '.address // empty' 2>/dev/null)
ACTIVE_WS=$(printf '%s' "$ACTIVE_JSON" | jq -r '.workspace.name // empty' 2>/dev/null)

# If the window is inside ramvault, kill it directly
if [ "$ACTIVE_WS" = "ramvault" ] || [ "$ACTIVE_WS" = "name:ramvault" ] || [ "$ACTIVE_WS" = "special:ramvault" ]; then
    if [ -n "$ACTIVE_ADDR" ]; then
        hyprctl dispatch closewindow "address:$ACTIVE_ADDR" >/dev/null 2>&1
    else
        hyprctl dispatch killactive >/dev/null 2>&1
    fi
    exit 0
fi

# If the window is inside special:hidden or ramvault, kill it directly
if [ "$ACTIVE_WS" = "special:hidden" ] || [ "$ACTIVE_WS" = "ramvault" ] || [ "$ACTIVE_WS" = "name:ramvault" ] || [ "$ACTIVE_WS" = "special:ramvault" ]; then
    if [ -n "$ACTIVE_ADDR" ]; then
        hyprctl dispatch closewindow "address:$ACTIVE_ADDR" >/dev/null 2>&1
    else
        hyprctl dispatch killactive >/dev/null 2>&1
    fi
    exit 0
fi

# Check if this window class has load_to_ram == true
if [ -n "$ACTIVE_CLASS" ] && [ -f "$CFG" ]; then
    IS_RAM_LOCKED=$(jq -r --arg cls "$ACTIVE_CLASS" '
        .pinned[]? | select(.load_to_ram == true) |
        select((.match_class // "") as $m |
               (($m != "") and (($cls | ascii_downcase) | contains($m | ascii_downcase)))) |
        .id
    ' "$CFG" 2>/dev/null | head -n 1)

    if [ -n "$IS_RAM_LOCKED" ]; then
        # Stash to warm-standby name:ramvault in <1ms without slidevert animation
        hyprctl dispatch movetoworkspacesilent "name:ramvault,address:$ACTIVE_ADDR" >/dev/null 2>&1

        # If we were inside special:extra and it is now empty, close special:extra
        if [ "$ACTIVE_WS" = "special:extra" ]; then
            REM_EXTRA=$(hyprctl clients -j 2>/dev/null | jq '[.[] | select(.workspace.name == "special:extra")] | length' 2>/dev/null || echo 0)
            if [ "${REM_EXTRA:-0}" -eq 0 ]; then
                hyprctl dispatch togglespecialworkspace extra >/dev/null 2>&1
            fi
        fi
        exit 0
    fi
fi

if [ -n "$ACTIVE_ADDR" ]; then
    hyprctl dispatch closewindow "address:$ACTIVE_ADDR" >/dev/null 2>&1
else
    hyprctl dispatch killactive >/dev/null 2>&1
fi

if [ "$ACTIVE_WS" = "special:extra" ]; then
    sleep 0.08
    REM_EXTRA=$(hyprctl clients -j 2>/dev/null | jq '[.[] | select(.workspace.name == "special:extra")] | length' 2>/dev/null || echo 0)
    if [ "${REM_EXTRA:-0}" -eq 0 ]; then
        hyprctl dispatch togglespecialworkspace extra >/dev/null 2>&1
    fi
fi
