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
#include <atomic>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pwd.h>
#include <thread>
#include <chrono>

#include "grain_generator.hpp"
#include "hypr_ipc.hpp"

using json = nlohmann::json;
using namespace Monolith;

static inline std::string get_user_home() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') return std::string(home);
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) return std::string(pw->pw_dir);
    const char* user = std::getenv("USER");
    if (user && user[0] != '\0') return "/home/" + std::string(user);
    return "/tmp";
}

// ══════════════════════════════════════════════════════════════════════════════
// MONOLITH TASK GRAPH 2.0 ("Constellation & Procedural Background Engine")
//   - Direct Zero-Fork UNIX Socket IPC with Hyprland (.socket.sock & .socket2.sock)
//   - True 0.00% CPU Sleep: Timer completely stops when desktop is covered
//   - Unified Procedural Gradient + Mathematical Film Grain Base Layer
//   - Clean Obsidian Node Discs (lines clip at rim, never deface app icons)
//   - Workspace Badges (W1..W9, S, E), RAM Cache Beacons, & 5-Action Context Menu
// ══════════════════════════════════════════════════════════════════════════════

struct AppNode {
    std::string id;
    std::string name;
    std::string icon_name;
    std::string exec_cmd;
    std::string match_class;
    std::string hypr_address;
    std::string workspace_label;
    int workspace_id = 0;
    bool is_pinned = false;
    bool is_running = false;
    bool is_hovered = false;
    bool is_dragged = false;
    bool load_to_ram = false;

    double x = 0.0, y = 0.0;
    double vx = 0.0, vy = 0.0;
    double radius = 18.0;
    double cur_scale = 1.0;

    // Harmonic ambient Lissajous drift
    double drift_phase = 0.0;
    double drift_speed = 0.22;

    GdkPixbuf* pixbuf = nullptr;
};

struct AppEdge {
    std::string from_id;
    std::string to_id;
    double rest_length = 260.0;
};

struct GraphState {
    GtkWidget* win = nullptr;
    GtkWidget* da = nullptr;
    GtkIconTheme* icon_theme = nullptr;

    std::vector<AppNode> nodes;
    std::vector<AppEdge> edges;
    std::unordered_map<std::string, GdkPixbuf*> icon_cache;

    cairo_surface_t* bg_surface = nullptr;

    int screen_w = 1920;
    int screen_h = 1200;

    // Dynamic physics and visual settings loaded from settings.json
    double spring_k = 0.0042;
    double rest_length = 260.0;
    double repulsion = 24000.0;
    double center_gravity = 0.010;
    double damping = 0.90;
    double drift_speed = 0.20;
    double edge_width = 1.1;
    double edge_alpha = 0.45;
    double node_radius = 18.0;
    double repulsion_radius = 620.0;
    double fuzzel_repulsion = 450.0;
    bool auto_freeze = true;

    // Procedural Gradient & Grain settings loaded from settings.json
    std::string gradient_type = "diagonal_up";
    std::string color1 = "#000000";
    std::string color2 = "#18181b";
    std::string color3 = "#27272a";
    double grain_intensity = 14.0;
    std::string grain_algorithm = "gaussian";
    std::string grain_tint = "#d4d4d8";
    int grain_scale = 1;
    bool crosshair = false;

    // Simulation & Freeze state
    int active_ws_id = 1;
    double sim_time = 0.0;
    double alpha = 1.0;
    double return_timer = 0.0;
    bool physics_active = true;
    bool is_covered = false;
    bool fuzzel_active = false;

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
    double menu_w = 276.0;
    double menu_h = 198.0;
    int menu_hovered_item = -1;

    guint physics_timer_id = 0;
    std::atomic<bool> state_check_queued{false};
};

static GraphState g_graph;

static gboolean on_physics_tick(gpointer);

static void ensure_physics_timer_running() {
    if (g_graph.physics_timer_id == 0) {
        g_graph.physics_timer_id = g_timeout_add(16, on_physics_tick, nullptr);
    }
}

static void wake_physics(double new_alpha = 0.65) {
    g_graph.alpha = std::max(g_graph.alpha, new_alpha);
    g_graph.physics_active = true;
    if (!g_graph.is_covered || g_graph.fuzzel_active) {
        ensure_physics_timer_running();
        if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
    }
}

static void exec_bg(const std::string& cmd) {
    std::string full = cmd + " > /dev/null 2>&1 &";
    system(full.c_str());
}

