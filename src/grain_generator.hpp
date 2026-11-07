#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cairo.h>

namespace Monolith {

struct RGBColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

inline RGBColor parse_hex_rgb(const std::string& hex) {
    std::string h = hex;
    while (!h.empty() && (h[0] == '#' || h[0] == ' ')) h.erase(h.begin());
    if (h.length() == 3) {
        h = std::string(2, h[0]) + std::string(2, h[1]) + std::string(2, h[2]);
    }
    if (h.length() < 6) h = "000000";
    try {
        int ir = std::stoi(h.substr(0, 2), nullptr, 16);
        int ig = std::stoi(h.substr(2, 2), nullptr, 16);
        int ib = std::stoi(h.substr(4, 2), nullptr, 16);
        return { ir / 255.0f, ig / 255.0f, ib / 255.0f };
    } catch (...) {
        return { 0.0f, 0.0f, 0.0f };
    }
}

// Smooth Hermite interpolation to prevent linear gradient crease lines
inline float smoothstep_f(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Renders procedural mathematical gradient & film grain directly into a Cairo ARGB32 surface
inline void render_procedural_grain_to_surface(
    cairo_surface_t* surface,
    const std::string& algorithm,
    double intensity,
    int scale,
    const std::string& gradient_type, // "none", "vertical", "horizontal", "diagonal_down", "diagonal_up", "mesh_3point", "radial"
    const std::string& color1_hex,    // Base / Bottom color
    const std::string& color2_hex,    // Top / Left / Point 2
    const std::string& color3_hex,    // Point 3 (for 3-color mesh)
    const std::string& tint_hex,      // Grain noise particle tint
    bool crosshair = false
) {
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) return;

    cairo_surface_flush(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    if (width <= 0 || height <= 0) return;

    scale = std::clamp(scale, 1, 6);

    uint32_t* data = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surface));
    int stride = cairo_image_surface_get_stride(surface) / 4;

    // Fast deterministic XorShift64* PRNG
    struct XorShift {
        uint64_t state = 88172645463325252ULL;
        inline uint64_t next() {
            uint64_t x = state;
            x ^= x >> 12;
            x ^= x << 25;
            x ^= x >> 27;
            state = x;
            return x * 0x2545F4914F6CDD1DULL;
        }
        inline float next_float() {
            return (next() >> 40) * (1.0f / 16777216.0f);
        }
    } rng;

    RGBColor c1 = parse_hex_rgb(color1_hex);
    RGBColor c2 = parse_hex_rgb(color2_hex);
    RGBColor c3 = parse_hex_rgb(color3_hex);
    RGBColor tint = parse_hex_rgb(tint_hex);

    float std_dev = static_cast<float>(intensity);
    bool is_pure_black_nograd = (gradient_type == "none" && c1.r < 0.002f && c1.g < 0.002f && c1.b < 0.002f && std_dev <= 0.05f && !crosshair);
    if (is_pure_black_nograd) {
        for (int y = 0; y < height; ++y) {
            uint32_t* row = data + y * stride;
            std::fill(row, row + width, 0xFF000000U);
        }
        cairo_surface_mark_dirty(surface);
        return;
    }

    int grid_w = (width + scale - 1) / scale;
    int grid_h = (height + scale - 1) / scale;
    std::vector<float> noise_grid(static_cast<size_t>(grid_w * grid_h), 0.0f);

    if (std_dev > 0.02f) {
        if (algorithm == "harmonic") {
            for (int gy = 0; gy < grid_h; ++gy) {
                float fy = static_cast<float>(gy);
                for (int gx = 0; gx < grid_w; ++gx) {
                    float fx = static_cast<float>(gx);
                    float wave1 = std::sin(fx * 0.14f) * std::cos(fy * 0.14f);
                    float wave2 = std::sin(fx * 0.37f + fy * 0.29f);
                    float uniform_noise = (rng.next_float() - 0.5f) * 2.0f;
                    noise_grid[gy * grid_w + gx] = (wave1 * 0.35f + wave2 * 0.35f + uniform_noise * 0.30f) * std_dev;
                }
            }
        } else if (algorithm == "quantum") {
            float threshold = std::clamp(1.0f - (std_dev / 85.0f), 0.10f, 0.999f);
            for (int i = 0; i < grid_w * grid_h; ++i) {
                float sample = rng.next_float();
                if (sample > threshold) {
                    float factor = (sample - threshold) / (1.0f - threshold);
                    noise_grid[i] = factor * factor * 210.0f;
                }
            }
        } else if (algorithm == "dither") {
            for (int i = 0; i < grid_w * grid_h; ++i) {
                noise_grid[i] = (rng.next_float() - 0.5f) * 2.2f * std_dev;
            }
        } else {
            // Gaussian Box-Muller (paired generation for 2x throughput)
            int total = grid_w * grid_h;
            for (int i = 0; i < total; i += 2) {
                float u1 = std::max(rng.next_float(), 1e-7f);
                float u2 = rng.next_float();
                float mag = std_dev * std::sqrt(-2.0f * std::log(u1));
                noise_grid[i] = mag * std::cos(6.2831853f * u2);
                if (i + 1 < total) {
                    noise_grid[i + 1] = mag * std::sin(6.2831853f * u2);
                }
            }
        }
    }

