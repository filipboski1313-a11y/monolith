#!/usr/bin/env bash
# Monolith App Launcher Wrapper (Signals Fuzzel state to Task Graph + CPU Boost Burst)

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/monolith"

if pkill -x fuzzel; then
    rm -f /tmp/monolith_fuzzel_active
    exit 0
fi

# Trigger 3.5s AMD CPU Boost Burst so launched app initializes at peak clock
/usr/local/bin/monolith-ram-preloader boost 3.5 >/dev/null 2>&1 &

touch /tmp/monolith_fuzzel_active
fuzzel --config "$CONF/fuzzel/fuzzel.ini" "$@"
rm -f /tmp/monolith_fuzzel_active
