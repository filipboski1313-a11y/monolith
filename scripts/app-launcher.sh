#!/usr/bin/env bash
# Monolith App Launcher Wrapper (Signals Fuzzel state to Monolith Task Graph)

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/monolith"

if pkill -x fuzzel; then
    rm -f /tmp/monolith_fuzzel_active
    exit 0
fi

touch /tmp/monolith_fuzzel_active
fuzzel --config "$CONF/fuzzel/fuzzel.ini" "$@"
rm -f /tmp/monolith_fuzzel_active
