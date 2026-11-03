#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# Monolith Waybar Drawer Auto-Close Controller
# Automatically collapses the system drawer after 10s of inactivity
# ══════════════════════════════════════════════════════════════════════════════

STATE_FILE="/tmp/.monolith_drawer_state"
NOW=$(date +%s)

# Read previous state
if [ -f "$STATE_FILE" ]; then
    STATE=$(cat "$STATE_FILE" 2>/dev/null || echo "closed")
else
    STATE="closed"
fi

if [ "$STATE" = "open" ]; then
    # User clicked manually to close -> reset state
    echo "closed" > "$STATE_FILE"
    exit 0
fi

# Drawer just opened -> mark open with current timestamp
echo "open" > "$STATE_FILE"
echo "$NOW" > "/tmp/.monolith_drawer_ts"

# Background watcher: sleep 10s, then close if still open
(
    sleep 10
    CUR_STATE=$(cat "$STATE_FILE" 2>/dev/null || echo "closed")
    TS=$(cat "/tmp/.monolith_drawer_ts" 2>/dev/null || echo 0)
    if [ "$CUR_STATE" = "open" ] && [ "$TS" = "$NOW" ]; then
        echo "closed" > "$STATE_FILE"
        killall -SIGUSR2 waybar 2>/dev/null || true
    fi
) &