// ──────────────────────────────────────────────────────────────────────────────
// ICON & PROCEDURAL BACKGROUND LOADERS
// ──────────────────────────────────────────────────────────────────────────────
static GdkPixbuf* get_icon_pixbuf(const std::string& icon_name, int size = 24) {
    std::string key = icon_name + "_" + std::to_string(size);
    auto it = g_graph.icon_cache.find(key);
    if (it != g_graph.icon_cache.end()) {
        return it->second;
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

    g_graph.icon_cache[key] = pb;
    return pb;
}

static void rebuild_background_surface() {
    int w = std::max(g_graph.screen_w, 1280);
    int h = std::max(g_graph.screen_h, 800);

    if (g_graph.bg_surface) {
        int cur_w = cairo_image_surface_get_width(g_graph.bg_surface);
        int cur_h = cairo_image_surface_get_height(g_graph.bg_surface);
        if (cur_w != w || cur_h != h) {
            cairo_surface_destroy(g_graph.bg_surface);
            g_graph.bg_surface = nullptr;
        }
    }

    if (!g_graph.bg_surface) {
        g_graph.bg_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    }

    render_procedural_grain_to_surface(
        g_graph.bg_surface,
        g_graph.grain_algorithm,
        g_graph.grain_intensity,
        g_graph.grain_scale,
        g_graph.gradient_type,
        g_graph.color1,
        g_graph.color2,
        g_graph.color3,
        g_graph.grain_tint,
        g_graph.crosshair
    );
}

static void load_monolith_settings() {
    std::string path = get_user_home() + "/.config/monolith/settings.json";
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;

        bool bg_changed = false;

        if (j.contains("grain")) {
            auto& gr = j["grain"];
            std::string n_gtype = gr.value("gradient_type", g_graph.gradient_type);
            std::string n_c1 = gr.value("color1", g_graph.color1);
            std::string n_c2 = gr.value("color2", g_graph.color2);
            std::string n_c3 = gr.value("color3", g_graph.color3);
            double n_intensity = gr.value("intensity", g_graph.grain_intensity);
            std::string n_algo = gr.value("algorithm", g_graph.grain_algorithm);
            std::string n_tint = gr.value("tint", g_graph.grain_tint);
            int n_scale = gr.value("scale", g_graph.grain_scale);
            bool n_cross = gr.value("crosshair", g_graph.crosshair);

            if (n_gtype != g_graph.gradient_type || n_c1 != g_graph.color1 ||
                n_c2 != g_graph.color2 || n_c3 != g_graph.color3 ||
                std::abs(n_intensity - g_graph.grain_intensity) > 0.01 ||
                n_algo != g_graph.grain_algorithm || n_tint != g_graph.grain_tint ||
                n_scale != g_graph.grain_scale || n_cross != g_graph.crosshair || !g_graph.bg_surface) {
                g_graph.gradient_type = n_gtype;
                g_graph.color1 = n_c1;
                g_graph.color2 = n_c2;
                g_graph.color3 = n_c3;
                g_graph.grain_intensity = n_intensity;
                g_graph.grain_algorithm = n_algo;
                g_graph.grain_tint = n_tint;
                g_graph.grain_scale = n_scale;
                g_graph.crosshair = n_cross;
                bg_changed = true;
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
            g_graph.node_radius = std::max(14.0, tg.value("node_radius", g_graph.node_radius));
            g_graph.repulsion_radius = tg.value("repulsion_radius", g_graph.repulsion_radius);
            g_graph.fuzzel_repulsion = tg.value("fuzzel_repulsion", g_graph.fuzzel_repulsion);
            g_graph.auto_freeze = tg.value("auto_freeze", g_graph.auto_freeze);

            for (auto& n : g_graph.nodes) {
                n.radius = g_graph.node_radius;
            }
        }

        if (bg_changed || !g_graph.bg_surface) {
            rebuild_background_surface();
        }

        wake_physics(0.5);
    } catch (...) {}
}

// ──────────────────────────────────────────────────────────────────────────────
// CONFIGURATION MANAGEMENT (taskgraph/config.json)
// ──────────────────────────────────────────────────────────────────────────────
static void check_desktop_state();

static void save_config() {
    std::string path = get_user_home() + "/.config/monolith/taskgraph/config.json";
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

static void rebuild_default_constellation_edges() {
    auto add_edge = [](const std::string& a, const std::string& b, double len) {
        if (a == b) return;
        bool has_a = false, has_b = false;
        for (const auto& n : g_graph.nodes) {
            if (n.id == a) has_a = true;
            if (n.id == b) has_b = true;
        }
        if (!has_a || !has_b) return;
        for (const auto& e : g_graph.edges) {
            if ((e.from_id == a && e.to_id == b) || (e.from_id == b && e.to_id == a)) return;
        }
        g_graph.edges.push_back({a, b, len});
    };

    std::vector<std::string> pinned_ids;
    for (const auto& n : g_graph.nodes) {
        if (n.is_pinned) pinned_ids.push_back(n.id);
    }

    size_t m = pinned_ids.size();
    if (m >= 2) {
        for (size_t i = 0; i < m; ++i) {
            add_edge(pinned_ids[i], pinned_ids[(i + 1) % m], 145.0);
        }
    }
    if (m >= 6) {
        for (size_t i = 0; i < m; i += 2) {
            add_edge(pinned_ids[i], pinned_ids[(i + 2) % m], 260.0);
        }
    }
}

static void load_config(bool preserve_positions = false) {
    std::string path = get_user_home() + "/.config/monolith/taskgraph/config.json";
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;

        // Save existing positions if reloading live
        std::unordered_map<std::string, std::pair<double, double>> old_pos;
        if (preserve_positions) {
            for (const auto& n : g_graph.nodes) {
                old_pos[n.id] = {n.x, n.y};
            }
        }

        g_graph.nodes.clear();
        g_graph.edges.clear();

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
                node.radius = g_graph.node_radius;

                if (old_pos.find(node.id) != old_pos.end()) {
                    node.x = old_pos[node.id].first;
                    node.y = old_pos[node.id].second;
                } else if (initial_positions.find(node.id) != initial_positions.end()) {
                    auto pos = initial_positions[node.id];
                    node.x = pos.first * g_graph.screen_w;
                    node.y = pos.second * g_graph.screen_h;
                } else {
                    double angle = (2.0 * M_PI * i) / std::max<size_t>(1, count);
                    node.x = (g_graph.screen_w / 2.0) + 390.0 * std::cos(angle);
                    node.y = (g_graph.screen_h / 2.0) + 250.0 * std::sin(angle);
                }

                node.drift_phase = (i * 1.17);
                node.drift_speed = 0.20 + (i % 7) * 0.02;
                node.pixbuf = get_icon_pixbuf(node.icon_name, 22);

                g_graph.nodes.push_back(node);
            }

            if (j.contains("edges") && j["edges"].is_array() && !j["edges"].empty()) {
                for (auto& item : j["edges"]) {
                    std::string from = item.value("from", "");
                    std::string to = item.value("to", "");
                    double len = item.value("rest_length", g_graph.rest_length);
                    if (!from.empty() && !to.empty()) {
                        g_graph.edges.push_back({from, to, len});
                    }
                }
            } else {
                rebuild_default_constellation_edges();
            }
        }
    } catch (...) {}
}

static void setup_file_monitors() {
    std::string base = get_user_home() + "/.config/monolith";

    // 1. Monitor settings.json
    GFile* f_set = g_file_new_for_path((base + "/settings.json").c_str());
    GFileMonitor* m_set = g_file_monitor_file(f_set, G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (m_set) {
        g_signal_connect(m_set, "changed", G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent ev, gpointer) {
            if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_CHANGED) {
                load_monolith_settings();
            }
        }), nullptr);
    }
    g_object_unref(f_set);

    // 2. Monitor taskgraph/config.json (for RAM Preload / Pin updates from Settings UI)
    GFile* f_cfg = g_file_new_for_path((base + "/taskgraph/config.json").c_str());
    GFileMonitor* m_cfg = g_file_monitor_file(f_cfg, G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (m_cfg) {
        g_signal_connect(m_cfg, "changed", G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent ev, gpointer) {
            if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_CHANGED) {
                load_config(true);
                check_desktop_state();
                wake_physics(0.4);
            }
        }), nullptr);
    }
    g_object_unref(f_cfg);

    // 3. Monitor /tmp/monolith_fuzzel_active so Fuzzel wakes TaskGraph even from deep sleep
    GFile* f_fuz = g_file_new_for_path("/tmp/monolith_fuzzel_active");
    GFileMonitor* m_fuz = g_file_monitor_file(f_fuz, G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (m_fuz) {
        g_signal_connect(m_fuz, "changed", G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer) {
            ensure_physics_timer_running();
            wake_physics(1.0);
        }), nullptr);
    }
    g_object_unref(f_fuz);
}

// ──────────────────────────────────────────────────────────────────────────────
// ZERO-FORK HYPRLAND SYNCHRONIZATION & COVERAGE STATE
// ──────────────────────────────────────────────────────────────────────────────
static std::string format_ws_badge(int ws_id, const std::string& ws_name) {
    if (ws_name.find("spotify") != std::string::npos) return "S";
    if (ws_name.find("extra") != std::string::npos) return "E";
    if (ws_name.find("ramvault") != std::string::npos) return "V";
    if (ws_id > 0) return "W" + std::to_string(ws_id);
    return "•";
}

