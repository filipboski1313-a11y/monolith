#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <cairo.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <glob.h>

using json = nlohmann::json;

// ══════════════════════════════════════════════════════════════════════════════
// MONOLITH TASK GRAPH ("Task Graph Engine")
// Ultra-Optimized, Event-Driven Desktop Task Graph:
//   - Zero-overhead freeze: When windows are open, graph stops rendering and computing
//   - Fuzzel App Menu repulsion: Pushes nodes outward when Super menu opens
//   - 100% Solid OLED Film Grain texture on Black
//   - Right-click OLED Menu: Pin/Unpin, Load to RAM, Focus/Launch
//   - Event-driven wakeup via Hyprland socket2
// ══════════════════════════════════════════════════════════════════════════════

struct AppNode {
    std::string id;
    std::string name;
    std::string icon_name;
    std::string exec_cmd;
    std::string match_class;
    std::string hypr_address;
    int workspace_id = 0;
    bool is_pinned = false;
    bool is_running = false;
    bool is_hovered = false;
    bool is_dragged = false;
    bool load_to_ram = false;

    double x = 0.0, y = 0.0;
    double vx = 0.0, vy = 0.0;
    double radius = 28.0;
    double cur_scale = 1.0;

    // Harmonic ambient drift
    double drift_phase = 0.0;
    double drift_speed = 0.22;

    GdkPixbuf* pixbuf = nullptr;
};

struct AppEdge {
    std::string from_id;
    std::string to_id;
    double rest_length = 340.0;
};

struct GraphState {
    GtkWidget* win = nullptr;
    GtkWidget* da = nullptr;
    GtkIconTheme* icon_theme = nullptr;

    std::vector<AppNode> nodes;
    std::vector<AppEdge> edges;
    std::unordered_map<std::string, GdkPixbuf*> icon_cache;

    cairo_surface_t* bg_surface = nullptr;

    int screen_w = 1536;
    int screen_h = 931;

    // Dynamic physics and visual settings loaded from settings.json
    double spring_k = 0.03;
    double rest_length = 250.0;
    double repulsion = 12000.0;
    double center_gravity = 0.008;
    double damping = 0.88;
    double drift_speed = 0.20;
    double edge_width = 1.0;
    double edge_alpha = 0.45;
    double node_radius = 16.0;
    double repulsion_radius = 620.0;
    double fuzzel_repulsion = 85.0;
    bool auto_freeze = true;

    // Procedural Algorithmic Grain settings loaded from settings.json
    double grain_intensity = 14.4;
    std::string grain_algorithm = "gaussian";
    std::string grain_tint = "#d4d4d8";
    int grain_scale = 1;

    // Simulation & Freeze state
    double sim_time = 0.0;
    double alpha = 1.0;
    bool physics_active = true;
    bool is_covered = false;       // TRUE when desktop is actually covered (FROZEN)
    bool fuzzel_active = false;    // TRUE when app menu (Super) is open (Repulsion field)

    // Interaction state
    int dragged_node_idx = -1;
    double drag_start_x = 0.0;
    double drag_start_y = 0.0;
    double drag_dist = 0.0;

    // Right-click context menu
    bool menu_open = false;
    int menu_node_idx = -1;
    double menu_x = 0.0;
    double menu_y = 0.0;
    double menu_w = 260.0;
    double menu_h = 138.0;
    int menu_hovered_item = -1;

    guint physics_timer_id = 0;
    guint hypr_sync_timer_id = 0;
};

static GraphState g_graph;

