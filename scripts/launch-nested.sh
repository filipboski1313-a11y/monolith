#!/usr/bin/env bash
# ==============================================================================
# Monolith Nested Session Launcher
# Runs Monolith Hyprland inside an isolated window without conflicting with host
# ==============================================================================

set -e

CONF_DIR="${HOME}/.config/monolith"
NESTED_CONF="/tmp/monolith-nested.conf"

# Generate nested configuration on the fly from current hyprland.conf
# Strips out conflicting daemons (systemd, swaync, hypridle, session-init, cliphist)
grep -v -E "systemctl|swaync|hypridle|session-init|cliphist" "${CONF_DIR}/hyprland.conf" > "${NESTED_CONF}"

# Start nested Waybar directly inside this instance
cat << 'EOF' >> "${NESTED_CONF}"

# --- Nested Specific Autostart ---
exec-once = waybar -c ~/.config/monolith/waybar/config.jsonc -s ~/.config/monolith/waybar/style.css
EOF

export MONOLITH_NESTED=1

echo "Uruchamianie Monolith w oknie (nested compositor)..."
exec Hyprland -c "${NESTED_CONF}" "$@"
