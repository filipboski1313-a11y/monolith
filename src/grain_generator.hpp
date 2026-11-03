#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <cstdint>
#include <cairo.h>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace Monolith {

struct RGBColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

inline RGBColor parse_hex_rgb(const std::string& hex) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.length() < 6) h = "ffffff";
    try {
        int ir = std::stoi(h.substr(0, 2), nullptr, 16);
        int ig = std::stoi(h.substr(2, 2), nullptr, 16);
        int ib = std::stoi(h.substr(4, 2), nullptr, 16);
        return { ir / 255.0f, ig / 255.0f, ib / 255.0f };
    } catch (...) {
        return { 1.0f, 1.0f, 1.0f };
    }
}

// Generates procedural mathematical film grain on top of solid color or complex gradients
inline bool generate_procedural_grain(
    const std::string& output_path,
    int width,
    int height,
    const std::string& algorithm,
    double intensity,
    int scale,
    const std::string& gradient_type, // "none", "vertical", "horizontal", "diagonal_down", "diagonal_up", "mesh_3point"
    const std::string& color1_hex,    // Base / Bottom color
    const std::string& color2_hex,    // Top / Left / Point 2
    const std::string& color3_hex,    // Point 3 (for 3-color mesh: top-right lower)
    const std::string& tint_hex,      // Grain noise particle tint
    bool crosshair = false
) {
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1200;
    if (scale < 1) scale = 1;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return false;
    }

    uint32_t* data = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surface));
    int stride = cairo_image_surface_get_stride(surface) / 4;

    // Fast PRNG: xorshift64star
    struct XorShift {
        uint64_t state = 88172645463325252ULL;
        uint64_t next() {
            uint64_t x = state;
            x ^= x >> 12;
            x ^= x << 25;
            x ^= x >> 27;
            state = x;
            return x * 0x2545F4914F6CDD1DULL;
        }
        float next_float() {
            return (next() >> 40) * (1.0f / 16777216.0f);
        }
    } rng;

    RGBColor c1 = parse_hex_rgb(color1_hex);
    RGBColor c2 = parse_hex_rgb(color2_hex);
    RGBColor c3 = parse_hex_rgb(color3_hex);
    RGBColor tint = parse_hex_rgb(tint_hex);

    float std_dev = static_cast<float>(intensity);

    // Compute base background color for each pixel using procedural gradient math
    for (int y = 0; y < height; ++y) {
        float v = static_cast<float>(y) / static_cast<float>(height);
        for (int x = 0; x < width; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(width);

            RGBColor base = c1;

            if (gradient_type == "vertical") {
                // 2-Color Vertical: c2 at top (v=0), c1 at bottom (v=1)
                float t = v;
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "horizontal") {
                // 2-Color Horizontal: c2 at left (u=0), c1 at right (u=1)
                float t = u;
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "diagonal_down") {
                // 2-Color Diagonal: c2 at top-left, c1 at bottom-right
                float t = std::clamp((u + v) * 0.5f, 0.0f, 1.0f);
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "diagonal_up") {
                // 2-Color Diagonal: c2 at bottom-left, c1 at top-right
                float t = std::clamp((u + (1.0f - v)) * 0.5f, 0.0f, 1.0f);
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "mesh_3point") {
                // 3-Color Gradient:
                // Point 1 (c1): Bottom Center (u = 0.5, v = 1.0)
                // Point 2 (c2): Top-Left (u = 0.0, v = 0.0)
                // Point 3 (c3): Top-Right slightly lower (u = 1.0, v = 0.35)
                float d1_sq = (u - 0.5f) * (u - 0.5f) + (v - 1.0f) * (v - 1.0f);
                float d2_sq = (u - 0.0f) * (u - 0.0f) + (v - 0.0f) * (v - 0.0f);
                float d3_sq = (u - 1.0f) * (u - 1.0f) + (v - 0.35f) * (v - 0.35f);

                float w1 = 1.0f / (d1_sq + 0.10f);
                float w2 = 1.0f / (d2_sq + 0.10f);
                float w3 = 1.0f / (d3_sq + 0.10f);
                float total_w = w1 + w2 + w3;

                base.r = (c1.r * w1 + c2.r * w2 + c3.r * w3) / total_w;
                base.g = (c1.g * w1 + c2.g * w2 + c3.g * w3) / total_w;
                base.b = (c1.b * w1 + c2.b * w2 + c3.b * w3) / total_w;
            }

            // Generate grain noise value according to chosen mathematical distribution
            float noise_val = 0.0f;

            if (algorithm == "harmonic") {
                float fx = static_cast<float>(x);
                float fy = static_cast<float>(y);
                float wave1 = std::sin(fx * 0.12f) * std::cos(fy * 0.12f);
                float wave2 = std::sin(fx * 0.35f + fy * 0.28f);
                float uniform_noise = (rng.next_float() - 0.5f) * 2.0f;
                noise_val = (wave1 * 0.35f + wave2 * 0.35f + uniform_noise * 0.30f) * std_dev;
            } else if (algorithm == "quantum") {
                float threshold = 1.0f - (std_dev / 100.0f);
                float sample = rng.next_float();
                if (sample > threshold) {
                    float factor = (sample - threshold) / (1.0f - threshold);
                    noise_val = factor * 255.0f;
                }
            } else if (algorithm == "dither") {
                noise_val = (rng.next_float() - 0.5f) * 2.0f * std_dev;
            } else {
                // Gaussian Box-Muller Normal distribution
                float u1 = std::max(rng.next_float(), 1e-7f);
                float u2 = rng.next_float();
                noise_val = std_dev * std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
            }

            // Blend grain into gradient base
            int r = std::clamp(static_cast<int>(base.r * 255.0f + noise_val * tint.r), 0, 255);
            int g = std::clamp(static_cast<int>(base.g * 255.0f + noise_val * tint.g), 0, 255);
            int b = std::clamp(static_cast<int>(base.b * 255.0f + noise_val * tint.b), 0, 255);

            data[y * stride + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }

    // Optional subtle architectural crosshair in the center
    if (crosshair) {
        int cx = width / 2;
        int cy = height / 2;
        int sz = 12;
        uint32_t ch_color = (0xFF << 24) | (static_cast<int>(tint.r * 180) << 16) | (static_cast<int>(tint.g * 180) << 8) | static_cast<int>(tint.b * 180);
        for (int dx = -sz; dx <= sz; ++dx) {
            if (cx + dx >= 0 && cx + dx < width) data[cy * stride + (cx + dx)] = ch_color;
        }
        for (int dy = -sz; dy <= sz; ++dy) {
            if (cy + dy >= 0 && cy + dy < height) data[(cy + dy) * stride + cx] = ch_color;
        }
    }

    cairo_surface_mark_dirty(surface);
    cairo_status_t st = cairo_surface_write_to_png(surface, output_path.c_str());
    cairo_surface_destroy(surface);

    return (st == CAIRO_STATUS_SUCCESS);
}

} // namespace Monolith
