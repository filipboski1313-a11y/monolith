#!/usr/bin/env bash
# Monolith Zero-Latency Terminal Launcher (< 2.5ms via footclient)
FOOT_CFG="$HOME/.config/monolith/foot/foot.ini"

if footclient "$@" 2>/dev/null; then
    exit 0
fi

# Start server daemon for subsequent instant launches and open terminal
foot --server -c "$FOOT_CFG" >/dev/null 2>&1 &
exec foot -c "$FOOT_CFG" "$@"
