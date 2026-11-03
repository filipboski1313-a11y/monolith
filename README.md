# 🏛️ MONOLITH HYPRLAND SHELL

> **Ultra-Minimalist • Razor-Sharp • Zero-Bloat • High Performance Native C++20 Desktop Shell**

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Compositor](https://img.shields.io/badge/Compositor-Hyprland-00a8ff.svg)](https://hyprland.org)
[![Platform](https://img.shields.io/badge/Platform-Wayland%20%7C%20Linux-success.svg)](https://wayland.freedesktop.org)
[![Aesthetic](https://img.shields.io/badge/Aesthetic-OLED%20Brutalist-000000.svg)](https://github.com)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## 🌟 Overview

**Monolith** is an integrated, custom-crafted graphical environment for Hyprland. Built from scratch in **C++20**, **GTK4**, and **gtk4-layer-shell**, it focuses on uncompromising performance, sub-millisecond responsiveness with zero overhead (no Electron, no bloated scripting frameworks), and pure aesthetic clarity: **pure OLED Black (`#000000`)** paired with architectural silver typography and customizable multi-stop gradients with mathematical film grain.

---

## ⚡ Core Native Modules

### 1. `monolith-settings` (Control Center)
- Rebuilt native GTK4 + Libadwaita control panel.
- **Sidebar Category Navigation**: Clean, organized categories on the left side.
- **Top HeaderBar Search**: Fast, dynamic search entry (`GtkSearchEntry`) for settings, physics parameters, and colors.
- **Bottom Right Apply Button**: Prominent "Apply Settings" button for instant one-click execution.
- **Interactive Bézier Curve Editor**: Custom Cairo widget for real-time visualization and tuning of window and workspace animation curves.
- **Mathematical Gradient & Film Grain Engine**: Algorithmic wallpaper rendering in RAM in `< 2 ms` (Gaussian normal distribution / harmonic modulation / quantum dots) across 2-color (vertical, horizontal, diagonal) and 3-color mesh gradients.
- **Visual Color Customization**: Full visual palette control over window borders, bar backgrounds, borders, module icons, active workspace accents, and gradient stops.

### 2. `monolith-taskgraph` (Desktop Task Graph)
- Interactive force-directed process graph rendered directly on the wallpaper/background layer (*layer: bottom/background*).
- **Event-Driven Synchronization**: Direct integration with Hyprland `socket2` — the graph wakes only on window state changes.
- **Zero-Overhead Freeze**: When windows cover the desktop, physics simulation and rendering completely freeze (0% CPU/GPU usage).
- **Menu Repulsion**: Dynamic radial repulsion pushing graph nodes outward when the application launcher opens.
- **OLED Context Menu**: Window focusing, process launching, pinning, and RAM pagecache prefetching.

### 3. `monolith-toolbox` (Edge Drawer Utility)
- Edge drawer with smooth hover tracking and dynamic cursor-following indicator (`gtk4-layer-shell`).
- Built-in utilities:
  - 󰔛 **Stopwatch**: Millisecond-accurate stopwatch with split/lap recording.
  - 󰃬 **Calculator**: Keyboard-aware mathematical expression engine.
  - 󰽉 **Cairo Canvas**: Smooth freehand drawing canvas with instant Wayland clipboard copy.
  - 󰈙 **Scratchpad**: Persistent auto-saving text notes.

### 4. `monolith-popup` (Native Status Bar Popups)
- Microsecond-fast OLED overlay popups for Waybar modules (`gtk4-layer-shell`):
  - 󰕾 **Audio**: Volume sliders, source microphone controls, quick mute toggle, and Pavucontrol launcher.
  - 󰖩 **Network**: Wi-Fi radio switch, network list with signal strength, and one-click connection.
  - 󰂯 **Bluetooth**: Radio power switch, paired device list, quick connect/disconnect, and pairing launcher.
  - 󰍛 **System Resources**: Live CPU load and RAM usage bars with top process monitor.
  - 󰐥 **Power Menu**: Clean action buttons (Lock Screen, Suspend, Reboot, Power Off, Log Out).
- **Smart Toggle**: Clicking the bar icon again, clicking outside, or pressing `Escape` dismisses the popup instantly.

### 5. Waybar Pinnable System Drawer
- **Minimalist Hover Mode**: Drawer slides out smoothly on mouse hover, auto-collapsing when the mouse departs.
- **One-Click Pinning**: Clicking the drawer icon (`󰍜`) pins the drawer permanently open on the bar; clicking again unpins it.
- Seamless in-place reload via `SIGUSR2` with zero desktop notifications and zero compositor restarts.

### 6. Spatial Tabs & Window Shelf
- **Spatial Tabs**:
  - `Up (▲)`: Dedicated Spotify media workspace (`special:spotify`).
  - `Down (▼)`: Dedicated Extra shelf workspace (`special:extra`).
  - Single-step barrier gestures with vertical sliding animations.
- **Window Shelf**:
  - `Super + S`: Stash active window to the hidden Extra shelf.
  - Quick recall restoring stashed windows back to the active desktop.

### 7. `monolith-ram-preloader` (Pagecache Prefetcher)
- Prefetches key binaries and shared libraries into Linux kernel pagecache via `posix_fadvise`, delivering ~0ms cold-start launch times.

---

## ⌨️ Keybindings

| Shortcut | Action |
| :--- | :--- |
| `Super + Enter` | Launch terminal (`foot`) |
| `Super + Space` / `Super` / `Super + D` | Application launcher (`fuzzel`) |
| `Super + I` / `Super + ,` | Monolith Settings (`monolith-settings`) |
| `Super + Z` | Toggle edge toolbox (`monolith-toolbox`) |
| `Super + Q` / `Super + C` | Close active window |
| `Super + V` | Toggle floating window |
| `Super + F` | Toggle fullscreen |
| `Super + M` | Toggle Spotify media tab |
| `Super + S` / `Super + ~` | Toggle Extra shelf tab |
| `Super + Shift + V` | Clipboard history (`cliphist` + `fuzzel`) |
| `Super + N` | Toggle night light filter (`hyprsunset`) |
| `Super + Backspace` | Power management menu (`powermenu.sh`) |
| `Super + L` | Lock screen (`hyprlock`) |
| `Print` / `Super + Print` | Screenshot (fullscreen / selection) |
| `3-finger vertical swipe` | Smooth transition between Spotify / Desktop / Extra Shelf |

---

## 📦 Dependencies

### System Packages & Libraries:
- **Wayland Compositor & Core Tools**: `hyprland`, `waybar`, `fuzzel`, `foot`, `swaync`, `hyprpaper`, `hypridle`, `hyprlock`, `hyprsunset`
- **Compiler & C++ Headers**: `gcc` (C++20 support), `pkg-config`, `libadwaita-1`, `gtk4`, `gtk4-layer-shell`, `cairo`, `nlohmann-json`
- **Audio & CLI Utilities**: `wireplumber`, `pipewire`, `brightnessctl`, `playerctl`, `jq`, `cliphist`, `wl-clipboard`, `grim`, `slurp`
- **Python (Grain generator & Prefetcher)**: `python`, `python-numpy`, `python-pillow`

---

## 🚀 Installation

1. Clone repository to your config directory:
   ```bash
   git clone https://github.com/USER/monolith-hyprland.git ~/.config/monolith
   cd ~/.config/monolith
   ```

2. Run automated installer:
   ```bash
   ./install.sh
   ```
   *The installer verifies and installs required packages (on Arch Linux), compiles all C++20 binaries, installs them to `/usr/local/bin`, and activates systemd user units.*

3. Start your Monolith session:
   ```bash
   Hyprland --config ~/.config/monolith/hyprland.conf
   ```

---

## 📂 Directory Structure

```text
~/.config/monolith/
├── bin/                 # Compiled native C++ binaries
├── foot/                # Foot terminal configuration
├── fuzzel/              # Fuzzel launcher configuration
├── scripts/             # Utility scripts and automation
├── src/                 # Source code for monolith-settings (C++20, Libadwaita, Cairo)
├── systemd/             # Systemd --user service units
├── taskgraph/           # Source code for monolith-taskgraph (C++20, Layer Shell)
├── toolbox/             # Source code for monolith-toolbox (C++20, Layer Shell)
├── popup/               # Source code for monolith-popup (C++20, Layer Shell)
├── swaync/              # SwayNC notification center configuration
├── waybar/              # Waybar status bar configuration and OLED styles
├── hyprland.conf        # Main compositor configuration
├── hypridle.conf        # Idle management configuration
├── hyprlock.conf        # Screen locker configuration
├── hyprpaper.conf       # Wallpaper manager configuration
├── install.sh           # Automated installation script
├── Makefile             # Root build system Makefile
└── README.md
```

---

## 📜 License

Released under the [MIT License](LICENSE).
