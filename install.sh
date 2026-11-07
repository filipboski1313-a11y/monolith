#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════
# MONOLITH HYPRLAND SHELL - AUTOMATED INSTALLATION SCRIPT
# ══════════════════════════════════════════════════════════════════════════════
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}"
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║                  MONOLITH HYPRLAND INSTALLER                    ║"
echo "║    Ultra-Minimalist • Zero-Bloat • High Performance C++ DE       ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"
TARGET_CONFIG="${REAL_HOME}/.config/monolith"

# 1. Dependency Check & Installation (Arch Linux / Fedora)
if [ -f /etc/arch-release ]; then
    echo -e "${BLUE}[1/7] Arch Linux detected. Checking dependencies...${NC}"
    PKGS=(
        base-devel gcc pkg-config git
        hyprland waybar fuzzel foot swaync
        hyprpaper hypridle hyprlock
        gtk4 gtk4-layer-shell libadwaita cairo nlohmann-json
        wireplumber brightnessctl playerctl jq cliphist wl-clipboard grim slurp
        python python-numpy python-pillow
    )

    MISSING=()
    for pkg in "${PKGS[@]}"; do
        if ! pacman -Qi "$pkg" &>/dev/null; then
            MISSING+=("$pkg")
        fi
    done

    if [ ${#MISSING[@]} -gt 0 ]; then
        echo -e "${BLUE}Missing packages: ${MISSING[*]}${NC}"
        echo -e "${BLUE}Installing via pacman -S...${NC}"
        pacman -S --needed --noconfirm "${MISSING[@]}"
    else
        echo -e "${GREEN}All required system packages are already installed.${NC}"
    fi
elif [ -f /etc/fedora-release ]; then
    echo -e "${BLUE}[1/7] Fedora Linux detected. Checking dependencies...${NC}"
    FEDORA_PKGS=(
        gcc gcc-c++ pkgconf-pkg-config git make
        hyprland waybar fuzzel foot SwayNotificationCenter
        hyprpaper hypridle hyprlock
        gtk4-devel gtk4-layer-shell-devel libadwaita-devel cairo-devel json-devel
        wireplumber brightnessctl playerctl jq cliphist wl-clipboard grim slurp
        python3 python3-numpy python3-pillow
    )

    MISSING_FEDORA=()
    for pkg in "${FEDORA_PKGS[@]}"; do
        if ! rpm -q "$pkg" &>/dev/null; then
            MISSING_FEDORA+=("$pkg")
        fi
    done

    if [ ${#MISSING_FEDORA[@]} -gt 0 ]; then
        echo -e "${BLUE}Missing Fedora packages: ${MISSING_FEDORA[*]}${NC}"
        dnf install -y "${MISSING_FEDORA[@]}"
    else
        echo -e "${GREEN}All required Fedora packages are already installed.${NC}"
    fi

    # Ensure Hyprland vendor dynamic libraries (e.g. libaquamarine) are accessible
    if [ -d "/usr/libexec/hyprland/vendor/lib64" ]; then
        echo "/usr/libexec/hyprland/vendor/lib64" > /etc/ld.so.conf.d/hyprland-vendor.conf
        ldconfig 2>/dev/null || true
    fi
else
    echo -e "${RED}[Warning] Unsupported distribution. Please ensure dependencies are installed manually.${NC}"
fi

# 2. Permissions on scripts
echo -e "${BLUE}[2/7] Setting executable permissions on scripts...${NC}"
chmod +x "${SCRIPT_DIR}/scripts/"*.sh "${SCRIPT_DIR}/scripts/"*.py

# 3. Compile Native C++ Suite
echo -e "${BLUE}[3/7] Compiling native C++ suite (Settings, TaskGraph, Toolbox, Popups)...${NC}"
make -C "${SCRIPT_DIR}" clean
make -C "${SCRIPT_DIR}" -j"$(nproc)"

# 4. Install Binaries to /usr/local/bin and ~/.local/bin
echo -e "${BLUE}[4/7] Installing binaries to /usr/local/bin and ${REAL_HOME}/.local/bin...${NC}"
make -C "${SCRIPT_DIR}" install
mkdir -p "${REAL_HOME}/.local/bin"
cp -f "${SCRIPT_DIR}/bin/"* "${REAL_HOME}/.local/bin/" 2>/dev/null || true
cp -f "${SCRIPT_DIR}/scripts/monolith-updater.sh" "${REAL_HOME}/.local/bin/monolith-updater" 2>/dev/null || true
chown -R "${REAL_USER}:${REAL_USER}" "${REAL_HOME}/.local/bin"

# 5. Deploy Configuration to ~/.config/monolith
echo -e "${BLUE}[5/7] Deploying configuration to ${TARGET_CONFIG}...${NC}"
mkdir -p "$TARGET_CONFIG"
cp -r "${SCRIPT_DIR}/foot" "$TARGET_CONFIG/"
cp -r "${SCRIPT_DIR}/fuzzel" "$TARGET_CONFIG/"
cp -r "${SCRIPT_DIR}/scripts" "$TARGET_CONFIG/"
cp -r "${SCRIPT_DIR}/swaync" "$TARGET_CONFIG/"
cp -r "${SCRIPT_DIR}/waybar" "$TARGET_CONFIG/"
cp -r "${SCRIPT_DIR}/systemd" "$TARGET_CONFIG/"
cp -r "${SCRIPT_DIR}/taskgraph" "$TARGET_CONFIG/"
cp -r "${SCRIPT_DIR}/toolbox" "$TARGET_CONFIG/"
cp -f "${SCRIPT_DIR}/hypridle.conf" "$TARGET_CONFIG/"
cp -f "${SCRIPT_DIR}/hyprland.conf" "$TARGET_CONFIG/"
cp -f "${SCRIPT_DIR}/hyprlock.conf" "$TARGET_CONFIG/"
cp -f "${SCRIPT_DIR}/hyprpaper.conf" "$TARGET_CONFIG/"
cp -f "${SCRIPT_DIR}/wallpaper.png" "$TARGET_CONFIG/"

# Preserve existing settings if they already exist, otherwise copy defaults
if [ ! -f "$TARGET_CONFIG/settings.json" ]; then
    cp -f "${SCRIPT_DIR}/settings.json" "$TARGET_CONFIG/"
fi
if [ ! -f "$TARGET_CONFIG/user-settings.conf" ]; then
    cp -f "${SCRIPT_DIR}/user-settings.conf" "$TARGET_CONFIG/"
fi

# Detect GPU & configure hardware.conf
echo -e "${BLUE}Configuring hardware & GPU profile...${NC}"
if [ ! -f "$TARGET_CONFIG/hardware.conf" ]; then
    if command -v nvidia-smi &>/dev/null && lsmod | grep -q nvidia; then
        echo -e "${GREEN}NVIDIA GPU detected. Generating hardware.conf for NVIDIA...${NC}"
        cat << 'EOF' > "$TARGET_CONFIG/hardware.conf"
# --- Hardware & GPU Configuration (NVIDIA) ---
env = LIBVA_DRIVER_NAME,nvidia
env = GBM_BACKEND,nvidia-drm
env = __GLX_VENDOR_LIBRARY_NAME,nvidia
env = WLR_NO_HARDWARE_CURSORS,1
env = MOZ_DISABLE_RDD_SANDBOX,1
env = NVD_BACKEND,direct

# --- Display & Monitors ---
monitor = DP-1, 3440x1440@180, 1920x0, 1
monitor = HDMI-A-1, 1920x1080@60, 0x0, 1
monitor = MAC-1, 1440x900@60, 2920x-900, 1
monitor = , preferred, auto, 1
EOF
    else
        echo -e "${GREEN}AMD / Intel GPU detected. Generating hardware.conf...${NC}"
        cat << 'EOF' > "$TARGET_CONFIG/hardware.conf"
# --- Hardware & GPU Configuration (AMD / Intel) ---
env = AMD_VULKAN_ICD,RADV
env = RADV_PERFTEST,gpl

# --- Display & Monitors ---
monitor = eDP-1, preferred, 0x0, 1.23
monitor = , preferred, auto, 1
EOF
    fi
fi

chown -R "${REAL_USER}:${REAL_USER}" "$TARGET_CONFIG"

# 6. Setup Systemd User Units
echo -e "${BLUE}[6/7] Configuring systemd --user service units...${NC}"
SYSTEMD_USER_DIR="${REAL_HOME}/.config/systemd/user"
mkdir -p "$SYSTEMD_USER_DIR"
cp -f "${SCRIPT_DIR}/systemd/"*.service "$SYSTEMD_USER_DIR/"
chown -R "${REAL_USER}:${REAL_USER}" "$SYSTEMD_USER_DIR"
if [ -n "$SUDO_USER" ]; then
    sudo -u "$REAL_USER" XDG_RUNTIME_DIR="/run/user/$(id -u "$REAL_USER")" systemctl --user daemon-reload 2>/dev/null || true
else
    systemctl --user daemon-reload 2>/dev/null || true
fi

# 7. Register Desktop Session in Display Manager (SDDM / GDM)
echo -e "${BLUE}[7/7] Registering Monolith Wayland session in /usr/share/wayland-sessions...${NC}"
tee /usr/local/bin/monolith-session > /dev/null << 'EOF'
#!/usr/bin/env bash
export PATH="${HOME}/.local/bin:${PATH}"
if [ -d "/usr/libexec/hyprland/vendor/lib64" ]; then
    export LD_LIBRARY_PATH="/usr/libexec/hyprland/vendor/lib64:${LD_LIBRARY_PATH}"
fi
export XDG_CURRENT_DESKTOP=Hyprland
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=Monolith
export DESKTOP_SESSION=Monolith

CONFIG_FILE="${HOME}/.config/monolith/hyprland.conf"
if [ ! -f "$CONFIG_FILE" ]; then
    CONFIG_FILE="/home/boski/.config/monolith/hyprland.conf"
fi

if [ -x /usr/bin/start-hyprland ]; then
    exec /usr/bin/start-hyprland -- --config "$CONFIG_FILE" "$@"
else
    exec Hyprland --config "$CONFIG_FILE" "$@"
fi
EOF
chmod +x /usr/local/bin/monolith-session

tee /usr/share/wayland-sessions/monolith.desktop > /dev/null << 'EOF'
[Desktop Entry]
Name=Monolith
Comment=Monolith Hyprland Desktop Shell (Ultra-Minimalist C++20)
Exec=/usr/local/bin/monolith-session
TryExec=/usr/local/bin/monolith-session
Type=Application
DesktopNames=Hyprland;Monolith
Keywords=wayland;compositor;monolith;hyprland;minimalist;
EOF

echo -e "${GREEN}${BOLD}✓ Monolith installation completed successfully!${NC}"
echo ""
echo "Monolith session is now registered in your display manager (SDDM / GDM)."
echo "You can select 'Monolith' at the login screen, or launch it nested right now with:"
echo -e "  ${BOLD}~/.config/monolith/scripts/launch-nested.sh${NC}"
echo ""