    float inv_w = 1.0f / static_cast<float>(std::max(1, width - 1));
    float inv_h = 1.0f / static_cast<float>(std::max(1, height - 1));

    for (int y = 0; y < height; ++y) {
        float v = static_cast<float>(y) * inv_h;
        int gy = y / scale;
        uint32_t* row = data + y * stride;

        for (int x = 0; x < width; ++x) {
            float u = static_cast<float>(x) * inv_w;
            RGBColor base = c1;

            if (gradient_type == "vertical") {
                float t = smoothstep_f(v);
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "horizontal") {
                float t = smoothstep_f(u);
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "diagonal_down") {
                float t = smoothstep_f((u + v) * 0.5f);
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "diagonal_up") {
                float t = smoothstep_f((u + (1.0f - v)) * 0.5f);
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "radial") {
                float dx = u - 0.5f;
                float dy = v - 0.5f;
                float dist = std::min(1.0f, std::sqrt(dx * dx + dy * dy) * 1.4142f);
                float t = smoothstep_f(dist);
                base.r = c2.r * (1.0f - t) + c1.r * t;
                base.g = c2.g * (1.0f - t) + c1.g * t;
                base.b = c2.b * (1.0f - t) + c1.b * t;
            } else if (gradient_type == "mesh_3point") {
                float d1_sq = (u - 0.5f) * (u - 0.5f) + (v - 1.0f) * (v - 1.0f);
                float d2_sq = (u - 0.0f) * (u - 0.0f) + (v - 0.0f) * (v - 0.0f);
                float d3_sq = (u - 1.0f) * (u - 1.0f) + (v - 0.32f) * (v - 0.32f);

                float w1 = 1.0f / (d1_sq + 0.12f);
                float w2 = 1.0f / (d2_sq + 0.12f);
                float w3 = 1.0f / (d3_sq + 0.12f);
                float inv_tot = 1.0f / (w1 + w2 + w3);

                base.r = (c1.r * w1 + c2.r * w2 + c3.r * w3) * inv_tot;
                base.g = (c1.g * w1 + c2.g * w2 + c3.g * w3) * inv_tot;
                base.b = (c1.b * w1 + c2.b * w2 + c3.b * w3) * inv_tot;
            }

            float noise_val = noise_grid[gy * grid_w + (x / scale)];
            // Anti-banding sub-LSB triangular dither so dark OLED gradients never band
            float anti_band = (rng.next_float() - 0.5f) * 0.75f;

            int r = std::clamp(static_cast<int>(base.r * 255.0f + noise_val * tint.r + anti_band), 0, 255);
            int g = std::clamp(static_cast<int>(base.g * 255.0f + noise_val * tint.g + anti_band), 0, 255);
            int b = std::clamp(static_cast<int>(base.b * 255.0f + noise_val * tint.b + anti_band), 0, 255);

            row[x] = (0xFFU << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }

    // Optional architectural reticle + subtle rule-of-thirds corner ticks
    if (crosshair) {
        int cx = width / 2;
        int cy = height / 2;
        int sz = 14;
        uint32_t ch_color = (0xFFU << 24)
                          | (static_cast<uint32_t>(tint.r * 150.0f) << 16)
                          | (static_cast<uint32_t>(tint.g * 150.0f) << 8)
                          | static_cast<uint32_t>(tint.b * 150.0f);
        for (int dx = -sz; dx <= sz; ++dx) {
            if (std::abs(dx) >= 3 && cx + dx >= 0 && cx + dx < width) {
                data[cy * stride + (cx + dx)] = ch_color;
            }
        }
        for (int dy = -sz; dy <= sz; ++dy) {
            if (std::abs(dy) >= 3 && cy + dy >= 0 && cy + dy < height) {
                data[(cy + dy) * stride + cx] = ch_color;
            }
        }
    }

    cairo_surface_mark_dirty(surface);
}

inline bool generate_procedural_grain(
    const std::string& output_path,
    int width,
    int height,
    const std::string& algorithm,
    double intensity,
    int scale,
    const std::string& gradient_type,
    const std::string& color1_hex,
    const std::string& color2_hex,
    const std::string& color3_hex,
    const std::string& tint_hex,
    bool crosshair = false
) {
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1200;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return false;
    }

    render_procedural_grain_to_surface(
        surface,
        algorithm,
        intensity,
        scale,
        gradient_type,
        color1_hex,
        color2_hex,
        color3_hex,
        tint_hex,
        crosshair
    );

    cairo_status_t st = cairo_surface_write_to_png(surface, output_path.c_str());
    cairo_surface_destroy(surface);
    return (st == CAIRO_STATUS_SUCCESS);
}

} // namespace Monolith