static void check_desktop_state() {
    g_graph.state_check_queued.store(false);

    int active_ws_id = 1;
    bool has_fullscreen = false;
    bool special_open = false;
    std::string active_special_name;

    // 1. Query monitors & activeworkspace directly via HyprIPC socket (< 0.05ms, 0 forks)
    std::string mon_raw = HyprIPC::request("j/monitors");
    if (!mon_raw.empty()) {
        try {
            json mj = json::parse(mon_raw);
            if (mj.is_array() && !mj.empty()) {
                active_ws_id = mj[0].value("activeWorkspace", json::object()).value("id", 1);
                std::string sp = mj[0].value("specialWorkspace", json::object()).value("name", "");
                if (sp == "special:ramvault") {
                    // Never allow the internal RAM Vault special workspace to stay open as an overlay
                    HyprIPC::dispatch("togglespecialworkspace ramvault");
                } else if (!sp.empty() && sp != "null") {
                    special_open = true;
                    active_special_name = sp;
                }
            }
        } catch (...) {}
    }

    std::string ws_raw = HyprIPC::request("j/activeworkspace");
    if (!ws_raw.empty()) {
        try {
            json wj = json::parse(ws_raw);
            active_ws_id = wj.value("id", active_ws_id);
            has_fullscreen = wj.value("hasfullscreen", false);
        } catch (...) {}
    }
    if (active_ws_id > 0) {
        g_graph.active_ws_id = active_ws_id;
    }

    // 2. Query clients directly via HyprIPC socket
    std::string clients_raw = HyprIPC::request("j/clients");
    if (clients_raw.empty()) return;

    try {
        json clients = json::parse(clients_raw);

        std::unordered_set<std::string> active_addresses;
        std::unordered_map<std::string, json> unpinned_candidates;

        int total_on_active_ws = 0;
        int floating_on_active_ws = 0;
        int extra_ws_count = 0;

        for (auto& n : g_graph.nodes) {
            if (n.is_pinned) {
                n.is_running = false;
                n.hypr_address = "";
                n.workspace_label = "";
            }
        }

        for (const auto& c : clients) {
            std::string cls = c.value("class", "");
            std::string addr = c.value("address", "");
            bool mapped = c.value("mapped", true);
            if (!mapped || cls.empty() || cls == "monolith-constellation" || cls == "monolith-taskgraph" || cls == "monolith-toolbox" || cls == "monolith-popup") continue;

            int ws = c.value("workspace", json::object()).value("id", 1);
            std::string ws_name = c.value("workspace", json::object()).value("name", "");
            bool is_floating = c.value("floating", false);

            if (ws_name == "special:extra") {
                extra_ws_count++;
            }

            if (ws == active_ws_id && ws_name.rfind("special:", 0) != 0) {
                total_on_active_ws++;
                if (is_floating) floating_on_active_ws++;
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
                        n.workspace_label = format_ws_badge(ws, ws_name);
                        matched_pinned = true;
                        break;
                    }
                }
            }

            if (!matched_pinned) {
                // Never create dynamic unpinned nodes for hidden RAM Vault windows
                if (ws_name != "special:ramvault") {
                    unpinned_candidates[addr] = c;
                }
            }
        }

        // Intelligent Desktop Coverage Determination
        bool newly_covered = false;
        if (g_graph.auto_freeze) {
            if (has_fullscreen || special_open) {
                newly_covered = true;
            } else if (total_on_active_ws > 0 && floating_on_active_ws == 0) {
                newly_covered = true;
            }
        }

        if (newly_covered != g_graph.is_covered) {
            g_graph.is_covered = newly_covered;
            if (!g_graph.is_covered) {
                wake_physics(0.70);
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

        for (auto& n : g_graph.nodes) {
            if (!n.is_pinned) {
                auto it = unpinned_candidates.find(n.hypr_address);
                if (it != unpinned_candidates.end()) {
                    int ws = it->second.value("workspace", json::object()).value("id", 1);
                    std::string ws_name = it->second.value("workspace", json::object()).value("name", "");
                    n.workspace_id = ws;
                    n.workspace_label = format_ws_badge(ws, ws_name);
                    unpinned_candidates.erase(it);
                }
            }
        }

        // Add brand new dynamic windows
        for (const auto& [addr, c] : unpinned_candidates) {
            std::string cls = c.value("class", "");
            std::string title = c.value("title", "");
            int ws = c.value("workspace", json::object()).value("id", 1);
            std::string ws_name = c.value("workspace", json::object()).value("name", "");

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
            dn.workspace_label = format_ws_badge(ws, ws_name);
            dn.is_pinned = false;
            dn.is_running = true;
            dn.radius = g_graph.node_radius;
            dn.drift_phase = (rand() % 628) / 100.0;
            dn.drift_speed = 0.22;

            if (!g_graph.nodes.empty()) {
                const auto& ref = g_graph.nodes[rand() % g_graph.nodes.size()];
                double ang = (rand() % 360) * M_PI / 180.0;
                dn.x = std::clamp(ref.x + 210.0 * std::cos(ang), 120.0, g_graph.screen_w - 120.0);
                dn.y = std::clamp(ref.y + 210.0 * std::sin(ang), 120.0, g_graph.screen_h - 120.0);
            } else {
                dn.x = g_graph.screen_w * 0.5;
                dn.y = g_graph.screen_h * 0.5;
            }
            dn.pixbuf = get_icon_pixbuf(dn.icon_name, 22);

            g_graph.nodes.push_back(dn);

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
            if (closest_idx >= 0) {
                g_graph.edges.push_back({dn.id, g_graph.nodes[closest_idx].id, 240.0});
            }
            topology_changed = true;
        }

        // Clean dangling edges
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
            wake_physics(0.65);
        } else if (!g_graph.is_covered && g_graph.da) {
            gtk_widget_queue_draw(g_graph.da);
        }
    } catch (...) {}
}

