#!/usr/bin/env bash
# Monolith Smart Close ("Warm-Standby RAM Vault")
# Parks `load_to_ram: true` windows from regular workspaces into `special:ramvault` in <1ms
# and guarantees special workspaces never get stuck open when empty.

CFG="$HOME/.config/monolith/taskgraph/config.json"

CUR_SPECIAL=$(hyprctl monitors -j 2>/dev/null | jq -r '.[0].specialWorkspace.name // ""')
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

# If the window is inside special:ramvault, kill it directly
if [ "$ACTIVE_WS" = "special:ramvault" ]; then
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
        select((.match_class | ascii_downcase) == ($cls | ascii_downcase) or
               (($cls | ascii_downcase) | contains(.match_class | ascii_downcase))) |
        .id
    ' "$CFG" 2>/dev/null | head -n 1)

    if [ -n "$IS_RAM_LOCKED" ]; then
        # Check if there is already another instance in special:ramvault
        ALREADY_IN_VAULT=$(hyprctl clients -j 2>/dev/null | jq --arg cls "$ACTIVE_CLASS" --arg addr "$ACTIVE_ADDR" '
            [.[] | select(.workspace.name == "special:ramvault" and .address != $addr and ((.class | ascii_downcase) == ($cls | ascii_downcase)))] | length
        ' 2>/dev/null || echo 0)

        if [ "${ACTIVE_CLASS,,}" = "spotify" ]; then
            hyprctl dispatch movetoworkspacesilent "special:spotify,address:$ACTIVE_ADDR" >/dev/null 2>&1
            if [ "$CUR_SPECIAL" = "special:spotify" ]; then
                hyprctl dispatch togglespecialworkspace spotify >/dev/null 2>&1
            fi
        elif [ "${ALREADY_IN_VAULT:-0}" -eq 0 ]; then
            hyprctl dispatch movetoworkspacesilent "special:ramvault,address:$ACTIVE_ADDR" >/dev/null 2>&1
        else
            if [ -n "$ACTIVE_ADDR" ]; then
                hyprctl dispatch closewindow "address:$ACTIVE_ADDR" >/dev/null 2>&1
            else
                hyprctl dispatch killactive >/dev/null 2>&1
            fi
        fi

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