static void wake_physics(double new_alpha = 0.6) {
    g_graph.alpha = std::max(g_graph.alpha, new_alpha);
    g_graph.physics_active = true;
    if (g_graph.da && !g_graph.is_covered) {
        gtk_widget_queue_draw(g_graph.da);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// ICON & BACKGROUND LOADERS
// ──────────────────────────────────────────────────────────────────────────────
static GdkPixbuf* get_icon_pixbuf(const std::string& icon_name, int size = 32) {
    if (g_graph.icon_cache.find(icon_name) != g_graph.icon_cache.end()) {
        return g_graph.icon_cache[icon_name];
    }

    if (!g_graph.icon_theme) {
        g_graph.icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    }

    GdkPixbuf* pb = nullptr;
    GtkIconPaintable* icon = gtk_icon_theme_lookup_icon(
        g_graph.icon_theme,
        icon_name.c_str(),
        nullptr,
        size,
        1,
        GTK_TEXT_DIR_NONE,
        GTK_ICON_LOOKUP_FORCE_REGULAR
    );

    if (icon) {
        GFile* f = gtk_icon_paintable_get_file(icon);
        if (f) {
            char* path = g_file_get_path(f);
            if (path) {
                GError* err = nullptr;
                pb = gdk_pixbuf_new_from_file_at_scale(path, size, size, TRUE, &err);
                if (err) g_error_free(err);
                g_free(path);
            }
            g_object_unref(f);
        }
        g_object_unref(icon);
    }

    if (!pb) {
        icon = gtk_icon_theme_lookup_icon(g_graph.icon_theme, "application-x-executable", nullptr, size, 1, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_FORCE_REGULAR);
        if (icon) {
            GFile* f = gtk_icon_paintable_get_file(icon);
            if (f) {
                char* path = g_file_get_path(f);
                if (path) {
                    pb = gdk_pixbuf_new_from_file_at_scale(path, size, size, TRUE, nullptr);
                    g_free(path);
                }
                g_object_unref(f);
            }
            g_object_unref(icon);
        }
    }

    g_graph.icon_cache[icon_name] = pb;
    return pb;
}

static void generate_algorithmic_grain() {
    int w = std::max(g_graph.screen_w, 1920);
    int h = std::max(g_graph.screen_h, 1200);

    if (g_graph.bg_surface) {
        cairo_surface_destroy(g_graph.bg_surface);
        g_graph.bg_surface = nullptr;
    }

    g_graph.bg_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(g_graph.bg_surface) != CAIRO_STATUS_SUCCESS) return;

    uint32_t* data = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(g_graph.bg_surface));
    int stride = cairo_image_surface_get_stride(g_graph.bg_surface) / 4;

    if (g_graph.grain_intensity <= 0.05) {
        // Pure solid OLED black (#000000)
        memset(data, 0, stride * h * sizeof(uint32_t));
        cairo_surface_mark_dirty(g_graph.bg_surface);
        return;
    }

    // High performance xorshift64star PRNG
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

    std::string hex = g_graph.grain_tint;
    if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
    if (hex.length() < 6) hex = "d4d4d8";
    float tr = std::stoi(hex.substr(0, 2), nullptr, 16) / 255.0f;
    float tg = std::stoi(hex.substr(2, 2), nullptr, 16) / 255.0f;
    float tb = std::stoi(hex.substr(4, 2), nullptr, 16) / 255.0f;

    float std_dev = static_cast<float>(g_graph.grain_intensity);
    int scale = std::clamp(g_graph.grain_scale, 1, 4);

    if (g_graph.grain_algorithm == "harmonic") {
        for (int y = 0; y < h; ++y) {
            float fy = static_cast<float>(y / scale);
            for (int x = 0; x < w; ++x) {
                float fx = static_cast<float>(x / scale);
                float wave1 = std::sin(fx * 0.12f) * std::cos(fy * 0.12f);
                float wave2 = std::sin(fx * 0.35f + fy * 0.28f);
                float noise = (rng.next_float() - 0.5f) * 2.0f;
                float combined = (wave1 * 0.4f + wave2 * 0.3f + noise * 0.3f) * std_dev;
                int v = std::clamp(static_cast<int>(combined), 0, 255);
                int r = std::clamp(static_cast<int>(v * tr), 0, 255);
                int g = std::clamp(static_cast<int>(v * tg), 0, 255);
                int b = std::clamp(static_cast<int>(v * tb), 0, 255);
                data[y * stride + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
    } else if (g_graph.grain_algorithm == "quantum") {
        float threshold = 1.0f - (std_dev / 100.0f);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float sample = rng.next_float();
                int v = 0;
                if (sample > threshold) {
                    float factor = (sample - threshold) / (1.0f - threshold);
                    v = std::clamp(static_cast<int>(factor * 255.0f), 0, 255);
                }
                int r = std::clamp(static_cast<int>(v * tr), 0, 255);
                int g = std::clamp(static_cast<int>(v * tg), 0, 255);
                int b = std::clamp(static_cast<int>(v * tb), 0, 255);
                data[y * stride + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
    } else if (g_graph.grain_algorithm == "dither") {
        float max_amp = std_dev * 2.5f;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float val = rng.next_float() * max_amp;
                int v = std::clamp(static_cast<int>(val), 0, 255);
                int r = std::clamp(static_cast<int>(v * tr), 0, 255);
                int g = std::clamp(static_cast<int>(v * tg), 0, 255);
                int b = std::clamp(static_cast<int>(v * tb), 0, 255);
                data[y * stride + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
    } else {
        // Gaussian Box-Muller (default)
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; x += 2) {
                float u1 = std::max(rng.next_float(), 1e-7f);
                float u2 = rng.next_float();
                float mag = std_dev * std::sqrt(-2.0f * std::log(u1));
                float z0 = mag * std::cos(6.2831853f * u2);
                float z1 = mag * std::sin(6.2831853f * u2);

                int v0 = std::clamp(static_cast<int>(z0), 0, 255);
                int r0 = std::clamp(static_cast<int>(v0 * tr), 0, 255);
                int g0 = std::clamp(static_cast<int>(v0 * tg), 0, 255);
                int b0 = std::clamp(static_cast<int>(v0 * tb), 0, 255);
                data[y * stride + x] = (0xFF << 24) | (r0 << 16) | (g0 << 8) | b0;

                if (x + 1 < w) {
                    int v1 = std::clamp(static_cast<int>(z1), 0, 255);
                    int r1 = std::clamp(static_cast<int>(v1 * tr), 0, 255);
                    int g1 = std::clamp(static_cast<int>(v1 * tg), 0, 255);
                    int b1 = std::clamp(static_cast<int>(v1 * tb), 0, 255);
                    data[y * stride + (x + 1)] = (0xFF << 24) | (r1 << 16) | (g1 << 8) | b1;
                }
            }
        }
    }

    cairo_surface_mark_dirty(g_graph.bg_surface);
}

static void load_monolith_settings() {
    const char* home = getenv("HOME");
    std::string path = std::string(home ? home : "") + "/.config/monolith/settings.json";
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;

        bool grain_changed = false;

        if (j.contains("grain")) {
            auto& gr = j["grain"];
            double n_intensity = gr.value("intensity", g_graph.grain_intensity);
            std::string n_algo = gr.value("algorithm", g_graph.grain_algorithm);
            std::string n_tint = gr.value("tint", g_graph.grain_tint);
            int n_scale = gr.value("scale", g_graph.grain_scale);

            if (n_intensity != g_graph.grain_intensity || n_algo != g_graph.grain_algorithm ||
                n_tint != g_graph.grain_tint || n_scale != g_graph.grain_scale || !g_graph.bg_surface) {
                g_graph.grain_intensity = n_intensity;
                g_graph.grain_algorithm = n_algo;
                g_graph.grain_tint = n_tint;
                g_graph.grain_scale = n_scale;
                grain_changed = true;
            }
        }

        if (j.contains("taskgraph")) {
            auto& tg = j["taskgraph"];
            g_graph.spring_k = tg.value("spring_k", g_graph.spring_k);
            g_graph.rest_length = tg.value("rest_length", g_graph.rest_length);
            g_graph.repulsion = tg.value("repulsion", g_graph.repulsion);
            g_graph.center_gravity = tg.value("center_gravity", g_graph.center_gravity);
            g_graph.damping = tg.value("damping", g_graph.damping);
            g_graph.drift_speed = tg.value("drift_speed", g_graph.drift_speed);
            g_graph.edge_width = tg.value("edge_width", g_graph.edge_width);
            g_graph.edge_alpha = tg.value("edge_alpha", g_graph.edge_alpha);
            g_graph.node_radius = tg.value("node_radius", g_graph.node_radius);
            g_graph.repulsion_radius = tg.value("repulsion_radius", g_graph.repulsion_radius);
            g_graph.fuzzel_repulsion = tg.value("fuzzel_repulsion", g_graph.fuzzel_repulsion);
            g_graph.auto_freeze = tg.value("auto_freeze", g_graph.auto_freeze);

            for (auto& n : g_graph.nodes) {
                n.radius = g_graph.node_radius;
            }
        }

        if (grain_changed || !g_graph.bg_surface) {
            generate_algorithmic_grain();
        }

        if (g_graph.da) {
            gtk_widget_queue_draw(g_graph.da);
        }
    } catch (...) {}
}

static void on_settings_file_changed(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event_type, gpointer) {
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event_type == G_FILE_MONITOR_EVENT_CHANGED) {
        load_monolith_settings();
    }
}

static void setup_settings_monitor() {
    const char* home = getenv("HOME");
    std::string path = std::string(home ? home : "") + "/.config/monolith/settings.json";
    GFile* file = g_file_new_for_path(path.c_str());
    GFileMonitor* monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (monitor) {
        g_signal_connect(monitor, "changed", G_CALLBACK(on_settings_file_changed), nullptr);
    }
    g_object_unref(file);
}

// ──────────────────────────────────────────────────────────────────────────────
// CONFIGURATION MANAGEMENT
// ──────────────────────────────────────────────────────────────────────────────
static void save_config() {
    std::string path = std::string(getenv("HOME") ? getenv("HOME") : "") + "/.config/monolith/taskgraph/config.json";
    try {
        json j;
        j["pinned"] = json::array();
        for (const auto& n : g_graph.nodes) {
            if (n.is_pinned) {
                json item;
                item["id"] = n.id;
                item["name"] = n.name;
                item["icon"] = n.icon_name;
                item["exec"] = n.exec_cmd.empty() ? n.name : n.exec_cmd;
                item["match_class"] = n.match_class.empty() ? n.name : n.match_class;
                item["load_to_ram"] = n.load_to_ram;
                j["pinned"].push_back(item);
            }
        }
        j["physics"] = {
            {"repulsion", g_graph.repulsion},
            {"spring_k", g_graph.spring_k},
            {"rest_length", g_graph.rest_length},
            {"damping", g_graph.damping},
            {"center_gravity", g_graph.center_gravity},
            {"repulsion_radius", g_graph.repulsion_radius},
            {"fuzzel_repulsion", g_graph.fuzzel_repulsion}
        };

        std::ofstream f(path);
        if (f.is_open()) {
            f << j.dump(2);
        }
    } catch (...) {}
}

static void load_config() {
    std::string path = std::string(getenv("HOME") ? getenv("HOME") : "") + "/.config/monolith/taskgraph/config.json";
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;

        g_graph.nodes.clear();
        g_graph.edges.clear();

        if (j.contains("physics") && j["physics"].is_object()) {
            auto& p = j["physics"];
            g_graph.repulsion = p.value("repulsion", g_graph.repulsion);
            g_graph.spring_k = p.value("spring_k", g_graph.spring_k);
            g_graph.rest_length = p.value("rest_length", g_graph.rest_length);
            g_graph.damping = p.value("damping", g_graph.damping);
            g_graph.center_gravity = p.value("center_gravity", g_graph.center_gravity);
            g_graph.repulsion_radius = p.value("repulsion_radius", g_graph.repulsion_radius);
            g_graph.fuzzel_repulsion = p.value("fuzzel_repulsion", g_graph.fuzzel_repulsion);
        }

        if (j.contains("pinned") && j["pinned"].is_array()) {
            size_t count = j["pinned"].size();

            std::unordered_map<std::string, std::pair<double, double>> initial_positions = {
                { "btop",        { 0.500, 0.180 } },
                { "signal",      { 0.615, 0.204 } },
                { "telegram",    { 0.712, 0.274 } },
                { "spotify",     { 0.777, 0.378 } },
                { "vesktop",     { 0.800, 0.500 } },
                { "caprine",     { 0.777, 0.622 } },
                { "qbittorrent", { 0.712, 0.726 } },
                { "localsend",   { 0.615, 0.796 } },
                { "dolphin",     { 0.500, 0.820 } },
                { "obs",         { 0.385, 0.796 } },
                { "vlc",         { 0.288, 0.726 } },
                { "gimp",        { 0.223, 0.622 } },
                { "obsidian",    { 0.200, 0.500 } },
                { "zen",         { 0.223, 0.378 } },
                { "firefox",     { 0.288, 0.274 } },
                { "zed",         { 0.385, 0.204 } }
            };

            for (size_t i = 0; i < count; ++i) {
                auto& item = j["pinned"][i];
                AppNode node;
                node.id = item.value("id", "");
                node.name = item.value("name", "");
                node.icon_name = item.value("icon", "");
                node.exec_cmd = item.value("exec", "");
                node.match_class = item.value("match_class", "");
                node.load_to_ram = item.value("load_to_ram", false);
                node.is_pinned = true;

                if (initial_positions.find(node.id) != initial_positions.end()) {
                    auto pos = initial_positions[node.id];
                    node.x = pos.first * g_graph.screen_w;
                    node.y = pos.second * g_graph.screen_h;
                } else {
                    double angle = (2.0 * M_PI * i) / count;
                    node.x = (g_graph.screen_w / 2.0) + 400.0 * std::cos(angle);
                    node.y = (g_graph.screen_h / 2.0) + 260.0 * std::sin(angle);
                }

                node.drift_phase = (rand() % 628) / 100.0;
                node.drift_speed = 0.20 + (rand() % 16) / 100.0;
                node.pixbuf = get_icon_pixbuf(node.icon_name, 20);

                g_graph.nodes.push_back(node);
            }

            auto add_edge = [](const std::string& a, const std::string& b, double len) {
                bool has_a = false, has_b = false;
                for (const auto& n : g_graph.nodes) {
                    if (n.id == a) has_a = true;
                    if (n.id == b) has_b = true;
                }
                if (has_a && has_b) {
                    g_graph.edges.push_back({a, b, len});
                }
            };

            if (j.contains("edges") && j["edges"].is_array() && !j["edges"].empty()) {
                for (auto& item : j["edges"]) {
                    std::string from = item.value("from", "");
                    std::string to = item.value("to", "");
                    double len = item.value("rest_length", g_graph.rest_length);
                    if (!from.empty() && !to.empty()) {
                        add_edge(from, to, len);
                    }
                }
            } else {
                // 16-node perimeter ring around center
                add_edge("btop", "signal", 140.0);
                add_edge("signal", "telegram", 140.0);
                add_edge("telegram", "spotify", 140.0);
                add_edge("spotify", "vesktop", 140.0);
                add_edge("vesktop", "caprine", 140.0);
                add_edge("caprine", "qbittorrent", 140.0);
                add_edge("qbittorrent", "localsend", 140.0);
                add_edge("localsend", "dolphin", 140.0);
                add_edge("dolphin", "obs", 140.0);
                add_edge("obs", "vlc", 140.0);
                add_edge("vlc", "gimp", 140.0);
                add_edge("gimp", "obsidian", 140.0);
                add_edge("obsidian", "zen", 140.0);
                add_edge("zen", "firefox", 140.0);
                add_edge("firefox", "zed", 140.0);
                add_edge("zed", "btop", 140.0);

                // 8 perimeter non-crossing chords (constellation triangulation)
                add_edge("btop", "telegram", 260.0);
                add_edge("telegram", "vesktop", 260.0);
                add_edge("vesktop", "qbittorrent", 260.0);
                add_edge("qbittorrent", "dolphin", 260.0);
                add_edge("dolphin", "vlc", 260.0);
                add_edge("vlc", "obsidian", 260.0);
                add_edge("obsidian", "firefox", 260.0);
                add_edge("firefox", "btop", 260.0);
            }

            g_print("[TASKGRAPH] Loaded %zu nodes, %zu edges\n", g_graph.nodes.size(), g_graph.edges.size());
            for (const auto& n : g_graph.nodes) {
                g_print("[TASKGRAPH] Node: %s (id: %s) at (%.1f, %.1f)\n", n.name.c_str(), n.id.c_str(), n.x, n.y);
            }
        }
    } catch (...) {}
}