// ──────────────────────────────────────────────────────────────────────────────
// EVENT-DRIVEN HYPRLAND SOCKET2 LISTENER (DEBOUNCED, 0% CPU WAITING)
// ──────────────────────────────────────────────────────────────────────────────
static void hypr_socket_listener_thread() {
    while (true) {
        std::string sock_path = HyprIPC::find_socket(".socket2.sock");
        if (sock_path.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        char buf[2048];
        while (true) {
            ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            buf[n] = '\0';

            // Hardware-speed guard: never allow special:ramvault to open as a visible overlay
            if (std::strstr(buf, "activespecial>>special:ramvault")) {
                HyprIPC::dispatch("togglespecialworkspace ramvault");
            }

            // When switching regular virtual workspaces (1..9), automatically close any open special overlay
            // so windows in special:extra or special:spotify never stay stuck in front of the user!
            if (std::strstr(buf, "workspace>>")) {
                std::string mraw = HyprIPC::request("j/monitors");
                if (!mraw.empty()) {
                    try {
                        json mj = json::parse(mraw);
                        if (mj.is_array() && !mj.empty()) {
                            std::string sp = mj[0].value("specialWorkspace", json::object()).value("name", "");
                            if (sp.rfind("special:", 0) == 0) {
                                HyprIPC::dispatch("togglespecialworkspace " + sp.substr(8));
                            }
                        }
                    } catch (...) {}
                }
            }

            // If a window was closed with X button, re-warm any load_to_ram app after 1.8s
            if (std::strstr(buf, "closewindow>>")) {
                g_timeout_add(1800, +[](gpointer) -> gboolean {
                    check_desktop_state();
                    for (const auto& n : g_graph.nodes) {
                        if (n.is_pinned && n.load_to_ram && !n.is_running) {
                            exec_bg("/usr/local/bin/monolith-ram-preloader warm \"" + n.id + "\"");
                        }
                    }
                    return G_SOURCE_REMOVE;
                }, nullptr);
            }

            // Filter relevant window/workspace events & coalesce rapid bursts
            if (std::strstr(buf, "workspace") || std::strstr(buf, "openwindow") ||
                std::strstr(buf, "closewindow") || std::strstr(buf, "movewindow") ||
                std::strstr(buf, "fullscreen") || std::strstr(buf, "changefloatingmode") ||
                std::strstr(buf, "activespecial")) {
                if (!g_graph.state_check_queued.exchange(true)) {
                    g_timeout_add(20, +[](gpointer) -> gboolean {
                        check_desktop_state();
                        return G_SOURCE_REMOVE;
                    }, nullptr);
                }
            }
        }

        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

static void check_fuzzel_state() {
    bool fuzzel_now = (access("/tmp/monolith_fuzzel_active", F_OK) == 0);
    if (fuzzel_now != g_graph.fuzzel_active) {
        if (!fuzzel_now && g_graph.fuzzel_active) {
            g_graph.return_timer = 1.0;
        }
        g_graph.fuzzel_active = fuzzel_now;
        wake_physics(1.0);
    }
}

struct PopupRepulsion {
    bool active = false;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    double smooth_weight = 0.0;
} g_popup_rep;

static void check_popup_state() {
    int act = 0;
    double px = 0, py = 0, pw = 0, ph = 0;
    std::ifstream f("/dev/shm/monolith_popup_active");
    if (f.is_open()) {
        f >> act >> px >> py >> pw >> ph;
    }
    g_popup_rep.active = (act != 0);
    if (g_popup_rep.active) {
        g_popup_rep.x = px;
        g_popup_rep.y = py;
        g_popup_rep.w = pw;
        g_popup_rep.h = ph;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// PHYSICS ENGINE (Force-Directed Constellation with Critical Damping & Orbit)
// ──────────────────────────────────────────────────────────────────────────────
static void update_physics() {
    if (g_popup_rep.active) {
        g_popup_rep.smooth_weight += (1.0 - g_popup_rep.smooth_weight) * 0.22;
        wake_physics(1.0);
    } else {
        g_popup_rep.smooth_weight *= 0.84;
    }

    if (g_graph.is_covered && !g_graph.fuzzel_active && g_popup_rep.smooth_weight < 0.01 && g_graph.return_timer <= 0.0) {
        return;
    }

    if (g_graph.return_timer > 0.0) {
        g_graph.return_timer = std::max(0.0, g_graph.return_timer - 0.04);
    }

    bool is_returning = (g_graph.return_timer > 0.001);

    if (g_graph.alpha < 0.20) {
        g_graph.alpha = 0.20;
    }
    g_graph.physics_active = true;

    size_t n = g_graph.nodes.size();
    if (n == 0) return;

    g_graph.sim_time += 0.014;

    double fcx = g_graph.screen_w / 2.0;
    double fcy = g_graph.screen_h / 2.0;

    double effective_damping = g_graph.fuzzel_active ? 0.77 : (is_returning ? 0.81 : g_graph.damping);
    double spring_mult = g_graph.fuzzel_active ? 0.18 : (1.0 + 1.8 * g_graph.return_timer);
    double grav_mult = g_graph.fuzzel_active ? 0.12 : (1.0 + 3.8 * g_graph.return_timer);

    for (size_t i = 0; i < n; ++i) {
        auto& node = g_graph.nodes[i];
        if (node.is_dragged) continue;

        double t = g_graph.sim_time;
        double fx = 0.18 * std::sin(node.drift_phase + t * node.drift_speed)
                  + 0.10 * std::cos(node.drift_phase * 1.73 + t * (node.drift_speed * 0.7));
        double fy = 0.18 * std::cos(node.drift_phase * 1.41 + t * (node.drift_speed * 0.85))
                  + 0.10 * std::sin(node.drift_phase * 2.19 + t * (node.drift_speed * 0.6));

        // 1. Fuzzel App Menu Smoothstep Repulsion Field
        if (g_graph.fuzzel_active) {
            double fdx = node.x - fcx;
            double fdy = node.y - fcy;
            double rx = g_graph.repulsion_radius * 0.70;
            double ry = g_graph.repulsion_radius * 0.50;
            double dist = std::hypot(fdx / rx, fdy / ry);
            if (dist < 1.0) {
                double u = std::clamp((1.0 - dist), 0.0, 1.0);
                double smooth_push = u * u * (3.0 - 2.0 * u) * 13.0;
                double dir_len = std::hypot(fdx, fdy) + 0.001;
                fx += (fdx / dir_len) * smooth_push;
                fy += (fdy / dir_len) * smooth_push;
            }
        }

        // 1b. Top-Bar Popup Deflection Field
        if (g_popup_rep.smooth_weight > 0.01) {
            double pad = 65.0;
            double min_x = g_popup_rep.x - pad;
            double max_x = g_popup_rep.x + g_popup_rep.w + pad;
            double min_y = g_popup_rep.y - pad;
            double max_y = g_popup_rep.y + g_popup_rep.h + pad;

            if (node.x >= min_x && node.x <= max_x && node.y >= min_y && node.y <= max_y) {
                double pcx = g_popup_rep.x + g_popup_rep.w * 0.45;
                double pcy = g_popup_rep.y + g_popup_rep.h * 0.45;
                double dx = node.x - pcx;
                double dy = node.y - pcy;

                if (dx > 0) dx = -std::abs(dx) - 40.0;
                if (dy < 0) dy = std::abs(dy) + 50.0;

                double d = std::hypot(dx, dy) + 0.1;
                double rx = (g_popup_rep.w / 2.0) + pad;
                double ry = (g_popup_rep.h / 2.0) + pad;
                double pen_x = std::clamp((rx - std::abs(node.x - pcx)) / rx, 0.0, 1.0);
                double pen_y = std::clamp((ry - std::abs(node.y - pcy)) / ry, 0.0, 1.0);
                double pen = std::max(pen_x, pen_y);

                double push = pen * pen * (3.0 - 2.0 * pen) * 24.0 * g_popup_rep.smooth_weight;
                fx += (dx / d) * push;
                fy += (dy / d) * push;
            }
        }

        // 2. Central Gravitation
        fx += (fcx - node.x) * (g_graph.center_gravity * grav_mult);
        fy += (fcy - node.y) * (g_graph.center_gravity * grav_mult);

        // 3. Mutual Node Repulsion + Personal Bubble
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            double dx = node.x - g_graph.nodes[j].x;
            double dy = node.y - g_graph.nodes[j].y;
            double d = std::hypot(dx, dy) + 0.001;

            if (d < 750.0) {
                double rep = (g_graph.repulsion * 1.8) / (d * d + 400.0);
                fx += (dx / d) * rep;
                fy += (dy / d) * rep;
            }

            const double personal_bubble = 180.0;
            if (d < personal_bubble) {
                double u = (personal_bubble - d) / personal_bubble;
                double cushion_push = u * u * (3.0 - 2.0 * u) * 26.0;
                fx += (dx / d) * cushion_push;
                fy += (dy / d) * cushion_push;
            }
        }

        // 4. Elastic Springs with Dashpot Damping
        for (const auto& e : g_graph.edges) {
            std::string other_id;
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
                        double f = diff * (g_graph.spring_k * spring_mult);
                        fx += (dx / d) * f;
                        fy += (dy / d) * f;

                        double rel_vx = other.vx - node.vx;
                        double rel_vy = other.vy - node.vy;
                        fx += rel_vx * (0.04 * spring_mult);
                        fy += rel_vy * (0.04 * spring_mult);
                        break;
                    }
                }
            }
        }

        // 5. Screen Boundary Aerodynamic Cushion Force Field
        double pad_x = node.radius + 36.0;
        double pad_y_top = node.radius + 50.0;
        double pad_y_bot = node.radius + 38.0;

        double margin_x = 180.0;
        double margin_y = 130.0;

        double dist_left = node.x - pad_x;
        if (dist_left < margin_x) {
            double u = std::clamp((margin_x - dist_left) / margin_x, 0.0, 1.0);
            fx += u * u * 5.0;
            if (node.vx < 0.0) node.vx *= (1.0 - 0.44 * u);
        }

        double dist_right = (g_graph.screen_w - pad_x) - node.x;
        if (dist_right < margin_x) {
            double u = std::clamp((margin_x - dist_right) / margin_x, 0.0, 1.0);
            fx -= u * u * 5.0;
            if (node.vx > 0.0) node.vx *= (1.0 - 0.44 * u);
        }

        double dist_top = node.y - pad_y_top;
        if (dist_top < margin_y) {
            double u = std::clamp((margin_y - dist_top) / margin_y, 0.0, 1.0);
            fy += u * u * 5.0;
            if (node.vy < 0.0) node.vy *= (1.0 - 0.44 * u);
        }

        double dist_bot = (g_graph.screen_h - pad_y_bot) - node.y;
        if (dist_bot < margin_y) {
            double u = std::clamp((margin_y - dist_bot) / margin_y, 0.0, 1.0);
            fy -= u * u * 5.0;
            if (node.vy > 0.0) node.vy *= (1.0 - 0.44 * u);
        }

        // 6. Velocity Integration
        node.vx = (node.vx + fx * g_graph.alpha) * effective_damping;
        node.vy = (node.vy + fy * g_graph.alpha) * effective_damping;

        // 7. Subtle 16-second Orbital Rotation when Fuzzel Menu is Open
        if (g_graph.fuzzel_active) {
            double fdx = node.x - fcx;
            double fdy = node.y - fcy;
            double rx = g_graph.repulsion_radius * 0.70;
            double ry = g_graph.repulsion_radius * 0.50;

            double omega = (2.0 * M_PI) / (16.0 * 60.0);
            double target_vx = -omega * (rx / ry) * fdy;
            double target_vy =  omega * (ry / rx) * fdx;

            node.vx += (target_vx - node.vx) * 0.08;
            node.vy += (target_vy - node.vy) * 0.08;
        }

        double max_speed = g_graph.fuzzel_active ? 6.2 : (is_returning ? (1.3 + 4.2 * g_graph.return_timer) : 1.3);
        node.vx = std::clamp(node.vx, -max_speed, max_speed);
        node.vy = std::clamp(node.vy, -max_speed, max_speed);

        node.x += node.vx;
        node.y += node.vy;

        node.x = std::clamp(node.x, pad_x, g_graph.screen_w - pad_x);
        node.y = std::clamp(node.y, pad_y_top, g_graph.screen_h - pad_y_bot);
    }

    if (!g_graph.fuzzel_active && !is_returning) {
        g_graph.alpha = std::max(0.20, g_graph.alpha * 0.96);
    }

    for (auto& node : g_graph.nodes) {
        double target_scale = node.is_hovered ? 1.16 : 1.0;
        node.cur_scale += (target_scale - node.cur_scale) * 0.24;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// ARCHITECTURAL RENDERING ENGINE
// ──────────────────────────────────────────────────────────────────────────────
static void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_path(cr);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

static void draw_graph(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer) {
    if (width != g_graph.screen_w || height != g_graph.screen_h) {
        g_graph.screen_w = width;
        g_graph.screen_h = height;
        rebuild_background_surface();
    }

    // ──────────────── 0. UNIFIED PROCEDURAL GRADIENT + GRAIN BASE ────────────
    if (g_graph.bg_surface) {
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(cr, g_graph.bg_surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_paint(cr);
    }

    std::unordered_map<std::string, const AppNode*> id_map;
    for (const auto& n : g_graph.nodes) id_map[n.id] = &n;

    // ──────────────── 1. CONSTELLATION EDGES (CLIPPED AT NODE RIMS) ──────────
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    double pulse_phase = std::fmod(g_graph.sim_time * 0.65, 1.0);

    for (const auto& e : g_graph.edges) {
        auto it1 = id_map.find(e.from_id);
        auto it2 = id_map.find(e.to_id);
        if (it1 == id_map.end() || it2 == id_map.end()) continue;

        const AppNode* n1 = it1->second;
        const AppNode* n2 = it2->second;

        double dx = n2->x - n1->x;
        double dy = n2->y - n1->y;
        double dist = std::hypot(dx, dy);
        double r1 = n1->radius * n1->cur_scale + 2.0;
        double r2 = n2->radius * n2->cur_scale + 2.0;

        if (dist <= r1 + r2 + 4.0) continue;

        double ux = dx / dist;
        double uy = dy / dist;
        double sx = n1->x + ux * r1;
        double sy = n1->y + uy * r1;
        double ex = n2->x - ux * r2;
        double ey = n2->y - uy * r2;

        bool is_highlighted = n1->is_hovered || n2->is_hovered || n1->is_dragged || n2->is_dragged;
        bool both_running = n1->is_running && n2->is_running;

        if (is_highlighted) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.88);
            cairo_set_line_width(cr, g_graph.edge_width + 0.5);
        } else if (both_running) {
            cairo_set_source_rgba(cr, 0.85, 0.85, 0.90, std::min(0.80, g_graph.edge_alpha + 0.20));
            cairo_set_line_width(cr, g_graph.edge_width + 0.15);
        } else {
            double dist_fade = std::clamp(1.0 - (dist / 680.0), 0.25, 1.0);
            cairo_set_source_rgba(cr, 0.72, 0.72, 0.78, g_graph.edge_alpha * dist_fade);
            cairo_set_line_width(cr, g_graph.edge_width);
        }

        cairo_move_to(cr, sx, sy);
        cairo_line_to(cr, ex, ey);
        cairo_stroke(cr);

        // Subtle telemetry signal pulse traveling along active/hovered edges
        if (is_highlighted || both_running) {
            double px = sx + (ex - sx) * pulse_phase;
            double py = sy + (ey - sy) * pulse_phase;
            cairo_new_sub_path(cr);
            cairo_arc(cr, px, py, is_highlighted ? 2.2 : 1.6, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, is_highlighted ? 0.95 : 0.65);
            cairo_fill(cr);
        }
    }

    // ──────────────── 2. OBSIDIAN NODE DISCS, HALOS, ICONS & BADGES ──────────
    cairo_select_font_face(cr, "JetBrains Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    for (const auto& node : g_graph.nodes) {
        double r = node.radius * node.cur_scale;
        cairo_new_path(cr);

        // Soft radial aura behind hovered or running nodes
        if (node.is_hovered || node.is_dragged) {
            cairo_pattern_t* glow = cairo_pattern_create_radial(node.x, node.y, r * 0.5, node.x, node.y, r * 2.1);
            cairo_pattern_add_color_stop_rgba(glow, 0.0, 1.0, 1.0, 1.0, 0.18);
            cairo_pattern_add_color_stop_rgba(glow, 1.0, 1.0, 1.0, 1.0, 0.0);
            cairo_set_source(cr, glow);
            cairo_new_sub_path(cr);
            cairo_arc(cr, node.x, node.y, r * 2.1, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_pattern_destroy(glow);
        }

        // Running outer orbital ring
        if (node.is_running) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, node.x, node.y, r + 4.5, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, node.is_hovered ? 0.60 : 0.30);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }

        // Obsidian core disc (#07070a) so icons pop with razor-sharp contrast
        cairo_new_sub_path(cr);
        cairo_arc(cr, node.x, node.y, r, 0, 2 * M_PI);
        if (node.is_hovered || node.is_dragged) {
            cairo_set_source_rgba(cr, 0.09, 0.09, 0.12, 0.96);
        } else if (node.is_running) {
            cairo_set_source_rgba(cr, 0.05, 0.05, 0.07, 0.94);
        } else {
            cairo_set_source_rgba(cr, 0.025, 0.025, 0.035, 0.90);
        }
        cairo_fill_preserve(cr);

        // Precision architectural perimeter rim
        if (node.is_hovered || node.is_dragged) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 1.7);
        } else if (node.is_running) {
            cairo_set_source_rgba(cr, 0.92, 0.92, 0.96, 0.85);
            cairo_set_line_width(cr, 1.3);
        } else {
            cairo_set_source_rgba(cr, 0.32, 0.32, 0.38, 0.72);
            cairo_set_line_width(cr, 1.1);
        }
        cairo_stroke(cr);

        // Application Icon (cleanly centered, never obstructed!)
        if (node.pixbuf) {
            int icon_w = gdk_pixbuf_get_width(node.pixbuf);
            int icon_h = gdk_pixbuf_get_height(node.pixbuf);

            cairo_save(cr);
            cairo_translate(cr, node.x, node.y);
            if (node.cur_scale != 1.0) {
                cairo_scale(cr, node.cur_scale, node.cur_scale);
            }
            gdk_cairo_set_source_pixbuf(cr, node.pixbuf, -icon_w / 2.0, -icon_h / 2.0);
            cairo_paint_with_alpha(cr, (node.is_running || node.is_hovered) ? 1.0 : 0.88);
            cairo_restore(cr);
        }

        // Top-Right Badge: Resident in Physical RAM Cache (Emerald Beacon)
        if (node.load_to_ram) {
            double bx = node.x + r * 0.72;
            double by = node.y - r * 0.72;
            cairo_new_sub_path(cr);
            cairo_arc(cr, bx, by, 5.2, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.03, 0.03, 0.04);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.13, 0.77, 0.37, 0.95);
            cairo_set_line_width(cr, 1.2);
            cairo_stroke(cr);

            cairo_new_sub_path(cr);
            cairo_arc(cr, bx, by, 2.5, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.29, 0.87, 0.50);
            cairo_fill(cr);
        }

        // Top-Left Badge: Workspace Indicator for Running Apps (W1..W9, S, E)
        if (node.is_running && !node.workspace_label.empty()) {
            cairo_set_font_size(cr, 8.0);
            cairo_text_extents_t wext;
            cairo_text_extents(cr, node.workspace_label.c_str(), &wext);
            double bw = std::max(14.0, wext.width + 7.0);
            double bh = 12.0;
            double bx = node.x - r * 0.80 - bw / 2.0;
            double by = node.y - r * 0.80 - bh / 2.0;

            draw_rounded_rect(cr, bx, by, bw, bh, 3.5);
            if (node.workspace_label == "S" || node.workspace_label == "V") {
                cairo_set_source_rgb(cr, 0.04, 0.14, 0.08);
            } else if (node.workspace_label == "E") {
                cairo_set_source_rgb(cr, 0.15, 0.10, 0.03);
            } else {
                cairo_set_source_rgb(cr, 0.08, 0.08, 0.11);
            }
            cairo_fill_preserve(cr);

            if (node.workspace_label == "S" || node.workspace_label == "V") {
                cairo_set_source_rgb(cr, 0.18, 0.80, 0.44);
            } else if (node.workspace_label == "E") {
                cairo_set_source_rgb(cr, 0.96, 0.62, 0.04);
            } else {
                cairo_set_source_rgba(cr, 0.75, 0.75, 0.82, 0.85);
            }
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_move_to(cr, bx + (bw - wext.width) / 2.0 - wext.x_bearing, by + (bh - wext.height) / 2.0 - wext.y_bearing);
            cairo_show_text(cr, node.workspace_label.c_str());
        }

        // Bottom Label Pill
        cairo_set_font_size(cr, 9.5);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, node.name.c_str(), &ext);
        double tx = node.x - ext.width / 2.0;
        double ty = node.y + r + 15.0;

        double pw = ext.width + 12.0;
        double ph = ext.height + 6.0;
        double px = node.x - pw / 2.0;
        double py = ty - ext.height - 3.0;

        draw_rounded_rect(cr, px, py, pw, ph, 4.0);
        cairo_set_source_rgba(cr, 0.02, 0.02, 0.03, 0.92);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.16, 0.16, 0.20, node.is_hovered ? 0.95 : 0.65);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        if (node.is_hovered || node.is_running) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.98);
        } else {
            cairo_set_source_rgba(cr, 0.82, 0.82, 0.86, 0.88);
        }
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, node.name.c_str());
        cairo_new_path(cr);
    }

    // ──────────────── 3. 5-ACTION ARCHITECTURAL OLED CONTEXT MENU ────────────
    if (g_graph.menu_open && g_graph.menu_node_idx >= 0 && g_graph.menu_node_idx < static_cast<int>(g_graph.nodes.size())) {
        const auto& node = g_graph.nodes[g_graph.menu_node_idx];

        int num_items = node.is_running ? 5 : 4;
        double item_h = 30.0;
        double menu_h = 44.0 + num_items * (item_h + 2.0) + 6.0;
        g_graph.menu_h = menu_h;

        double mx = std::clamp(g_graph.menu_x, 15.0, g_graph.screen_w - g_graph.menu_w - 15.0);
        double my = std::clamp(g_graph.menu_y, 42.0, g_graph.screen_h - menu_h - 15.0);

        draw_rounded_rect(cr, mx, my, g_graph.menu_w, menu_h, 8.0);
        cairo_set_source_rgba(cr, 0.03, 0.03, 0.04, 0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.28, 0.28, 0.34, 0.95);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);

        // Header
        cairo_select_font_face(cr, "JetBrains Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, mx + 14.0, my + 24.0);
        std::string header = node.name + (node.is_running ? ("  [" + node.workspace_label + "]") : "  [STANDBY]");
        cairo_show_text(cr, header.c_str());

        cairo_set_source_rgba(cr, 0.18, 0.18, 0.22, 0.90);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, mx + 10.0, my + 34.0);
        cairo_line_to(cr, mx + g_graph.menu_w - 10.0, my + 34.0);
        cairo_stroke(cr);

        double start_y = my + 40.0;
        cairo_set_font_size(cr, 10.5);

        auto draw_menu_row = [&](int idx, const std::string& label, double r_col, double g_col, double b_col) {
            double ry = start_y + idx * (item_h + 2.0);
            if (g_graph.menu_hovered_item == idx) {
                draw_rounded_rect(cr, mx + 6.0, ry, g_graph.menu_w - 12.0, item_h, 5.0);
                cairo_set_source_rgba(cr, 0.16, 0.16, 0.20, 0.95);
                cairo_fill(cr);
            }
            cairo_set_source_rgb(cr, r_col, g_col, b_col);
            cairo_move_to(cr, mx + 14.0, ry + 19.5);
            cairo_show_text(cr, label.c_str());
        };

        // Item 0: Focus or Launch
        std::string item0_lbl = (node.workspace_label == "V") ? "⚡  Instant Launch (RAM Vault)" : (node.is_running ? "▶  Focus Window" : "▶  Launch Application");
        draw_menu_row(0, item0_lbl, 1.0, 1.0, 1.0);

        // Item 1: Stash to Extra Shelf (if running) or Launch New Instance
        if (node.is_running) {
            bool in_extra = (node.workspace_label == "E");
            draw_menu_row(1, in_extra ? "▲  Restore from Extra Shelf" : "▼  Stash to Extra Shelf", 0.96, 0.72, 0.25);
        } else {
            draw_menu_row(1, "+  Launch Fresh Instance", 0.82, 0.82, 0.86);
        }

        // Item 2: Pin / Unpin
        draw_menu_row(2, node.is_pinned ? "✕  Unpin from Constellation" : "★  Pin to Constellation", 0.88, 0.88, 0.92);

        // Item 3: Load to RAM
        if (node.load_to_ram) {
            draw_menu_row(3, "●  RAM Cache: [ RESIDENT ]", 0.29, 0.87, 0.50);
        } else {
            draw_menu_row(3, "○  Load to RAM: [ DISABLED ]", 0.65, 0.65, 0.70);
        }

        // Item 4: Close Window (only when running)
        if (node.is_running) {
            draw_menu_row(4, "■  Close Running Window", 0.97, 0.44, 0.44);
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

static void focus_or_launch_node(const AppNode& node) {
    if (node.is_running && !node.hypr_address.empty()) {
        if (node.workspace_label == "V" || node.workspace_label == "E") {
            // Instant <1ms summon from Warm RAM Vault or Extra Shelf onto current regular workspace!
            std::string target_ws = std::to_string(std::max(1, g_graph.active_ws_id));
            HyprIPC::batch({
                "dispatch movetoworkspace " + target_ws + ",address:" + node.hypr_address,
                "dispatch focuswindow address:" + node.hypr_address
            });
            check_desktop_state();
        } else if (node.workspace_label == "S") {
            HyprIPC::dispatch("togglespecialworkspace spotify");
        } else {
            HyprIPC::dispatch("focuswindow address:" + node.hypr_address);
        }
    } else if (!node.exec_cmd.empty()) {
        exec_bg("/usr/local/bin/monolith-ram-preloader boost 3.5");
        exec_bg(node.exec_cmd);
    }
}

static void on_drag_begin(GtkGestureDrag*, double start_x, double start_y, gpointer) {
    g_graph.dragged_node_idx = -1;

    if (g_graph.menu_open && g_graph.menu_node_idx >= 0 && g_graph.menu_node_idx < static_cast<int>(g_graph.nodes.size())) {
        double mx = std::clamp(g_graph.menu_x, 15.0, g_graph.screen_w - g_graph.menu_w - 15.0);
        double my = std::clamp(g_graph.menu_y, 42.0, g_graph.screen_h - g_graph.menu_h - 15.0);

        if (start_x >= mx && start_x <= mx + g_graph.menu_w && start_y >= my + 40.0 && start_y <= my + g_graph.menu_h) {
            int clicked_item = static_cast<int>((start_y - (my + 40.0)) / 32.0);
            auto& target_node = g_graph.nodes[g_graph.menu_node_idx];

            if (clicked_item == 0) {
                focus_or_launch_node(target_node);
            } else if (clicked_item == 1) {
                if (target_node.is_running && !target_node.hypr_address.empty()) {
                    if (target_node.workspace_label == "E") {
                        std::string target_ws = std::to_string(std::max(1, g_graph.active_ws_id));
                        HyprIPC::dispatch("movetoworkspace " + target_ws + ",address:" + target_node.hypr_address);
                    } else {
                        HyprIPC::dispatch("movetoworkspacesilent special:extra,address:" + target_node.hypr_address);
                    }
                    check_desktop_state();
                } else if (!target_node.exec_cmd.empty()) {
                    exec_bg(target_node.exec_cmd);
                }
            } else if (clicked_item == 2) {
                target_node.is_pinned = !target_node.is_pinned;
                if (!target_node.is_pinned) {
                    target_node.load_to_ram = false;
                }
                save_config();
            } else if (clicked_item == 3) {
                target_node.load_to_ram = !target_node.load_to_ram;
                if (target_node.load_to_ram) {
                    target_node.is_pinned = true;
                }
                save_config();
                std::string action = target_node.load_to_ram ? "load" : "unload";
                exec_bg("/usr/local/bin/monolith-ram-preloader " + action + " \"" + target_node.id + "\"");
            } else if (clicked_item == 4 && target_node.is_running && !target_node.hypr_address.empty()) {
                HyprIPC::dispatch("closewindow address:" + target_node.hypr_address);
            }

            g_graph.menu_open = false;
            g_graph.menu_node_idx = -1;
            if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
            return;
        } else {
            g_graph.menu_open = false;
            g_graph.menu_node_idx = -1;
            if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
            return;
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
        wake_physics(0.45);
    }
}

static void on_drag_end(GtkGestureDrag*, double, double, gpointer) {
    if (g_graph.dragged_node_idx >= 0 && g_graph.dragged_node_idx < static_cast<int>(g_graph.nodes.size())) {
        auto& node = g_graph.nodes[g_graph.dragged_node_idx];
        node.is_dragged = false;

        if (g_graph.drag_dist < 6.0) {
            focus_or_launch_node(node);
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
    } else if (g_graph.menu_open) {
        g_graph.menu_open = false;
        g_graph.menu_node_idx = -1;
        if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
    }
}

static void on_middle_click(GtkGestureClick*, int, double x, double y, gpointer) {
    int idx = hit_test_node(x, y);
    if (idx >= 0 && g_graph.nodes[idx].is_running && !g_graph.nodes[idx].hypr_address.empty()) {
        auto& n = g_graph.nodes[idx];
        if (n.workspace_label == "E") {
            std::string target_ws = std::to_string(std::max(1, g_graph.active_ws_id));
            HyprIPC::dispatch("movetoworkspace " + target_ws + ",address:" + n.hypr_address);
        } else {
            HyprIPC::dispatch("movetoworkspacesilent special:extra,address:" + n.hypr_address);
        }
        check_desktop_state();
    }
}

static void on_motion(GtkEventControllerMotion*, double x, double y, gpointer) {
    if (g_graph.menu_open) {
        double mx = std::clamp(g_graph.menu_x, 15.0, g_graph.screen_w - g_graph.menu_w - 15.0);
        double my = std::clamp(g_graph.menu_y, 42.0, g_graph.screen_h - g_graph.menu_h - 15.0);

        if (x >= mx && x <= mx + g_graph.menu_w && y >= my + 40.0 && y <= my + g_graph.menu_h) {
            int item = static_cast<int>((y - (my + 40.0)) / 32.0);
            if (item != g_graph.menu_hovered_item) {
                g_graph.menu_hovered_item = item;
                if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
            }
            return;
        } else if (g_graph.menu_hovered_item != -1) {
            g_graph.menu_hovered_item = -1;
            if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
        }
    }

    int hovered_idx = hit_test_node(x, y);
    bool changed = false;
    for (size_t i = 0; i < g_graph.nodes.size(); ++i) {
        bool h = (static_cast<int>(i) == hovered_idx);
        if (g_graph.nodes[i].is_hovered != h) {
            g_graph.nodes[i].is_hovered = h;
            changed = true;
        }
    }

    if (changed) {
        wake_physics(0.35);
    }

    GtkNative* native = gtk_widget_get_native(g_graph.da);
    if (native) {
        GdkSurface* surf = gtk_native_get_surface(native);
        if (surf) {
            GdkCursor* cur = gdk_cursor_new_from_name((hovered_idx >= 0 || g_graph.menu_open) ? "pointer" : "default", nullptr);
            gdk_surface_set_cursor(surf, cur);
            g_object_unref(cur);
        }
    }
}

static gboolean on_physics_tick(gpointer) {
    static uint32_t tick_count = 0;
    ++tick_count;

    if ((tick_count & 3) == 0 || g_graph.fuzzel_active || g_popup_rep.active) {
        check_fuzzel_state();
        check_popup_state();
    }

    // TRUE ZERO-CPU SLEEP: If desktop is covered and no overlay menu is active, stop timer completely!
    if (g_graph.is_covered && !g_graph.fuzzel_active && !g_popup_rep.active && g_popup_rep.smooth_weight < 0.01 && g_graph.return_timer <= 0.0) {
        g_graph.physics_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    bool high_activity = (g_graph.dragged_node_idx >= 0) ||
                         g_graph.fuzzel_active ||
                         g_popup_rep.active ||
                         (g_popup_rep.smooth_weight > 0.01) ||
                         (g_graph.return_timer > 0.001) ||
                         (g_graph.alpha > 0.22);

    if (!high_activity) {
        for (const auto& n : g_graph.nodes) {
            double target_scale = n.is_hovered ? 1.16 : 1.0;
            if (n.is_hovered || std::abs(n.cur_scale - target_scale) > 0.005 || std::hypot(n.vx, n.vy) > 0.18) {
                high_activity = true;
                break;
            }
        }
    }

    // Adaptive Frame Rate: 60 FPS during interaction/settlement, 20 FPS during sub-pixel ambient drift
    if (!high_activity && (tick_count % 3 != 0)) {
        return G_SOURCE_CONTINUE;
    }

    update_physics();
    if (g_graph.da) gtk_widget_queue_draw(g_graph.da);
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

    // Load pinned nodes first, then apply authoritative settings.json parameters & build background
    load_config(false);
    load_monolith_settings();
    setup_file_monitors();

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, "window, .taskgraph-window, drawingarea { background: #000000; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    g_graph.win = gtk_window_new();
    gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(g_graph.win));

    gtk_layer_init_for_window(GTK_WINDOW(g_graph.win));
    gtk_layer_set_layer(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_LAYER_BOTTOM);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(g_graph.win), -1);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(g_graph.win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_namespace(GTK_WINDOW(g_graph.win), "monolith-constellation");

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

    GtkGesture* middle_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle_click), GDK_BUTTON_MIDDLE);
    g_signal_connect(middle_click, "pressed", G_CALLBACK(on_middle_click), nullptr);
    gtk_widget_add_controller(g_graph.da, GTK_EVENT_CONTROLLER(middle_click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), nullptr);
    gtk_widget_add_controller(g_graph.da, motion);

    gtk_window_present(GTK_WINDOW(g_graph.win));

    check_desktop_state();
    ensure_physics_timer_running();

    std::thread(hypr_socket_listener_thread).detach();
}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("org.monolith.constellation", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
