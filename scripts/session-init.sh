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

# Detect GPU vendor dynamically
if command -v nvidia-smi &>/dev/null && lsmod | grep -q nvidia; then
    export LIBVA_DRIVER_NAME=nvidia
    export GBM_BACKEND=nvidia-drm
    export __GLX_VENDOR_LIBRARY_NAME=nvidia
    export NVD_BACKEND=direct
    export WLR_NO_HARDWARE_CURSORS=1
    EXTRA_ENVS="LIBVA_DRIVER_NAME GBM_BACKEND __GLX_VENDOR_LIBRARY_NAME NVD_BACKEND WLR_NO_HARDWARE_CURSORS"
elif lspci 2>/dev/null | grep -i 'vga\|3d' | grep -qi 'amd\|radeon'; then
    export AMD_VULKAN_ICD=RADV
    export RADV_PERFTEST=gpl
    EXTRA_ENVS="AMD_VULKAN_ICD RADV_PERFTEST"
else
    EXTRA_ENVS=""
fi

systemctl --user import-environment WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP HYPRLAND_INSTANCE_SIGNATURE ELECTRON_OZONE_PLATFORM_HINT NIXOS_OZONE_WL MOZ_ENABLE_WAYLAND QT_SCALE_FACTOR_ROUNDING_POLICY MESA_SHADER_CACHE_MAX_SIZE $EXTRA_ENVS 2>/dev/null || true
dbus-update-activation-environment --systemd WAYLAND_DISPLAY DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP HYPRLAND_INSTANCE_SIGNATURE ELECTRON_OZONE_PLATFORM_HINT NIXOS_OZONE_WL MOZ_ENABLE_WAYLAND QT_SCALE_FACTOR_ROUNDING_POLICY MESA_SHADER_CACHE_MAX_SIZE $EXTRA_ENVS 2>/dev/null || true

# Start zero-latency Foot terminal server & Thunar daemon if not running
pgrep -f "foot --server" >/dev/null || foot --server -c "$HOME/.config/monolith/foot/foot.ini" >/dev/null 2>&1 &
pgrep -f "thunar --daemon" >/dev/null || thunar --daemon >/dev/null 2>&1 &

# Start Polkit Authentication Agent if not running
if ! pgrep -f "polkit" > /dev/null; then
    for agent in \
        "/usr/libexec/xfce-polkit" \
        "/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1" \
        "/usr/libexec/polkit-gnome-authentication-agent-1" \
        "/usr/lib/polkit-kde-authentication-agent-1" \
        "/usr/libexec/kf6/polkit-kde-authentication-agent-1"; do
        if [ -f "$agent" ]; then
            "$agent" &
            break
        fi
    done
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