// ──────────────────────────────────────────────────────────────────────────────
// HYPRLAND SYNCHRONIZATION & COVERAGE STATE
// ──────────────────────────────────────────────────────────────────────────────
static void check_desktop_state() {
    int active_ws_id = 1;
    bool has_fullscreen = false;

    // 1. Get active workspace information from Hyprland
    FILE* fp_ws = popen("hyprctl activeworkspace -j 2>/dev/null", "r");
    if (fp_ws) {
        std::string out;
        char buf[256];
        while (fgets(buf, sizeof(buf), fp_ws) != nullptr) out += buf;
        pclose(fp_ws);

        if (!out.empty()) {
            try {
                json j = json::parse(out);
                active_ws_id = j.value("id", 1);
                has_fullscreen = j.value("hasfullscreen", false);
            } catch (...) {}
        }
    }

    // 2. Synchronize running client addresses and calculate real coverage
    FILE* fp = popen("hyprctl clients -j 2>/dev/null", "r");
    if (!fp) return;

    std::string output;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        output += buffer;
    }
    pclose(fp);

    if (output.empty()) return;

    try {
        json clients = json::parse(output);

        std::unordered_set<std::string> active_addresses;
        std::unordered_map<std::string, json> unpinned_candidates;

        int total_on_active_ws = 0;
        int floating_on_active_ws = 0;

        for (auto& n : g_graph.nodes) {
            if (n.is_pinned) {
                n.is_running = false;
                n.hypr_address = "";
            }
        }

        for (const auto& c : clients) {
            std::string cls = c.value("class", "");
            std::string title = c.value("title", "");
            std::string addr = c.value("address", "");
            int ws = c.value("workspace", json::object()).value("id", 1);
            bool is_floating = c.value("floating", false);

            if (cls.empty() || cls == "monolith-taskgraph" || cls == "monolith-toolbox") continue;

            if (ws == active_ws_id) {
                total_on_active_ws++;
                if (is_floating) {
                    floating_on_active_ws++;
                }
            }

            active_addresses.insert(addr);

            bool matched_pinned = false;
            for (auto& n : g_graph.nodes) {
                if (n.is_pinned && !n.match_class.empty()) {
                    if (strcasecmp(cls.c_str(), n.match_class.c_str()) == 0 ||
                        cls.find(n.match_class) != std::string::npos ||
                        n.match_class.find(cls) != std::string::npos) {
                        n.is_running = true;
                        n.hypr_address = addr;
                        n.workspace_id = ws;
                        matched_pinned = true;
                        break;
                    }
                }
            }

            if (!matched_pinned) {
                unpinned_candidates[addr] = c;
            }
        }

        // INTELLIGENT DESKTOP VISIBILITY DETERMINATION
        // If any window is floating, user can see the desktop around it -> GRAPH MUST KEEP RUNNING!
        // Only freeze if all windows on active workspace are tiled with 0 gaps or fullscreen.
        bool newly_covered = false;
        if (g_graph.auto_freeze) {
            if (has_fullscreen) {
                newly_covered = true;
            } else if (total_on_active_ws > 0 && floating_on_active_ws == 0) {
                newly_covered = true;
            } else {
                newly_covered = false;
            }
        }

        if (newly_covered != g_graph.is_covered) {
            g_graph.is_covered = newly_covered;
            if (!g_graph.is_covered) {
                wake_physics(0.7);
            }
        }

        bool topology_changed = false;

        // Remove closed dynamic windows
        size_t prev_count = g_graph.nodes.size();
        g_graph.nodes.erase(
            std::remove_if(g_graph.nodes.begin(), g_graph.nodes.end(), [&](const AppNode& n) {
                if (n.is_pinned) return false;
                return active_addresses.find(n.hypr_address) == active_addresses.end();
            }),
            g_graph.nodes.end()
        );
        if (g_graph.nodes.size() != prev_count) topology_changed = true;

        for (const auto& n : g_graph.nodes) {
            if (!n.is_pinned) {
                unpinned_candidates.erase(n.hypr_address);
            }
        }

        // Add brand new dynamic windows
        for (const auto& [addr, c] : unpinned_candidates) {
            std::string cls = c.value("class", "");
            std::string title = c.value("title", "");
            int ws = c.value("workspace", json::object()).value("id", 1);

            AppNode dn;
            dn.id = "win_" + addr;
            dn.name = cls.empty() ? title : cls;
            if (dn.name.length() > 14) dn.name = dn.name.substr(0, 12) + "..";

            std::string icon_lookup = cls;
            std::transform(icon_lookup.begin(), icon_lookup.end(), icon_lookup.begin(), ::tolower);
            dn.icon_name = icon_lookup;
            dn.exec_cmd = cls;
            dn.match_class = cls;
            dn.hypr_address = addr;
            dn.workspace_id = ws;
            dn.is_pinned = false;
            dn.is_running = true;
            dn.drift_phase = (rand() % 628) / 100.0;
            dn.drift_speed = 0.22;

            if (!g_graph.nodes.empty()) {
                const auto& ref = g_graph.nodes[rand() % g_graph.nodes.size()];
                double ang = (rand() % 360) * M_PI / 180.0;
                dn.x = std::clamp(ref.x + 220.0 * std::cos(ang), 120.0, g_graph.screen_w - 120.0);
                dn.y = std::clamp(ref.y + 220.0 * std::sin(ang), 120.0, g_graph.screen_h - 120.0);
            } else {
                dn.x = g_graph.screen_w * 0.5;
                dn.y = g_graph.screen_h * 0.5;
            }
            dn.pixbuf = get_icon_pixbuf(dn.icon_name, 20);

            g_graph.nodes.push_back(dn);

            // Connect to at most ONE nearest pinned node
            int closest_idx = -1;
            double min_dist = 1e9;
            for (size_t i = 0; i < g_graph.nodes.size() - 1; ++i) {
                if (!g_graph.nodes[i].is_pinned) continue;
                double d = std::hypot(dn.x - g_graph.nodes[i].x, dn.y - g_graph.nodes[i].y);
                if (d < min_dist) {
                    min_dist = d;
                    closest_idx = static_cast<int>(i);
                }
            }
            if (closest_idx < 0) {
                for (size_t i = 0; i < g_graph.nodes.size() - 1; ++i) {
                    double d = std::hypot(dn.x - g_graph.nodes[i].x, dn.y - g_graph.nodes[i].y);
                    if (d < min_dist) {
                        min_dist = d;
                        closest_idx = static_cast<int>(i);
                    }
                }
            }
            if (closest_idx >= 0) {
                g_graph.edges.push_back({dn.id, g_graph.nodes[closest_idx].id, 260.0});
            }
            topology_changed = true;
        }

        // Clean dangling edges and duplicate edges
        std::vector<AppEdge> clean_edges;
        for (const auto& e : g_graph.edges) {
            bool has_from = false, has_to = false;
            for (const auto& n : g_graph.nodes) {
                if (n.id == e.from_id) has_from = true;
                if (n.id == e.to_id) has_to = true;
            }
            if (!has_from || !has_to || e.from_id == e.to_id) continue;

            bool dup = false;
            for (const auto& u : clean_edges) {
                if ((u.from_id == e.from_id && u.to_id == e.to_id) ||
                    (u.from_id == e.to_id && u.to_id == e.from_id)) {
                    dup = true;
                    break;
                }
            }
            if (!dup) clean_edges.push_back(e);
        }
        g_graph.edges = std::move(clean_edges);

        if (topology_changed) {
            wake_physics(0.6);
        } else if (!g_graph.is_covered && g_graph.da) {
            gtk_widget_queue_draw(g_graph.da);
        }

    } catch (...) {}
}

