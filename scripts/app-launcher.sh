#!/usr/bin/env bash
# Monolith App Launcher Wrapper (Signals Fuzzel state to Task Graph + CPU Boost Burst)

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/monolith"

if pkill -x fuzzel; then
    rm -f /tmp/monolith_fuzzel_active
    exit 0
fi

# Trigger 3.5s AMD CPU Boost Burst so launched app initializes at peak clock
monolith-ram-preloader boost 3.5 >/dev/null 2>&1 &

# Determine focused monitor so constellation deflection applies only to this display
CUR_POS=$(hyprctl cursorpos 2>/dev/null | tr -d ',')
CUR_X=$(echo "$CUR_POS" | awk '{print $1}')
CUR_Y=$(echo "$CUR_POS" | awk '{print $2}')
MON_JSON=$(hyprctl monitors -j 2>/dev/null)
TARGET_MON=$(echo "$MON_JSON" | jq -r --argjson cx "${CUR_X:-0}" --argjson cy "${CUR_Y:-0}" '
  ((.[] | select($cx >= .x and $cx < (.x + .width) and $cy >= .y and $cy < (.y + .height))) // (.[] | select(.focused)) // .[0]) | .name
')

if [ -n "$TARGET_MON" ]; then
    hyprctl dispatch focusmonitor "$TARGET_MON" >/dev/null 2>&1
fi

echo "${TARGET_MON:-all}" > /tmp/monolith_fuzzel_active
if [ -n "$TARGET_MON" ]; then
    fuzzel --output="$TARGET_MON" --config "$CONF/fuzzel/fuzzel.ini" "$@"
else
    fuzzel --config "$CONF/fuzzel/fuzzel.ini" "$@"
fi
rm -f /tmp/monolith_fuzzel_active
