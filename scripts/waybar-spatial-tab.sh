#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# Monolith Spatial Tab Indicator for Waybar
# Outputs clean JSON indicating whether user is on Desktop, Spotify or Extra
# ══════════════════════════════════════════════════════════════════════════════

MON_JSON=$(hyprctl monitors -j 2>/dev/null)
CUR_SPECIAL=$(echo "$MON_JSON" | jq -r '.[0].specialWorkspace.name // ""')
if [ "$CUR_SPECIAL" = "null" ]; then
    CUR_SPECIAL=""
fi

if [ "$CUR_SPECIAL" = "special:spotify" ]; then
    echo '{"text": "▲ SPOTIFY", "class": "tab-spotify", "tooltip": "Top Tab: Spotify\n[L-Click] Close | 3 fingers swipe down to return"}'
elif [ "$CUR_SPECIAL" = "special:extra" ]; then
    echo '{"text": "▼ EXTRA", "class": "tab-extra", "tooltip": "Bottom Tab: Extra Shelf\n[L-Click] Close | 3 fingers swipe up to return"}'
else
    WS_ID=$(echo "$MON_JSON" | jq -r '.[0].activeWorkspace.id // 1')
    echo "{\"text\": \"● DESKTOP\", \"class\": \"tab-desktop\", \"tooltip\": \"Workspace $WS_ID\n▲ 3 fingers swipe up: Spotify\n▼ 3 fingers swipe down: Extra Shelf\"}"
fi
