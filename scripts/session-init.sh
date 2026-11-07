#!/usr/bin/env bash
# Monolith Session Initialization Script
set -e

# Ensure user directories exist
PICTURES_DIR="$(xdg-user-dir PICTURES 2>/dev/null)"
mkdir -p "${PICTURES_DIR:-$HOME/Pictures}/Screenshots" "${HOME}/.local/share" "${HOME}/.cache"

# Import environment into systemd and D-Bus for portal synchronization
export ELECTRON_OZONE_PLATFORM_HINT=auto
export NIXOS_OZONE_WL=1
export MOZ_ENABLE_WAYLAND=1
export QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough
export MESA_SHADER_CACHE_MAX_SIZE=4G
export AMD_VULKAN_ICD=RADV
export RADV_PERFTEST=gpl
systemctl --user import-environment WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP HYPRLAND_INSTANCE_SIGNATURE ELECTRON_OZONE_PLATFORM_HINT NIXOS_OZONE_WL MOZ_ENABLE_WAYLAND QT_SCALE_FACTOR_ROUNDING_POLICY MESA_SHADER_CACHE_MAX_SIZE AMD_VULKAN_ICD RADV_PERFTEST 2>/dev/null || true
dbus-update-activation-environment --systemd WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP HYPRLAND_INSTANCE_SIGNATURE ELECTRON_OZONE_PLATFORM_HINT NIXOS_OZONE_WL MOZ_ENABLE_WAYLAND QT_SCALE_FACTOR_ROUNDING_POLICY MESA_SHADER_CACHE_MAX_SIZE AMD_VULKAN_ICD RADV_PERFTEST 2>/dev/null || true

# Start zero-latency Foot terminal server & Thunar daemon if not running
pgrep -f "foot --server" >/dev/null || foot --server -c "$HOME/.config/monolith/foot/foot.ini" >/dev/null 2>&1 &
pgrep -f "thunar --daemon" >/dev/null || thunar --daemon >/dev/null 2>&1 &

# Start Polkit Authentication Agent if not running
if ! pgrep -f "polkit-gnome-authentication-agent-1" > /dev/null; then
    if [ -f "/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1" ]; then
        /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &
    fi
fi

# Reset failed user units if any
systemctl --user reset-failed 2>/dev/null || true

# Preload configured applications into physical RAM cache on startup
systemctl --user start monolith-ram-preload.service 2>/dev/null || /usr/local/bin/monolith-ram-preloader preload-all &

# First-Run Welcome Experience (introduces user to Monolith and opens Control Center)
FIRST_RUN_FLAG="${XDG_CONFIG_HOME:-$HOME/.config}/monolith/.first_run_done"
if [ ! -f "$FIRST_RUN_FLAG" ]; then
    /usr/local/bin/monolith-settings --welcome &
fi
