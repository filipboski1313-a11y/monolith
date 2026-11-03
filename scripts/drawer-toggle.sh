#!/usr/bin/env python3
import json
import os
import subprocess

config_dir = os.path.expanduser("~/.config/monolith")
settings_path = os.path.join(config_dir, "settings.json")
wb_conf_path = os.path.join(config_dir, "waybar/config.jsonc")

try:
    with open(settings_path, "r", encoding="utf-8") as f:
        settings = json.load(f)
except Exception:
    settings = {}

wb_settings = settings.get("waybar", {})
current_drawer = wb_settings.get("drawer_enabled", True)
new_drawer = not current_drawer
wb_settings["drawer_enabled"] = new_drawer
settings["waybar"] = wb_settings

try:
    with open(settings_path, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=2)
except Exception as e:
    pass

try:
    with open(wb_conf_path, "r", encoding="utf-8") as f:
        wb_conf = json.load(f)

    drawer_mods = [
        "custom/drawer_toggle"
    ]
    if wb_settings.get("show_idle_inhibitor", True): drawer_mods.append("idle_inhibitor")
    if wb_settings.get("show_bluetooth", True): drawer_mods.append("bluetooth")
    if wb_settings.get("show_network", True): drawer_mods.append("network")
    if wb_settings.get("show_audio", True): drawer_mods.append("pulseaudio")
    if wb_settings.get("show_backlight", True): drawer_mods.append("backlight")
    if wb_settings.get("show_battery", True): drawer_mods.append("battery")
    if wb_settings.get("show_cpu", True): drawer_mods.append("cpu")
    if wb_settings.get("show_memory", True): drawer_mods.append("memory")
    if wb_settings.get("show_nightlight", True): drawer_mods.append("custom/nightlight")

    right = []
    if new_drawer:
        # Hover to reveal mode
        wb_conf["group/drawer"] = {
            "orientation": "horizontal",
            "drawer": {
                "transition-duration": 200,
                "children-class": "drawer-child",
                "transition-left-to-right": False,
                "click-to-reveal": False
            },
            "modules": drawer_mods
        }
        wb_conf["custom/drawer_toggle"] = {
            "format": "󰍜",
            "tooltip": "System Drawer (Hover to reveal, Click to pin)",
            "on-click": "~/.config/monolith/scripts/drawer-toggle.sh"
        }
        right.append("group/drawer")
    else:
        # Pinned open mode
        wb_conf["custom/drawer_toggle"] = {
            "format": "󰐃",
            "tooltip": "System Drawer [Pinned] (Click to collapse)",
            "on-click": "~/.config/monolith/scripts/drawer-toggle.sh"
        }
        for mod in drawer_mods:
            right.append(mod)

    right.append("custom/settings")
    if wb_settings.get("show_notifications", True): right.append("custom/notification")
    if wb_settings.get("show_power", True): right.append("custom/power")

    wb_conf["modules-right"] = right

    with open(wb_conf_path, "w", encoding="utf-8") as f:
        json.dump(wb_conf, f, indent=2)

    # Seamless reload of Waybar without restarting compositor or showing notifications
    subprocess.run(["pkill", "-SIGUSR2", "waybar"], stderr=subprocess.DEVNULL)
except Exception:
    pass