// ──────────────────────────────────────────────────────────────────────────────
// REAL-TIME EVENT-DRIVEN HYPRLAND SOCKET LISTENER (0% CPU WAITING)
// ──────────────────────────────────────────────────────────────────────────────
static std::string find_hyprland_socket2() {
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) runtime_dir = "/run/user/1000";

    std::string pattern = std::string(runtime_dir) + "/hypr/*/.socket2.sock";
    glob_t g;
    std::string result = "";
    if (glob(pattern.c_str(), 0, nullptr, &g) == 0) {
        if (g.gl_pathc > 0) {
            result = g.gl_pathv[0];
        }
    }
    globfree(&g);
    return result;
}

static void hypr_socket_listener_thread() {
    while (true) {
        std::string sock_path = find_hyprland_socket2();
        if (sock_path.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        char buf[1024];
        while (true) {
            ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            // Any event in hyprland (workspace switch, open/close, focus)
            g_idle_add([](gpointer) -> gboolean {
                check_desktop_state();
                return G_SOURCE_REMOVE;
            }, nullptr);
        }

        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

static void check_fuzzel_state() {
    bool fuzzel_now = (access("/tmp/monolith_fuzzel_active", F_OK) == 0);
    if (fuzzel_now != g_graph.fuzzel_active) {
        g_graph.fuzzel_active = fuzzel_now;
        wake_physics(1.0);
        if (g_graph.fuzzel_active) {
            double fcx = g_graph.screen_w / 2.0;
            double fcy = g_graph.screen_h / 2.0;
            double rx = g_graph.repulsion_radius;
            double ry = g_graph.repulsion_radius * 0.72;
            for (auto& node : g_graph.nodes) {
                double fdx = node.x - fcx;
                double fdy = node.y - fcy;
                double norm_dist = std::hypot(fdx / rx, fdy / ry) + 0.001;
                if (norm_dist < 1.0) {
                    double impulse = 5.0 * (1.0 - norm_dist);
                    node.vx += (fdx / (norm_dist * rx)) * impulse;
                    node.vy += (fdy / (norm_dist * ry)) * impulse;
                }
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// PHYSICS ENGINE (With Fuzzel Repulsion Field & Freeze Logic)
// ──────────────────────────────────────────────────────────────────────────────
static void update_physics() {
    // 0. Check if Fuzzel is open via indicator file
    check_fuzzel_state();

    // If desktop is covered and Fuzzel is NOT open, completely freeze simulation
    if (g_graph.is_covered && !g_graph.fuzzel_active) {
        return;
    }

    // Keep physics alive for continuous gentle floating drift unless screen is covered
    if (g_graph.alpha < 0.20) {
        g_graph.alpha = 0.20;
    }
    g_graph.physics_active = true;

    size_t n = g_graph.nodes.size();
    if (n == 0) return;

    g_graph.sim_time += 0.012;

    double fcx = g_graph.screen_w / 2.0;
    double fcy = g_graph.screen_h / 2.0;

    for (size_t i = 0; i < n; ++i) {
        auto& node = g_graph.nodes[i];
        if (node.is_dragged) continue;

        // Continuous subtle organic floating vectors (each node drifts on its own harmonic path)
        double t = g_graph.sim_time;
        double fx = 0.26 * std::sin(node.drift_phase + t * node.drift_speed)
                  + 0.14 * std::cos(node.drift_phase * 1.73 + t * (node.drift_speed * 0.7));
        double fy = 0.26 * std::cos(node.drift_phase * 1.41 + t * (node.drift_speed * 0.85))
                  + 0.14 * std::sin(node.drift_phase * 2.19 + t * (node.drift_speed * 0.6));

        // 1. FUZZEL APP MENU REPULSION FIELD (Powerful clearance)
        // Sweeps all nodes outward into a clear perimeter when Super is pressed
        if (g_graph.fuzzel_active) {
            double fdx = node.x - fcx;
            double fdy = node.y - fcy;
            double rx = g_graph.repulsion_radius;
            double ry = g_graph.repulsion_radius * 0.72; // Elliptical matching Fuzzel horizontal geometry
            double norm_dist = std::hypot(fdx / rx, fdy / ry) + 0.001;
            if (norm_dist < 1.0) {
                double push = g_graph.fuzzel_repulsion * (1.0 - norm_dist);
                fx += (fdx / (norm_dist * rx)) * push;
                fy += (fdy / (norm_dist * ry)) * push;
            }
        }

        // 2. Obsidian-style gentle central gravity (keeps constellation floating in center of screen)
        // When Fuzzel is open, dampen central gravity inside the menu field so nodes do not fight inward
        double grav_mult = g_graph.fuzzel_active ? 0.45 : 1.0;
        fx += (fcx - node.x) * (g_graph.center_gravity * grav_mult);
        fy += (fcy - node.y) * (g_graph.center_gravity * grav_mult);

        // 3. Smooth mutual repulsion (inverse square with softening)
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            double dx = node.x - g_graph.nodes[j].x;
            double dy = node.y - g_graph.nodes[j].y;
            double d = std::hypot(dx, dy) + 0.1;
            if (d < 700.0) {
                double rep = g_graph.repulsion / (d * d);
                fx += (dx / d) * rep;
                fy += (dy / d) * rep;
            }
        }

        // 4. Elastic springs
        for (const auto& e : g_graph.edges) {
            std::string other_id = "";
            if (e.from_id == node.id) other_id = e.to_id;
            else if (e.to_id == node.id) other_id = e.from_id;

            if (!other_id.empty()) {
                for (const auto& other : g_graph.nodes) {
                    if (other.id == other_id) {
                        double dx = other.x - node.x;
                        double dy = other.y - node.y;
                        double d = std::hypot(dx, dy) + 0.1;
                        double rlen = (e.rest_length > 0) ? e.rest_length : g_graph.rest_length;
                        double diff = d - rlen;
                        double f = diff * g_graph.spring_k;
                        fx += (dx / d) * f;
                        fy += (dy / d) * f;
                        break;
                    }
                }
            }
        }

        // 5. Screen boundary safety zones (prevents nodes from sticking to borders)
        double margin_x = 110.0;
        double margin_y_top = 90.0;
        double margin_y_bot = 90.0;

        if (node.x < margin_x) {
            double pen = margin_x - node.x;
            fx += pen * 0.12;
            if (node.vx < 0) node.vx *= 0.6;
        } else if (node.x > g_graph.screen_w - margin_x) {
            double pen = node.x - (g_graph.screen_w - margin_x);
            fx -= pen * 0.12;
            if (node.vx > 0) node.vx *= 0.6;
        }

        if (node.y < margin_y_top) {
            double pen = margin_y_top - node.y;
            fy += pen * 0.12;
            if (node.vy < 0) node.vy *= 0.6;
        } else if (node.y > g_graph.screen_h - margin_y_bot) {
            double pen = node.y - (g_graph.screen_h - margin_y_bot);
            fy -= pen * 0.12;
            if (node.vy > 0) node.vy *= 0.6;
        }

        // 6. Velocity integration & damping
        node.vx = (node.vx + fx * g_graph.alpha) * g_graph.damping;
        node.vy = (node.vy + fy * g_graph.alpha) * g_graph.damping;

        double max_speed = g_graph.fuzzel_active ? 9.5 : 1.2;
        node.vx = std::clamp(node.vx, -max_speed, max_speed);
        node.vy = std::clamp(node.vy, -max_speed, max_speed);

        node.x += node.vx;
        node.y += node.vy;

        double pad_x = node.radius + 30.0;
        double pad_y_top = node.radius + 45.0; // below 30px bar
        double pad_y_bot = node.radius + 30.0;

        if (node.x < pad_x) {
            node.x = pad_x;
            if (node.vx < 0) node.vx = -node.vx * 0.2;
        } else if (node.x > g_graph.screen_w - pad_x) {
            node.x = g_graph.screen_w - pad_x;
            if (node.vx > 0) node.vx = -node.vx * 0.2;
        }

        if (node.y < pad_y_top) {
            node.y = pad_y_top;
            if (node.vy < 0) node.vy = -node.vy * 0.2;
        } else if (node.y > g_graph.screen_h - pad_y_bot) {
            node.y = g_graph.screen_h - pad_y_bot;
            if (node.vy > 0) node.vy = -node.vy * 0.2;
        }
    }

    if (!g_graph.fuzzel_active) {
        g_graph.alpha = std::max(0.20, g_graph.alpha * 0.96);
    }

    for (auto& node : g_graph.nodes) {
        double target_scale = node.is_hovered ? 1.12 : 1.0;
        node.cur_scale += (target_scale - node.cur_scale) * 0.2;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// RENDERING
// ──────────────────────────────────────────────────────────────────────────────
static void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

static void draw_graph(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer) {
    g_graph.screen_w = width;
    g_graph.screen_h = height;

    // ──────────────── 0. PROCEDURAL GRAIN BACKGROUND ────────────────────────
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    if (g_graph.bg_surface) {
        int img_w = cairo_image_surface_get_width(g_graph.bg_surface);
        int img_h = cairo_image_surface_get_height(g_graph.bg_surface);
        if (img_w == width && img_h == height) {
            cairo_set_source_surface(cr, g_graph.bg_surface, 0, 0);
            cairo_paint(cr);
        } else {
            cairo_save(cr);
            cairo_scale(cr, static_cast<double>(width) / img_w, static_cast<double>(height) / img_h);
            cairo_set_source_surface(cr, g_graph.bg_surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    } else {
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_paint(cr);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    std::unordered_map<std::string, const AppNode*> id_map;
    for (const auto& n : g_graph.nodes) id_map[n.id] = &n;

    // ──────────────── 1. NODE BASES & ICONS (Drawn First) ───────────────────────
    for (const auto& node : g_graph.nodes) {
        double r = node.radius * node.cur_scale;

        // Subtle circular boundary rim
        cairo_arc(cr, node.x, node.y, r, 0, 2 * M_PI);
        if (node.is_hovered || node.is_dragged) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 1.6);
        } else {
            cairo_set_source_rgba(cr, 0.80, 0.80, 0.85, 0.60);
            cairo_set_line_width(cr, 1.0);
        }
        cairo_stroke(cr);

        if (node.is_running) {
            cairo_arc(cr, node.x, node.y, r + 4.0, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        if (node.load_to_ram) {
            double bx = node.x + r * 0.70;
            double by = node.y - r * 0.70;
            cairo_arc(cr, bx, by, 4.5, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.95, 0.95, 0.98, 0.85);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            cairo_arc(cr, bx, by, 2.0, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_fill(cr);
        }

        if (node.pixbuf) {
            int icon_w = gdk_pixbuf_get_width(node.pixbuf);
            int icon_h = gdk_pixbuf_get_height(node.pixbuf);

            cairo_save(cr);
            cairo_translate(cr, node.x, node.y);
            if (node.cur_scale != 1.0) {
                cairo_scale(cr, node.cur_scale, node.cur_scale);
            }
            gdk_cairo_set_source_pixbuf(cr, node.pixbuf, -icon_w / 2.0, -icon_h / 2.0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    }

    // ──────────────── 2. DELICATE HAILINE EDGES (Drawn OVER Icons to Exact Center) 
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (const auto& e : g_graph.edges) {
        auto it1 = id_map.find(e.from_id);
        auto it2 = id_map.find(e.to_id);
        if (it1 == id_map.end() || it2 == id_map.end()) continue;

        const AppNode* n1 = it1->second;
        const AppNode* n2 = it2->second;

        double dx = n2->x - n1->x;
        double dy = n2->y - n1->y;
        double dist = std::hypot(dx, dy);

        if (dist <= 6.0) continue;

        bool is_highlighted = n1->is_hovered || n2->is_hovered || n1->is_dragged || n2->is_dragged;

        if (is_highlighted) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.90);
            cairo_set_line_width(cr, g_graph.edge_width + 0.4);
        } else {
            // Delicate, subtle hairline edge (Obsidian style)
            cairo_set_source_rgba(cr, 0.75, 0.75, 0.80, g_graph.edge_alpha);
            cairo_set_line_width(cr, g_graph.edge_width);
        }

        // Draw line directly into geometric centers: visibly leads across the icon to (node.x, node.y)
        cairo_move_to(cr, n1->x, n1->y);
        cairo_line_to(cr, n2->x, n2->y);
        cairo_stroke(cr);
    }

    // ──────────────── 3. CENTRAL ANCHOR DOTS & LABELS ───────────────────────────
    cairo_select_font_face(cr, "JetBrains Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9.5);

    for (const auto& node : g_graph.nodes) {
        double r = node.radius * node.cur_scale;

        // Visible Central Anchor Dot (Where all connection lines visibly meet in the exact center)
        cairo_arc(cr, node.x, node.y, 2.2, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_fill(cr);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, node.name.c_str(), &ext);
        double tx = node.x - ext.width / 2.0;
        double ty = node.y + r + 13.0;

        // Solid OLED black protective pill behind label: prevents passing lines from cutting through letters
        draw_rounded_rect(cr, tx - 5.0, ty - ext.height - 2.0, ext.width + 10.0, ext.height + 5.0, 3.0);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.90, 0.90, 0.92, 0.95);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, node.name.c_str());
    }

    // ──────────────── 3. OLED RIGHT-CLICK CONTEXT MENU ─────────────────────
    if (g_graph.menu_open && g_graph.menu_node_idx >= 0 && g_graph.menu_node_idx < static_cast<int>(g_graph.nodes.size())) {
        const auto& node = g_graph.nodes[g_graph.menu_node_idx];

        double mx = std::clamp(g_graph.menu_x, 15.0, g_graph.screen_w - g_graph.menu_w - 15.0);
        double my = std::clamp(g_graph.menu_y, 15.0, g_graph.screen_h - g_graph.menu_h - 15.0);

        draw_rounded_rect(cr, mx, my, g_graph.menu_w, g_graph.menu_h, 8.0);
        cairo_set_source_rgba(cr, 0.05, 0.05, 0.06, 0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.35, 0.35, 0.38, 0.90);
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);

        cairo_select_font_face(cr, "JetBrains Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.5);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, mx + 14.0, my + 24.0);
        std::string header = node.name + (node.is_running ? "  [ACTIVE]" : "");
        cairo_show_text(cr, header.c_str());

        cairo_set_source_rgba(cr, 0.25, 0.25, 0.28, 0.70);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, mx + 10.0, my + 34.0);
        cairo_line_to(cr, mx + g_graph.menu_w - 10.0, my + 34.0);
        cairo_stroke(cr);

        double item_h = 30.0;
        double start_y = my + 38.0;

        cairo_set_font_size(cr, 10.5);

        // Item 0: Focus / Launch
        if (g_graph.menu_hovered_item == 0) {
            draw_rounded_rect(cr, mx + 6.0, start_y, g_graph.menu_w - 12.0, item_h, 5.0);
            cairo_set_source_rgba(cr, 0.20, 0.20, 0.24, 0.95);
            cairo_fill(cr);
        }
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, mx + 16.0, start_y + 20.0);
        std::string action_str = node.is_running ? "▶  Switch to Window (Focus)" : "▶  Launch Application";
        cairo_show_text(cr, action_str.c_str());

        // Item 1: Pin / Unpin
        double y1 = start_y + item_h + 2.0;
        if (g_graph.menu_hovered_item == 1) {
            draw_rounded_rect(cr, mx + 6.0, y1, g_graph.menu_w - 12.0, item_h, 5.0);
            cairo_set_source_rgba(cr, 0.20, 0.20, 0.24, 0.95);
            cairo_fill(cr);
        }
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, mx + 16.0, y1 + 20.0);
        std::string pin_str = node.is_pinned ? "Unpin from Task Graph" : "Pin to Task Graph";
        cairo_show_text(cr, pin_str.c_str());

        // Item 2: Load to RAM
        double y2 = y1 + item_h + 2.0;
        if (g_graph.menu_hovered_item == 2) {
            draw_rounded_rect(cr, mx + 6.0, y2, g_graph.menu_w - 12.0, item_h, 5.0);
            cairo_set_source_rgba(cr, 0.20, 0.20, 0.24, 0.95);
            cairo_fill(cr);
        }
        cairo_move_to(cr, mx + 16.0, y2 + 20.0);
        if (node.load_to_ram) {
            cairo_set_source_rgba(cr, 0.95, 0.95, 0.98, 1.0);
            cairo_show_text(cr, "Load to RAM: [ ENABLED ]");
        } else {
            cairo_set_source_rgba(cr, 0.65, 0.65, 0.68, 0.85);
            cairo_show_text(cr, "Load to RAM: [ DISABLED ]");
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// MOUSE INTERACTION & GESTURES
// ──────────────────────────────────────────────────────────────────────────────
static int hit_test_node(double x, double y) {
    for (int i = static_cast<int>(g_graph.nodes.size()) - 1; i >= 0; --i) {
        const auto& n = g_graph.nodes[i];
        double dx = x - n.x;
        double dy = y - n.y;
        double hit_r = n.radius * n.cur_scale + 6.0;
        if (dx * dx + dy * dy <= hit_r * hit_r) {
            return i;
        }
    }
    return -1;
}

static void on_drag_begin(GtkGestureDrag*, double start_x, double start_y, gpointer) {
    if (g_graph.menu_open && g_graph.menu_node_idx >= 0 && g_graph.menu_node_idx < static_cast<int>(g_graph.nodes.size())) {
        double mx = std::clamp(g_graph.menu_x, 15.0, g_graph.screen_w - g_graph.menu_w - 15.0);
        double my = std::clamp(g_graph.menu_y, 15.0, g_graph.screen_h - g_graph.menu_h - 15.0);

        if (start_x >= mx && start_x <= mx + g_graph.menu_w && start_y >= my && start_y <= my + g_graph.menu_h) {
            double rel_y = start_y - (my + 38.0);
            int clicked_item = static_cast<int>(rel_y / 32.0);

            auto& target_node = g_graph.nodes[g_graph.menu_node_idx];

            if (clicked_item == 0) {
                if (target_node.is_running && !target_node.hypr_address.empty()) {
                    std::string cmd = "hyprctl dispatch focuswindow address:" + target_node.hypr_address + " > /dev/null 2>&1 &";
                    system(cmd.c_str());
                } else if (!target_node.exec_cmd.empty()) {
                    std::string cmd = target_node.exec_cmd + " > /dev/null 2>&1 &";
                    system(cmd.c_str());
                }
            } else if (clicked_item == 1) {
                target_node.is_pinned = !target_node.is_pinned;
                save_config();
            } else if (clicked_item == 2) {
                target_node.load_to_ram = !target_node.load_to_ram;
                save_config();
                std::string cmd = "/usr/local/bin/monolith-ram-preloader toggle " + target_node.id + " > /dev/null 2>&1 &";
                system(cmd.c_str());
            }

            g_graph.menu_open = false;
            g_graph.menu_node_idx = -1;
            if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
            return;
        } else {
            g_graph.menu_open = false;
            g_graph.menu_node_idx = -1;
            if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
        }
    }

    int idx = hit_test_node(start_x, start_y);
    g_graph.dragged_node_idx = idx;
    g_graph.drag_start_x = start_x;
    g_graph.drag_start_y = start_y;
    g_graph.drag_dist = 0.0;

    if (idx >= 0) {
        g_graph.nodes[idx].is_dragged = true;
        g_graph.nodes[idx].vx = 0.0;
        g_graph.nodes[idx].vy = 0.0;
        wake_physics(0.6);
    }
}

static void on_drag_update(GtkGestureDrag*, double offset_x, double offset_y, gpointer) {
    if (g_graph.dragged_node_idx >= 0 && g_graph.dragged_node_idx < static_cast<int>(g_graph.nodes.size())) {
        auto& node = g_graph.nodes[g_graph.dragged_node_idx];
        node.x = g_graph.drag_start_x + offset_x;
        node.y = g_graph.drag_start_y + offset_y;
        g_graph.drag_dist = std::hypot(offset_x, offset_y);
        wake_physics(0.4);
    }
}

static void on_drag_end(GtkGestureDrag*, double, double, gpointer) {
    if (g_graph.dragged_node_idx >= 0 && g_graph.dragged_node_idx < static_cast<int>(g_graph.nodes.size())) {
        auto& node = g_graph.nodes[g_graph.dragged_node_idx];
        node.is_dragged = false;

        if (g_graph.drag_dist < 6.0) {
            if (node.is_running && !node.hypr_address.empty()) {
                std::string cmd = "hyprctl dispatch focuswindow address:" + node.hypr_address + " > /dev/null 2>&1 &";
                system(cmd.c_str());
            } else if (!node.exec_cmd.empty()) {
                std::string cmd = node.exec_cmd + " > /dev/null 2>&1 &";
                system(cmd.c_str());
            }
        }
    }

    g_graph.dragged_node_idx = -1;
    wake_physics(0.5);
}

static void on_right_click(GtkGestureClick*, int, double x, double y, gpointer) {
    int idx = hit_test_node(x, y);
    if (idx >= 0) {
        g_graph.menu_open = true;
        g_graph.menu_node_idx = idx;
        g_graph.menu_x = x + 12.0;
        g_graph.menu_y = y + 12.0;
        g_graph.menu_hovered_item = -1;
        if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
    } else {
        if (g_graph.menu_open) {
            g_graph.menu_open = false;
            g_graph.menu_node_idx = -1;
            if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
        }
    }
}

static void on_motion(GtkEventControllerMotion*, double x, double y, gpointer) {
    if (g_graph.menu_open) {
        double mx = std::clamp(g_graph.menu_x, 15.0, g_graph.screen_w - g_graph.menu_w - 15.0);
        double my = std::clamp(g_graph.menu_y, 15.0, g_graph.screen_h - g_graph.menu_h - 15.0);

        if (x >= mx && x <= mx + g_graph.menu_w && y >= my + 38.0 && y <= my + g_graph.menu_h) {
            int item = static_cast<int>((y - (my + 38.0)) / 32.0);
            if (item != g_graph.menu_hovered_item) {
                g_graph.menu_hovered_item = item;
                if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
            }
            return;
        } else {
            if (g_graph.menu_hovered_item != -1) {
                g_graph.menu_hovered_item = -1;
                if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
            }
        }
    }

    int hovered_idx = hit_test_node(x, y);

    for (size_t i = 0; i < g_graph.nodes.size(); ++i) {
        g_graph.nodes[i].is_hovered = (static_cast<int>(i) == hovered_idx);
    }

    if (hovered_idx >= 0 || g_graph.menu_open) {
        gdk_surface_set_cursor(gtk_native_get_surface(gtk_widget_get_native(g_graph.da)), gdk_cursor_new_from_name("pointer", nullptr));
    } else {
        gdk_surface_set_cursor(gtk_native_get_surface(gtk_widget_get_native(g_graph.da)), gdk_cursor_new_from_name("default", nullptr));
    }
}

static gboolean on_physics_tick(gpointer) {
    // Check Fuzzel state every tick
    check_fuzzel_state();

    // IF COVERED BY WINDOWS (and Fuzzel not active), ZERO PROCESS WORK!
    if (g_graph.is_covered && !g_graph.fuzzel_active) {
        return G_SOURCE_CONTINUE;
    }

    if (g_graph.physics_active || g_graph.fuzzel_active) {
        update_physics();
        if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_hypr_sync_tick(gpointer) {
    check_desktop_state();
    return G_SOURCE_CONTINUE;
}

// ──────────────────────────────────────────────────────────────────────────────
// APPLICATION ACTIVATION & LAYER SHELL
// ──────────────────────────────────────────────────────────────────────────────
static void on_activate(GApplication* app, gpointer) {
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GListModel* monitors = gdk_display_get_monitors(display);
        if (monitors && g_list_model_get_n_items(monitors) > 0) {
            GdkMonitor* mon = GDK_MONITOR(g_list_model_get_item(monitors, 0));
            if (mon) {
                GdkRectangle geom;
                gdk_monitor_get_geometry(mon, &geom);
                if (geom.width > 0 && geom.height > 0) {
                    g_graph.screen_w = geom.width;
                    g_graph.screen_h = geom.height;
                }
                g_object_unref(mon);
            }
        }
    }

    load_monolith_settings();
    setup_settings_monitor();
    load_config();

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, "window, .taskgraph-window { background: #000000; background-color: #000000; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    g_graph.win = gtk_window_new();
    gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(g_graph.win));

    gtk_layer_init_for_window(GTK_WINDOW(g_graph.win));
    gtk_layer_set_layer(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_LAYER_BOTTOM);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(g_graph.win), 0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_namespace(GTK_WINDOW(g_graph.win), "monolith-taskgraph");

    gtk_layer_set_anchor(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

    gtk_widget_add_css_class(g_graph.win, "taskgraph-window");

    g_graph.da = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_graph.da), draw_graph, nullptr, nullptr);
    gtk_window_set_child(GTK_WINDOW(g_graph.win), g_graph.da);

    GtkGesture* drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), nullptr);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), nullptr);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), nullptr);
    gtk_widget_add_controller(g_graph.da, GTK_EVENT_CONTROLLER(drag));

    GtkGesture* right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_right_click), nullptr);
    gtk_widget_add_controller(g_graph.da, GTK_EVENT_CONTROLLER(right_click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), nullptr);
    gtk_widget_add_controller(g_graph.da, motion);

    gtk_widget_set_visible(g_graph.win, TRUE);

    // Initial check
    check_desktop_state();

    // Spawn event-driven background thread listening to Hyprland socket2
    std::thread(hypr_socket_listener_thread).detach();

    // 16ms physics ticker (~60 FPS when active, 0% CPU when frozen/covered)
    g_graph.physics_timer_id = g_timeout_add(16, on_physics_tick, nullptr);

    // 2s fallback sync timer
    g_graph.hypr_sync_timer_id = g_timeout_add(2000, on_hypr_sync_tick, nullptr);
}

int main(int argc, char* argv[]) {
    GtkApplication* app = gtk_application_new("org.monolith.taskgraph", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
