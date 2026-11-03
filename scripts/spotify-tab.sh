#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# Monolith Spotify Tab Manager
# Toggles upper Spotify tab (special:spotify) and launches if missing
# ══════════════════════════════════════════════════════════════════════════════

# Check if spotify is running
if ! pgrep -x spotify >/dev/null 2>&1 && ! pgrep -f "spotify-launcher" >/dev/null 2>&1; then
    spotify >/dev/null 2>&1 &
    # Allow process spawn
    sleep 0.3
fi

# Toggle upper tab
hyprctl dispatch togglespecialworkspace spotify
