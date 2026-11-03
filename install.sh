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
TARGET_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/monolith"

# 1. Dependency Check & Installation (Arch Linux)
if [ -f /etc/arch-release ]; then
    echo -e "${BLUE}[1/5] Arch Linux detected. Checking and installing dependencies...${NC}"
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
        echo -e "${BLUE}Installing via sudo pacman -S...${NC}"
        sudo pacman -S --needed --noconfirm "${MISSING[@]}"
    else
        echo -e "${GREEN}All required system packages are already installed.${NC}"
    fi
else
    echo -e "${RED}[Warning] Arch Linux not detected. Please ensure you have installed:${NC}"
    echo "  - Hyprland, Waybar, Fuzzel, Foot, SwayNC, Hyprpaper, Hypridle, Hyprlock"
    echo "  - C++20, GTK4, gtk4-layer-shell, Libadwaita, Cairo, nlohmann-json"
    echo "  - Wireplumber, brightnessctl, playerctl, jq, cliphist, wl-clipboard, grim, slurp"
fi

# 2. Permissions on scripts
echo -e "${BLUE}[2/5] Setting executable permissions on scripts...${NC}"
chmod +x "${SCRIPT_DIR}/scripts/"*.sh "${SCRIPT_DIR}/scripts/"*.py

# 3. Compile Native C++ Suite
echo -e "${BLUE}[3/5] Compiling native C++ suite (Settings, TaskGraph, Toolbox, Popups)...${NC}"
make -C "${SCRIPT_DIR}" clean
make -C "${SCRIPT_DIR}" -j"$(nproc)"

# 4. Install Binaries to /usr/local/bin
echo -e "${BLUE}[4/5] Installing binaries to /usr/local/bin...${NC}"
sudo make -C "${SCRIPT_DIR}" install

# 5. Setup Systemd User Units
echo -e "${BLUE}[5/5] Configuring systemd --user service units...${NC}"
SYSTEMD_USER_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
mkdir -p "$SYSTEMD_USER_DIR"
cp -f "${SCRIPT_DIR}/systemd/"*.service "$SYSTEMD_USER_DIR/"
systemctl --user daemon-reload

echo -e "${GREEN}${BOLD}✓ Monolith installation completed successfully!${NC}"
echo ""
echo "To start your Monolith session:"
echo -e "  ${BOLD}Hyprland --config ~/.config/monolith/hyprland.conf${NC}"
echo ""
