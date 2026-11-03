#!/usr/bin/env bash
# Monolith Session Initialization Script
set -e

# Ensure user directories exist
PICTURES_DIR="$(xdg-user-dir PICTURES 2>/dev/null)"
mkdir -p "${PICTURES_DIR:-$HOME/Pictures}/Screenshots" "${HOME}/.local/share" "${HOME}/.cache"

# Import environment into systemd and D-Bus for portal synchronization
systemctl --user import-environment WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP HYPRLAND_INSTANCE_SIGNATURE 2>/dev/null || true
dbus-update-activation-environment --systemd WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP HYPRLAND_INSTANCE_SIGNATURE 2>/dev/null || true

# Start Polkit Authentication Agent if not running
if ! pgrep -f "polkit-gnome-authentication-agent-1" > /dev/null; then
    if [ -f "/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1" ]; then
        /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &
    fi
fi

# Reset failed user units if any
systemctl --user reset-failed 2>/dev/null || true
