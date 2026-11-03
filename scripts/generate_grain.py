#!/usr/bin/env python3
"""
Monolith Mathematical Gradient & Film Grain Wallpaper Generator
Generates procedural pixel grain fields over solid OLED black or multi-stop gradients.
100% mathematical functions - Zero static asset reliance.
"""

import sys
import os
import json
import math
import numpy as np
from PIL import Image

CONFIG_DIR = os.path.expanduser("~/.config/monolith")
WALLPAPER_PATH = os.path.join(CONFIG_DIR, "wallpaper.png")
SETTINGS_JSON = os.path.join(CONFIG_DIR, "settings.json")


def hex_to_rgb(hex_str):
    hex_str = hex_str.lstrip("#")
    if len(hex_str) == 3:
        hex_str = "".join([c * 2 for c in hex_str])
    if len(hex_str) != 6:
        return (212, 212, 216)
    return tuple(int(hex_str[i:i+2], 16) for i in (0, 2, 4))


def generate_gradient_grain(
    width=1920,
    height=1200,
    intensity=14.0,
    algorithm="gaussian",
    scale=1,
    gradient_type="none",
    color1="#000000",
    color2="#18181b",
    color3="#27272a",
    tint="#d4d4d8",
    crosshair=False
):
    eff_w = width // max(1, scale)
    eff_h = height // max(1, scale)

    # Grid normalized coordinates (0.0 to 1.0)
    y_idx, x_idx = np.indices((eff_h, eff_w), dtype=np.float32)
    u = x_idx / float(eff_w)
    v = y_idx / float(eff_h)

    c1 = np.array(hex_to_rgb(color1), dtype=np.float32)
    c2 = np.array(hex_to_rgb(color2), dtype=np.float32)
    c3 = np.array(hex_to_rgb(color3), dtype=np.float32)
    t_rgb = np.array(hex_to_rgb(tint), dtype=np.float32) / 255.0

    # Base gradient calculation
    if gradient_type == "vertical":
        t = v[..., np.newaxis]
        base_rgb = c2 * (1.0 - t) + c1 * t
    elif gradient_type == "horizontal":
        t = u[..., np.newaxis]
        base_rgb = c2 * (1.0 - t) + c1 * t
    elif gradient_type == "diagonal_down":
        t = np.clip((u + v) * 0.5, 0.0, 1.0)[..., np.newaxis]
        base_rgb = c2 * (1.0 - t) + c1 * t
    elif gradient_type == "diagonal_up":
        t = np.clip((u + (1.0 - v)) * 0.5, 0.0, 1.0)[..., np.newaxis]
        base_rgb = c2 * (1.0 - t) + c1 * t
    elif gradient_type == "mesh_3point":
        # Point 1 (c1): bottom center (0.5, 1.0)
        # Point 2 (c2): top left (0.0, 0.0)
        # Point 3 (c3): top right lower (1.0, 0.35)
        d1_sq = (u - 0.5) ** 2 + (v - 1.0) ** 2
        d2_sq = (u - 0.0) ** 2 + (v - 0.0) ** 2
        d3_sq = (u - 1.0) ** 2 + (v - 0.35) ** 2

        w1 = 1.0 / (d1_sq + 0.10)
        w2 = 1.0 / (d2_sq + 0.10)
        w3 = 1.0 / (d3_sq + 0.10)
        total_w = w1 + w2 + w3

        w1_n = (w1 / total_w)[..., np.newaxis]
        w2_n = (w2 / total_w)[..., np.newaxis]
        w3_n = (w3 / total_w)[..., np.newaxis]

        base_rgb = c1 * w1_n + c2 * w2_n + c3 * w3_n
    else:
        # Solid background
        base_rgb = np.full((eff_h, eff_w, 3), c1, dtype=np.float32)

    # Procedural film grain noise
    if algorithm == "harmonic":
        fx = x_idx * 0.12
        fy = y_idx * 0.12
        wave1 = np.sin(fx) * np.cos(fy)
        wave2 = np.sin(x_idx * 0.35 + y_idx * 0.28)
        uniform_noise = (np.random.rand(eff_h, eff_w) - 0.5) * 2.0
        noise = (wave1 * 0.35 + wave2 * 0.35 + uniform_noise * 0.30) * intensity
    elif algorithm == "quantum":
        threshold = 1.0 - (intensity / 100.0)
        sample = np.random.rand(eff_h, eff_w)
        mask = sample > threshold
        noise = np.where(mask, ((sample - threshold) / max(1e-5, (1.0 - threshold))) * 255.0, 0.0)
    elif algorithm == "dither":
        noise = (np.random.rand(eff_h, eff_w) - 0.5) * 2.0 * intensity
    else:
        # Gaussian Box-Muller Normal
        noise = np.random.normal(loc=0.0, scale=intensity, size=(eff_h, eff_w))

    noise_3d = noise[..., np.newaxis] * t_rgb
    final_rgb = np.clip(base_rgb + noise_3d, 0.0, 255.0).astype(np.uint8)

    # Crosshair
    if crosshair:
        cx, cy = eff_w // 2, eff_h // 2
        sz = 12
        ch_val = np.clip(t_rgb * 180, 0, 255).astype(np.uint8)
        for dx in range(-sz, sz + 1):
            if 0 <= cx + dx < eff_w:
                final_rgb[cy, cx + dx] = ch_val
        for dy in range(-sz, sz + 1):
            if 0 <= cy + dy < eff_h:
                final_rgb[cy + dy, cx] = ch_val

    # Scale up if needed
    if scale > 1:
        final_rgb = np.repeat(np.repeat(final_rgb, scale, axis=0), scale, axis=1)
        final_rgb = final_rgb[:height, :width]

    img = Image.fromarray(final_rgb, "RGB")
    img.save(WALLPAPER_PATH, "PNG", optimize=False)
    return WALLPAPER_PATH


if __name__ == "__main__":
    intensity = 14.0
    algorithm = "gaussian"
    gradient_type = "none"
    color1 = "#000000"
    color2 = "#18181b"
    color3 = "#27272a"
    tint = "#d4d4d8"
    scale = 1
    crosshair = False

    if os.path.exists(SETTINGS_JSON):
        try:
            with open(SETTINGS_JSON, "r") as f:
                d = json.load(f)
                gr = d.get("grain", {})
                intensity = float(gr.get("intensity", 14.0))
                algorithm = gr.get("algorithm", "gaussian")
                scale = int(gr.get("scale", 1))
                gradient_type = gr.get("gradient_type", "none")
                color1 = gr.get("color1", gr.get("base_color", "#000000"))
                color2 = gr.get("color2", "#18181b")
                color3 = gr.get("color3", "#27272a")
                tint = gr.get("tint", "#d4d4d8")
                crosshair = bool(gr.get("crosshair", False))
        except Exception as e:
            print(f"Error loading settings: {e}", file=sys.stderr)

    out = generate_gradient_grain(
        width=1920,
        height=1200,
        intensity=intensity,
        algorithm=algorithm,
        scale=scale,
        gradient_type=gradient_type,
        color1=color1,
        color2=color2,
        color3=color3,
        tint=tint,
        crosshair=crosshair
    )
    print(f"Generated procedural gradient grain wallpaper at {out}")
