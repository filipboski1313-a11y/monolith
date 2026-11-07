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
#include <cstring>
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
// MONOLITH CONSTELLATION & PROCEDURAL BACKGROUND ENGINE 2.0
//   - Multi-Monitor Decoupled State & Per-Output Coordinate System (1:1 Native Resolution)
//   - Zero-Fork UNIX Socket IPC with Hyprland (.socket.sock & .socket2.sock)
//   - True 0.00% CPU Sleep: Timer sleeps when all active screens are covered
//   - Unified Procedural Gradient + Film Grain Base Layer (Native per-display resolution)
//   - Obsidian Node Discs (clean rim clipping, crisp icons and typography)
//   - Isolated Per-Display Super Launcher (Fuzzel) Deflection & Hover Highlights
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

    // Ambient Lissajous drift parameters
    double drift_phase = 0.0;
    double drift_speed = 0.22;

    GdkPixbuf* pixbuf = nullptr;
};

struct AppEdge {
    std::string from_id;
    std::string to_id;
    double rest_length = 260.0;
};

struct PinnedTemplate {
    std::string id;
    std::string name;
    std::string icon_name;
    std::string exec_cmd;
    std::string match_class;
    bool load_to_ram = false;
};

struct OutputWindow {
    GtkWidget* win = nullptr;
    GtkWidget* da = nullptr;
    GdkMonitor* monitor = nullptr;
    std::string name;
    int width = 1920;
    int height = 1080;
    cairo_surface_t* bg_surface = nullptr;

    double scale = 1.0;
    double scale_osd_alpha = 0.0;
    std::string scale_osd_text;

    // Per-output isolated simulation & interaction state
    std::vector<AppNode> nodes;
    std::vector<AppEdge> edges;
    int hovered_idx = -1;
    int dragged_node_idx = -1;
    double drag_start_x = 0.0;
    double drag_start_y = 0.0;
    double drag_dist = 0.0;

    bool menu_open = false;
    int menu_node_idx = -1;
    double menu_x = 0.0;
    double menu_y = 0.0;
    double menu_w = 286.0;
    double menu_h = 198.0;
    int menu_hovered_item = -1;

    double sim_time = 0.0;
    double alpha = 1.0;
    double return_timer = 0.0;
    bool is_covered = false;
    bool fuzzel_active = false;
    bool physics_active = true;
};

struct GraphState {
    GtkIconTheme* icon_theme = nullptr;
    GApplication* app = nullptr;

    std::vector<PinnedTemplate> pinned_templates;
    std::vector<AppEdge> template_edges;
    std::unordered_map<std::string, GdkPixbuf*> icon_cache;

    cairo_surface_t* bg_surface = nullptr;

    std::string display_output = "all";
    std::vector<OutputWindow*> outputs;

    int screen_w = 1920;
    int screen_h = 1080;

    // Physics parameters loaded from settings.json
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
    double base_scale = 1.0;
    std::unordered_map<std::string, double> monitor_scales;

    // Procedural gradient & grain parameters
    std::string gradient_type = "diagonal_up";
    std::string color1 = "#000000";
    std::string color2 = "#18181b";
    std::string color3 = "#27272a";
    double grain_intensity = 14.0;
    std::string grain_algorithm = "gaussian";
    std::string grain_tint = "#d4d4d8";
    int grain_scale = 1;
    bool crosshair = false;

    int active_ws_id = 1;

    guint physics_timer_id = 0;
    std::atomic<bool> state_check_queued{false};
};

static GraphState g_graph;

static gboolean on_physics_tick(gpointer);
static void sync_output_windows(GApplication* app);
static void check_desktop_state();

static void ensure_physics_timer_running() {
    if (g_graph.physics_timer_id == 0) {
        g_graph.physics_timer_id = g_timeout_add(16, on_physics_tick, nullptr);
    }
}

static void wake_output_physics(OutputWindow* out, double new_alpha = 0.65) {
    if (!out) return;
    out->alpha = std::max(out->alpha, new_alpha);
    out->physics_active = true;
    ensure_physics_timer_running();
    if (out->da) gtk_widget_queue_draw(out->da);
}

static void wake_all_outputs_physics(double new_alpha = 0.65) {
    for (auto* out : g_graph.outputs) {
        if (out) wake_output_physics(out, new_alpha);
    }
}


static void exec_bg(const std::string& cmd) {
    std::string full = cmd + " > /dev/null 2>&1 &";
    system(full.c_str());
}

static bool case_insensitive_contains(const std::string& haystack, const std::string& needle) {
    if (needle.empty() || haystack.empty()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) {
            return std::tolower(static_cast<unsigned char>(ch1)) == std::tolower(static_cast<unsigned char>(ch2));
        }
    );
    return (it != haystack.end());
}

static std::string resolve_app_command(const std::string& app_id, const std::string& raw_cmd) {
    std::string id_lower = app_id;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), [](unsigned char c){ return std::tolower(c); });

    // Known application smart fallbacks for cross-distro compatibility (Fedora / Arch / Ubuntu)
    if (id_lower == "obsidian") {
        return "(command -v flatpak >/dev/null 2>&1 && flatpak info md.obsidian.Obsidian >/dev/null 2>&1 && flatpak run md.obsidian.Obsidian) || (command -v obsidian >/dev/null 2>&1 && exec obsidian) || obsidian";
    }
    if (id_lower == "vesktop" || id_lower == "discord") {
        return "(command -v vesktop >/dev/null 2>&1 && exec vesktop) || (command -v flatpak >/dev/null 2>&1 && flatpak info dev.vencord.Vesktop >/dev/null 2>&1 && flatpak run dev.vencord.Vesktop) || (command -v discord >/dev/null 2>&1 && exec discord) || discord";
    }
    if (id_lower == "spotify") {
        return "(test -x \"$HOME/.local/bin/spotify\" && exec \"$HOME/.local/bin/spotify\") || (command -v spotify >/dev/null 2>&1 && exec spotify) || (command -v flatpak >/dev/null 2>&1 && flatpak info com.spotify.Client >/dev/null 2>&1 && flatpak run com.spotify.Client) || spotify";
    }
    if (id_lower == "caprine") {
        return "(command -v flatpak >/dev/null 2>&1 && flatpak info com.sindresorhus.Caprine >/dev/null 2>&1 && flatpak run com.sindresorhus.Caprine) || (command -v caprine >/dev/null 2>&1 && exec caprine) || caprine";
    }
    if (id_lower == "signal") {
        return "(command -v flatpak >/dev/null 2>&1 && flatpak info org.signal.Signal >/dev/null 2>&1 && flatpak run org.signal.Signal) || (command -v signal-desktop >/dev/null 2>&1 && exec signal-desktop) || signal-desktop";
    }
    if (id_lower == "localsend") {
        return "(command -v flatpak >/dev/null 2>&1 && flatpak info org.localsend.localsend_app >/dev/null 2>&1 && flatpak run org.localsend.localsend_app) || (command -v localsend >/dev/null 2>&1 && exec localsend) || localsend";
    }
    if (raw_cmd.empty()) {
        return app_id;
    }
    return raw_cmd;
}

// ──────────────────────────────────────────────────────────────────────────────
// ICON & PROCEDURAL BACKGROUND GENERATOR
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

static void rebuild_output_bg(OutputWindow* out) {
    if (!out) return;
    int w = std::max(out->width, 1280);
    int h = std::max(out->height, 800);

    if (out->bg_surface) {
        int cur_w = cairo_image_surface_get_width(out->bg_surface);
        int cur_h = cairo_image_surface_get_height(out->bg_surface);
        if (cur_w != w || cur_h != h) {
            cairo_surface_destroy(out->bg_surface);
            out->bg_surface = nullptr;
        }
    }

    if (!out->bg_surface) {
        out->bg_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    }

    render_procedural_grain_to_surface(
        out->bg_surface,
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

static void rebuild_all_backgrounds() {
    for (auto* out : g_graph.outputs) {
        rebuild_output_bg(out);
    }
}

static void init_output_nodes(OutputWindow* out, bool preserve_positions = false) {
    if (!out) return;

    std::unordered_map<std::string, std::pair<double, double>> old_pos;
    if (preserve_positions) {
        for (const auto& n : out->nodes) {
            old_pos[n.id] = {n.x, n.y};
        }
    }

    out->nodes.clear();
    out->edges.clear();

    if (g_graph.monitor_scales.count(out->name)) {
        out->scale = g_graph.monitor_scales[out->name];
    } else {
        out->scale = g_graph.base_scale;
    }

    size_t count = g_graph.pinned_templates.size();
    if (count == 0) return;

    double cx = out->width / 2.0;
    double cy = out->height / 2.0;
    double rx = std::clamp(cx * 0.44, 250.0, 560.0) * out->scale;
    double ry = std::clamp(cy * 0.48, 170.0, 340.0) * out->scale;

    for (size_t i = 0; i < count; ++i) {
        const auto& tmpl = g_graph.pinned_templates[i];
        AppNode n;
        n.id = tmpl.id;
        n.name = tmpl.name;
        n.icon_name = tmpl.icon_name;
        n.exec_cmd = tmpl.exec_cmd;
        n.match_class = tmpl.match_class;
        n.load_to_ram = tmpl.load_to_ram;
        n.is_pinned = true;
        n.radius = g_graph.node_radius;

        if (preserve_positions && old_pos.find(n.id) != old_pos.end()) {
            n.x = old_pos[n.id].first;
            n.y = old_pos[n.id].second;
        } else {
            double angle = (2.0 * M_PI * i) / count - (M_PI / 2.0);
            n.x = cx + rx * std::cos(angle);
            n.y = cy + ry * std::sin(angle);
        }

        n.drift_phase = i * 1.17;
        n.drift_speed = 0.20 + (i % 7) * 0.02;
        n.pixbuf = get_icon_pixbuf(n.icon_name, 22);

        out->nodes.push_back(n);
    }

    auto add_edge = [&](const std::string& a, const std::string& b, double len) {
        if (a == b) return;
        bool has_a = false, has_b = false;
        for (const auto& n : out->nodes) {
            if (n.id == a) has_a = true;
            if (n.id == b) has_b = true;
        }
        if (!has_a || !has_b) return;
        for (const auto& e : out->edges) {
            if ((e.from_id == a && e.to_id == b) || (e.from_id == b && e.to_id == a)) return;
        }
        out->edges.push_back({a, b, len});
    };

    if (!g_graph.template_edges.empty()) {
        for (const auto& te : g_graph.template_edges) {
            add_edge(te.from_id, te.to_id, te.rest_length);
        }
    } else {
        if (count >= 2) {
            for (size_t i = 0; i < count; ++i) {
                add_edge(out->nodes[i].id, out->nodes[(i + 1) % count].id, 145.0);
            }
        }
        if (count >= 6) {
            for (size_t i = 0; i < count; i += 2) {
                add_edge(out->nodes[i].id, out->nodes[(i + 2) % count].id, 260.0);
            }
        }
    }
}

static void save_config() {
    std::string path = get_user_home() + "/.config/monolith/taskgraph/config.json";
    try {
        json j;
        j["pinned"] = json::array();
        for (const auto& tmpl : g_graph.pinned_templates) {
            json item;
            item["id"] = tmpl.id;
            item["name"] = tmpl.name;
            item["icon"] = tmpl.icon_name;
            item["exec"] = tmpl.exec_cmd.empty() ? tmpl.name : tmpl.exec_cmd;
            item["match_class"] = tmpl.match_class.empty() ? tmpl.name : tmpl.match_class;
            item["load_to_ram"] = tmpl.load_to_ram;
            j["pinned"].push_back(item);
        }
        j["scale"] = g_graph.base_scale;
        json ms_obj = json::object();
        for (const auto& [mon_name, mon_sc] : g_graph.monitor_scales) {
            ms_obj[mon_name] = mon_sc;
        }
        j["monitor_scales"] = ms_obj;

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

static void load_config(bool preserve_positions = false) {
    std::string path = get_user_home() + "/.config/monolith/taskgraph/config.json";
    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;

        g_graph.base_scale = j.value("scale", 1.0);
        if (j.contains("monitor_scales") && j["monitor_scales"].is_object()) {
            for (auto it = j["monitor_scales"].begin(); it != j["monitor_scales"].end(); ++it) {
                if (it.value().is_number()) {
                    g_graph.monitor_scales[it.key()] = it.value().get<double>();
                }
            }
        }

        g_graph.pinned_templates.clear();
        g_graph.template_edges.clear();

        if (j.contains("pinned") && j["pinned"].is_array()) {
            for (const auto& item : j["pinned"]) {
                PinnedTemplate tmpl;
                tmpl.id = item.value("id", "");
                tmpl.name = item.value("name", "");
                tmpl.icon_name = item.value("icon", "");
                tmpl.exec_cmd = item.value("exec", "");
                tmpl.match_class = item.value("match_class", "");
                tmpl.load_to_ram = item.value("load_to_ram", false);
                g_graph.pinned_templates.push_back(tmpl);
            }
        }

        if (j.contains("edges") && j["edges"].is_array()) {
            for (const auto& item : j["edges"]) {
                std::string from = item.value("from", "");
                std::string to = item.value("to", "");
                double len = item.value("rest_length", g_graph.rest_length);
                if (!from.empty() && !to.empty()) {
                    g_graph.template_edges.push_back({from, to, len});
                }
            }
        }

        for (auto* out : g_graph.outputs) {
            init_output_nodes(out, preserve_positions);
        }
    } catch (...) {}
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
                n_scale != g_graph.grain_scale || n_cross != g_graph.crosshair) {
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
            std::string prev_out = g_graph.display_output;
            g_graph.display_output = tg.value("display_output", g_graph.display_output);
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
            if (tg.contains("scale") && tg["scale"].is_number()) {
                g_graph.base_scale = tg.value("scale", 1.0);
            }
            if (tg.contains("monitor_scales") && tg["monitor_scales"].is_object()) {
                for (auto it = tg["monitor_scales"].begin(); it != tg["monitor_scales"].end(); ++it) {
                    if (it.value().is_number()) {
                        g_graph.monitor_scales[it.key()] = it.value().get<double>();
                    }
                }
            }

            for (auto* out : g_graph.outputs) {
                if (!out) continue;
                for (auto& n : out->nodes) {
                    n.radius = g_graph.node_radius;
                }
            }

            if (g_graph.display_output != prev_out && g_graph.app) {
                sync_output_windows(g_graph.app);
            }
        }

        if (bg_changed) {
            rebuild_all_backgrounds();
        }

        wake_all_outputs_physics(0.5);
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

    // 2. Monitor taskgraph/config.json
    GFile* f_cfg = g_file_new_for_path((base + "/taskgraph/config.json").c_str());
    GFileMonitor* m_cfg = g_file_monitor_file(f_cfg, G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (m_cfg) {
        g_signal_connect(m_cfg, "changed", G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent ev, gpointer) {
            if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_CHANGED) {
                load_config(true);
                check_desktop_state();
                wake_all_outputs_physics(0.4);
            }
        }), nullptr);
    }
    g_object_unref(f_cfg);

    // 3. Monitor /tmp/monolith_fuzzel_active
    GFile* f_fuz = g_file_new_for_path("/tmp/monolith_fuzzel_active");
    GFileMonitor* m_fuz = g_file_monitor_file(f_fuz, G_FILE_MONITOR_NONE, nullptr, nullptr);
    if (m_fuz) {
        g_signal_connect(m_fuz, "changed", G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer) {
            ensure_physics_timer_running();
            wake_all_outputs_physics(1.0);
        }), nullptr);
    }
    g_object_unref(f_fuz);
}

// ──────────────────────────────────────────────────────────────────────────────
// ZERO-FORK HYPRLAND IPC & MONITOR SYNCHRONIZATION
// ──────────────────────────────────────────────────────────────────────────────
static std::string format_ws_badge(int ws_id, const std::string& ws_name) {
    if (ws_name.find("spotify") != std::string::npos) return "S";
    if (ws_name.find("extra") != std::string::npos) return "E";
    if (ws_name.find("hidden") != std::string::npos || ws_name.find("ramvault") != std::string::npos) return "V";
    if (ws_id > 0) return "W" + std::to_string(ws_id);
    return "-";
}

static void check_desktop_state() {
    g_graph.state_check_queued.store(false);

    std::unordered_map<std::string, int> mon_active_ws;
    std::unordered_map<std::string, std::string> mon_special_ws;

    std::string mon_raw = HyprIPC::request("j/monitors");
    if (!mon_raw.empty()) {
        try {
            json mj = json::parse(mon_raw);
            if (mj.is_array()) {
                for (const auto& mon : mj) {
                    std::string mname = mon.value("name", "");
                    int mws = mon.value("activeWorkspace", json::object()).value("id", 1);
                    std::string sp = mon.value("specialWorkspace", json::object()).value("name", "");
                    mon_active_ws[mname] = mws;
                    mon_special_ws[mname] = sp;

                    if (sp == "special:ramvault") {
                        HyprIPC::dispatch("togglespecialworkspace ramvault");
                    }
                }
            }
        } catch (...) {}
    }

    std::string ws_raw = HyprIPC::request("j/activeworkspace");
    int active_ws_id = 1;
    if (!ws_raw.empty()) {
        try {
            json wj = json::parse(ws_raw);
            active_ws_id = wj.value("id", 1);
        } catch (...) {}
    }
    if (active_ws_id > 0) {
        g_graph.active_ws_id = active_ws_id;
    }

    std::string clients_raw = HyprIPC::request("j/clients");
    if (clients_raw.empty()) return;

    try {
        json clients = json::parse(clients_raw);

        std::unordered_set<std::string> active_addresses;
        std::unordered_map<std::string, json> unpinned_candidates;

        std::unordered_map<int, int> tiled_on_ws;
        std::unordered_map<int, bool> fullscreen_on_ws;

        for (const auto& c : clients) {
            std::string cls = c.value("class", "");
            std::string addr = c.value("address", "");
            bool mapped = c.value("mapped", true);
            if (!mapped || cls.empty() || cls == "monolith-constellation" || cls == "monolith-taskgraph" || cls == "monolith-toolbox" || cls == "monolith-popup") continue;

            int ws = c.value("workspace", json::object()).value("id", 1);
            std::string ws_name = c.value("workspace", json::object()).value("name", "");
            bool is_floating = c.value("floating", false);
            bool is_fullscreen = c.value("fullscreen", false);

            if (is_fullscreen) fullscreen_on_ws[ws] = true;
            if (!is_floating && ws_name.rfind("special:", 0) != 0 && ws_name.find("ramvault") == std::string::npos && ws_name.find("hidden") == std::string::npos) {
                tiled_on_ws[ws]++;
            }

            active_addresses.insert(addr);
            if (ws_name.find("ramvault") == std::string::npos && ws_name.find("hidden") == std::string::npos) {
                unpinned_candidates[addr] = c;
            }
        }

        // Synchronize state per output window independently
        for (auto* out : g_graph.outputs) {
            if (!out) continue;
            std::string mname = out->name;
            int ws = mon_active_ws.count(mname) ? mon_active_ws[mname] : active_ws_id;
            std::string sp = mon_special_ws.count(mname) ? mon_special_ws[mname] : "";

            bool this_mon_special = (!sp.empty() && sp != "null" && sp != "special:ramvault" && sp != "special:hidden");
            bool this_mon_fs = fullscreen_on_ws[ws];
            bool this_mon_tiled = (tiled_on_ws[ws] > 0);

            bool covered = (this_mon_special || this_mon_fs || this_mon_tiled);
            if (!g_graph.auto_freeze) covered = false;

            if (covered != out->is_covered) {
                out->is_covered = covered;
                if (!out->is_covered) {
                    wake_output_physics(out, 0.70);
                }
            }

            // Sync pinned nodes running state
            for (auto& n : out->nodes) {
                if (n.is_pinned) {
                    n.is_running = false;
                    n.hypr_address = "";
                    n.workspace_label = "";

                    for (const auto& c : clients) {
                        std::string cls = c.value("class", "");
                        std::string addr = c.value("address", "");
                        if (!n.match_class.empty() && (strcasecmp(cls.c_str(), n.match_class.c_str()) == 0 ||
                            case_insensitive_contains(cls, n.match_class) ||
                            case_insensitive_contains(n.match_class, cls))) {
                            n.is_running = true;
                            n.hypr_address = addr;
                            int cws = c.value("workspace", json::object()).value("id", 1);
                            std::string cws_name = c.value("workspace", json::object()).value("name", "");
                            n.workspace_id = cws;
                            n.workspace_label = format_ws_badge(cws, cws_name);
                            break;
                        }
                    }
                }
            }

            // Remove closed unpinned nodes
            size_t prev_count = out->nodes.size();
            out->nodes.erase(
                std::remove_if(out->nodes.begin(), out->nodes.end(), [&](const AppNode& n) {
                    if (n.is_pinned) return false;
                    return active_addresses.find(n.hypr_address) == active_addresses.end();
                }),
                out->nodes.end()
            );
            bool topo_changed = (out->nodes.size() != prev_count);

            // Update existing unpinned nodes
            for (auto& n : out->nodes) {
                if (!n.is_pinned) {
                    auto it = unpinned_candidates.find(n.hypr_address);
                    if (it != unpinned_candidates.end()) {
                        int cws = it->second.value("workspace", json::object()).value("id", 1);
                        std::string cws_name = it->second.value("workspace", json::object()).value("name", "");
                        n.workspace_id = cws;
                        n.workspace_label = format_ws_badge(cws, cws_name);
                    }
                }
            }

            // Clean dangling edges
            std::vector<AppEdge> clean_edges;
            for (const auto& e : out->edges) {
                bool has_from = false, has_to = false;
                for (const auto& n : out->nodes) {
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
            out->edges = std::move(clean_edges);

            if (topo_changed) {
                wake_output_physics(out, 0.65);
            } else if (!out->is_covered && out->da) {
                gtk_widget_queue_draw(out->da);
            }
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

            // Close special workspaces when switching virtual workspaces
            if (std::strstr(buf, "workspace>>")) {
                std::string mraw = HyprIPC::request("j/monitors");
                if (!mraw.empty()) {
                    try {
                        json mj = json::parse(mraw);
                        if (mj.is_array()) {
                            for (const auto& mon : mj) {
                                std::string sp = mon.value("specialWorkspace", json::object()).value("name", "");
                                if (sp.rfind("special:", 0) == 0 && sp != "special:ramvault") {
                                    std::string mon_name = mon.value("name", "");
                                    if (!mon_name.empty()) {
                                        HyprIPC::dispatch("focusmonitor " + mon_name);
                                    }
                                    HyprIPC::dispatch("togglespecialworkspace " + sp.substr(8));
                                }
                            }
                        }
                    } catch (...) {}
                }
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
    std::string target_mon = "";
    bool fuzzel_file_exists = (access("/tmp/monolith_fuzzel_active", F_OK) == 0);
    if (fuzzel_file_exists) {
        std::ifstream f("/tmp/monolith_fuzzel_active");
        if (f.is_open()) {
            f >> target_mon;
        }
        if (target_mon.empty()) target_mon = "all";
    }

    for (auto* out : g_graph.outputs) {
        if (!out) continue;
        bool should_fuz = fuzzel_file_exists && (target_mon == "all" || target_mon == out->name);
        if (should_fuz != out->fuzzel_active) {
            if (!should_fuz && out->fuzzel_active) {
                out->return_timer = 1.0;
            }
            out->fuzzel_active = should_fuz;
            wake_output_physics(out, 1.0);
        }
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
// PER-OUTPUT PHYSICS ENGINE (1:1 Native Resolution Space)
// ──────────────────────────────────────────────────────────────────────────────
static void update_physics_for_output(OutputWindow* out) {
    if (!out) return;

    if (g_popup_rep.active) {
        g_popup_rep.smooth_weight += (1.0 - g_popup_rep.smooth_weight) * 0.22;
        wake_output_physics(out, 1.0);
    } else {
        g_popup_rep.smooth_weight *= 0.84;
    }

    if (out->is_covered && !out->fuzzel_active && g_popup_rep.smooth_weight < 0.01 && out->return_timer <= 0.0) {
        return;
    }

    if (out->return_timer > 0.0) {
        out->return_timer = std::max(0.0, out->return_timer - 0.04);
    }

    bool is_returning = (out->return_timer > 0.001);

    if (out->alpha < 0.20) {
        out->alpha = 0.20;
    }
    out->physics_active = true;

    size_t n = out->nodes.size();
    if (n == 0) return;

    out->sim_time += 0.014;

    double fcx = out->width / 2.0;
    double fcy = out->height / 2.0;

    double effective_damping = out->fuzzel_active ? 0.77 : (is_returning ? 0.81 : g_graph.damping);
    double spring_mult = out->fuzzel_active ? 0.18 : (1.0 + 1.8 * out->return_timer);
    double grav_mult = out->fuzzel_active ? 0.12 : (1.0 + 3.8 * out->return_timer);

    for (size_t i = 0; i < n; ++i) {
        auto& node = out->nodes[i];
        if (node.is_dragged) continue;

        double t = out->sim_time;
        double fx = 0.18 * std::sin(node.drift_phase + t * node.drift_speed)
                  + 0.10 * std::cos(node.drift_phase * 1.73 + t * (node.drift_speed * 0.7));
        double fy = 0.18 * std::cos(node.drift_phase * 1.41 + t * (node.drift_speed * 0.85))
                  + 0.10 * std::sin(node.drift_phase * 2.19 + t * (node.drift_speed * 0.6));

        // 1. Fuzzel App Menu Smoothstep Repulsion Field (Isolated to this monitor)
        if (out->fuzzel_active) {
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
                double prx = (g_popup_rep.w / 2.0) + pad;
                double pry = (g_popup_rep.h / 2.0) + pad;
                double pen_x = std::clamp((prx - std::abs(node.x - pcx)) / prx, 0.0, 1.0);
                double pen_y = std::clamp((pry - std::abs(node.y - pcy)) / pry, 0.0, 1.0);
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
            double dx = node.x - out->nodes[j].x;
            double dy = node.y - out->nodes[j].y;
            double d = std::hypot(dx, dy) + 0.001;

            double rep_dist = 750.0 * out->scale;
            if (d < rep_dist) {
                double rep = (g_graph.repulsion * 1.8 * out->scale) / (d * d + 400.0);
                fx += (dx / d) * rep;
                fy += (dy / d) * rep;
            }

            const double personal_bubble = 180.0 * out->scale;
            if (d < personal_bubble) {
                double u = (personal_bubble - d) / personal_bubble;
                double cushion_push = u * u * (3.0 - 2.0 * u) * 26.0;
                fx += (dx / d) * cushion_push;
                fy += (dy / d) * cushion_push;
            }
        }

        // 4. Elastic Springs with Dashpot Damping
        for (const auto& e : out->edges) {
            std::string other_id;
            if (e.from_id == node.id) other_id = e.to_id;
            else if (e.to_id == node.id) other_id = e.from_id;

            if (!other_id.empty()) {
                for (const auto& other : out->nodes) {
                    if (other.id == other_id) {
                        double dx = other.x - node.x;
                        double dy = other.y - node.y;
                        double d = std::hypot(dx, dy) + 0.1;
                        double rlen = ((e.rest_length > 0) ? e.rest_length : g_graph.rest_length) * out->scale;
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

        // 5. Screen Boundary Cushion Field
        double cur_r = node.radius * out->scale;
        double pad_x = cur_r + 36.0;
        double pad_y_top = cur_r + 50.0;
        double pad_y_bot = cur_r + 38.0;

        double margin_x = 180.0 * out->scale;
        double margin_y = 130.0 * out->scale;

        double dist_left = node.x - pad_x;
        if (dist_left < margin_x) {
            double u = std::clamp((margin_x - dist_left) / margin_x, 0.0, 1.0);
            fx += u * u * 5.0;
            if (node.vx < 0.0) node.vx *= (1.0 - 0.44 * u);
        }

        double dist_right = (out->width - pad_x) - node.x;
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

        double dist_bot = (out->height - pad_y_bot) - node.y;
        if (dist_bot < margin_y) {
            double u = std::clamp((margin_y - dist_bot) / margin_y, 0.0, 1.0);
            fy -= u * u * 5.0;
            if (node.vy > 0.0) node.vy *= (1.0 - 0.44 * u);
        }

        // 6. Velocity Integration
        node.vx = (node.vx + fx * out->alpha) * effective_damping;
        node.vy = (node.vy + fy * out->alpha) * effective_damping;

        // 7. Subtle 16-second Orbital Rotation when Fuzzel is Open
        if (out->fuzzel_active) {
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

        double max_speed = out->fuzzel_active ? 6.2 : (is_returning ? (1.3 + 4.2 * out->return_timer) : 1.3);
        node.vx = std::clamp(node.vx, -max_speed, max_speed);
        node.vy = std::clamp(node.vy, -max_speed, max_speed);

        node.x += node.vx;
        node.y += node.vy;

        node.x = std::clamp(node.x, pad_x, out->width - pad_x);
        node.y = std::clamp(node.y, pad_y_top, out->height - pad_y_bot);
    }

    if (!out->fuzzel_active && !is_returning) {
        out->alpha = std::max(0.20, out->alpha * 0.96);
    }

    for (auto& node : out->nodes) {
        double target_scale = node.is_hovered ? 1.16 : 1.0;
        node.cur_scale += (target_scale - node.cur_scale) * 0.24;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// ARCHITECTURAL RENDERING ENGINE (1:1 Native Resolution Cairo Canvas)
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

static void draw_graph(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return;

    if (width != out->width || height != out->height) {
        out->width = width;
        out->height = height;
        rebuild_output_bg(out);
        init_output_nodes(out, true);
    }

    // ──────────────── 0. UNIFIED PROCEDURAL GRADIENT + GRAIN BASE ────────────
    if (out->bg_surface) {
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_surface(cr, out->bg_surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_paint(cr);
    }

    // ──────────────── 1. CONSTELLATION EDGES (NATIVE PIXEL SPACE) ────────────
    std::unordered_map<std::string, const AppNode*> id_map;
    for (const auto& n : out->nodes) id_map[n.id] = &n;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    double pulse_phase = std::fmod(out->sim_time * 0.65, 1.0);

    for (const auto& e : out->edges) {
        auto it1 = id_map.find(e.from_id);
        auto it2 = id_map.find(e.to_id);
        if (it1 == id_map.end() || it2 == id_map.end()) continue;

        const AppNode* n1 = it1->second;
        const AppNode* n2 = it2->second;

        double dx = n2->x - n1->x;
        double dy = n2->y - n1->y;
        double dist = std::hypot(dx, dy);
        double r1 = n1->radius * n1->cur_scale * out->scale + 2.0;
        double r2 = n2->radius * n2->cur_scale * out->scale + 2.0;

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
            cairo_set_line_width(cr, (g_graph.edge_width + 0.5) * out->scale);
        } else if (both_running) {
            cairo_set_source_rgba(cr, 0.85, 0.85, 0.90, std::min(0.80, g_graph.edge_alpha + 0.20));
            cairo_set_line_width(cr, (g_graph.edge_width + 0.15) * out->scale);
        } else {
            double dist_fade = std::clamp(1.0 - (dist / (680.0 * out->scale)), 0.25, 1.0);
            cairo_set_source_rgba(cr, 0.72, 0.72, 0.78, g_graph.edge_alpha * dist_fade);
            cairo_set_line_width(cr, g_graph.edge_width * out->scale);
        }

        cairo_move_to(cr, sx, sy);
        cairo_line_to(cr, ex, ey);
        cairo_stroke(cr);

        // Subtle telemetry pulse
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

    for (const auto& node : out->nodes) {
        double r = node.radius * node.cur_scale * out->scale;
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
            cairo_arc(cr, node.x, node.y, r + 4.5 * out->scale, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, node.is_hovered ? 0.60 : 0.30);
            cairo_set_line_width(cr, 1.0 * out->scale);
            cairo_stroke(cr);
        }

        // Obsidian core disc
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

        // Precision perimeter rim
        if (node.is_hovered || node.is_dragged) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 1.7 * out->scale);
        } else if (node.is_running) {
            cairo_set_source_rgba(cr, 0.92, 0.92, 0.96, 0.85);
            cairo_set_line_width(cr, 1.3 * out->scale);
        } else {
            cairo_set_source_rgba(cr, 0.32, 0.32, 0.38, 0.72);
            cairo_set_line_width(cr, 1.1 * out->scale);
        }
        cairo_stroke(cr);

        // Centered Application Icon
        if (node.pixbuf) {
            int icon_w = gdk_pixbuf_get_width(node.pixbuf);
            int icon_h = gdk_pixbuf_get_height(node.pixbuf);

            cairo_save(cr);
            cairo_translate(cr, node.x, node.y);
            double iscale = node.cur_scale * out->scale;
            if (iscale != 1.0) {
                cairo_scale(cr, iscale, iscale);
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
            cairo_arc(cr, bx, by, 5.2 * out->scale, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.03, 0.03, 0.04);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 0.13, 0.77, 0.37, 0.95);
            cairo_set_line_width(cr, 1.2 * out->scale);
            cairo_stroke(cr);

            cairo_new_sub_path(cr);
            cairo_arc(cr, bx, by, 2.5 * out->scale, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.29, 0.87, 0.50);
            cairo_fill(cr);
        }

        // Top-Left Badge: Workspace Indicator (W1..W9, S, E, V)
        if (node.is_running && !node.workspace_label.empty()) {
            cairo_set_font_size(cr, std::clamp(8.0 * out->scale, 6.5, 13.0));
            cairo_text_extents_t wext;
            cairo_text_extents(cr, node.workspace_label.c_str(), &wext);
            double bw = std::max(14.0 * out->scale, wext.width + 7.0 * out->scale);
            double bh = 12.0 * out->scale;
            double bx = node.x - r * 0.80 - bw / 2.0;
            double by = node.y - r * 0.80 - bh / 2.0;

            draw_rounded_rect(cr, bx, by, bw, bh, 3.5 * out->scale);
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
            cairo_set_line_width(cr, 1.0 * out->scale);
            cairo_stroke(cr);

            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_move_to(cr, bx + (bw - wext.width) / 2.0 - wext.x_bearing, by + (bh - wext.height) / 2.0 - wext.y_bearing);
            cairo_show_text(cr, node.workspace_label.c_str());
        }

        // Bottom Label Pill
        cairo_set_font_size(cr, std::clamp(9.5 * out->scale, 7.5, 15.0));
        cairo_text_extents_t ext;
        cairo_text_extents(cr, node.name.c_str(), &ext);
        double tx = node.x - ext.width / 2.0;
        double ty = node.y + r + 15.0 * out->scale;

        double pw = ext.width + 12.0 * out->scale;
        double ph = ext.height + 6.0 * out->scale;
        double px = node.x - pw / 2.0;
        double py = ty - ext.height - 3.0 * out->scale;

        draw_rounded_rect(cr, px, py, pw, ph, 4.0 * out->scale);
        cairo_set_source_rgba(cr, 0.02, 0.02, 0.03, 0.92);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.16, 0.16, 0.20, node.is_hovered ? 0.95 : 0.65);
        cairo_set_line_width(cr, 1.0 * out->scale);
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

    // ──────────────── 3. CONTEXT MENU (LOCALIZED TO THIS OUTPUT) ─────────────
    if (out->menu_open && out->menu_node_idx >= 0 && out->menu_node_idx < static_cast<int>(out->nodes.size())) {
        const auto& node = out->nodes[out->menu_node_idx];

        int num_items = node.is_running ? 5 : 4;
        double item_h = 30.0;
        double menu_h = 44.0 + num_items * (item_h + 2.0) + 6.0;
        out->menu_h = menu_h;

        double mx = std::clamp(out->menu_x, 15.0, out->width - out->menu_w - 15.0);
        double my = std::clamp(out->menu_y, 42.0, out->height - menu_h - 15.0);

        draw_rounded_rect(cr, mx, my, out->menu_w, menu_h, 8.0);
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
        cairo_line_to(cr, mx + out->menu_w - 10.0, my + 34.0);
        cairo_stroke(cr);

        double start_y = my + 40.0;
        cairo_set_font_size(cr, 10.5);

        auto draw_menu_row = [&](int idx, const std::string& label, double r_col, double g_col, double b_col) {
            double ry = start_y + idx * (item_h + 2.0);
            if (out->menu_hovered_item == idx) {
                draw_rounded_rect(cr, mx + 6.0, ry, out->menu_w - 12.0, item_h, 5.0);
                cairo_set_source_rgba(cr, 0.16, 0.16, 0.20, 0.95);
                cairo_fill(cr);
            }
            cairo_set_source_rgb(cr, r_col, g_col, b_col);
            cairo_move_to(cr, mx + 14.0, ry + 19.5);
            cairo_show_text(cr, label.c_str());
        };

        std::string item0_lbl = (node.workspace_label == "V") ? "[!] Instant Launch (RAM Vault)" : (node.is_running ? "[>] Focus Window" : "[>] Launch Application");
        draw_menu_row(0, item0_lbl, 1.0, 1.0, 1.0);

        if (node.is_running) {
            bool in_extra = (node.workspace_label == "E");
            draw_menu_row(1, in_extra ? "[^] Restore from Extra Shelf" : "[v] Stash to Extra Shelf", 0.96, 0.72, 0.25);
        } else {
            draw_menu_row(1, "[+] Launch Fresh Instance", 0.82, 0.82, 0.86);
        }

        draw_menu_row(2, node.is_pinned ? "[-] Unpin from Constellation" : "[+] Pin to Constellation", 0.88, 0.88, 0.92);

        if (node.load_to_ram) {
            draw_menu_row(3, "[*] RAM Cache: [ RESIDENT ]", 0.29, 0.87, 0.50);
        } else {
            draw_menu_row(3, "[ ] Load to RAM: [ DISABLED ]", 0.65, 0.65, 0.70);
        }

        if (node.is_running) {
            draw_menu_row(4, "[x] Close Running Window", 0.97, 0.44, 0.44);
        }
    } else if (out->menu_open && out->menu_node_idx == -2) {
        int num_items = 4;
        double item_h = 30.0;
        double menu_h = 44.0 + num_items * (item_h + 2.0) + 6.0;
        out->menu_h = menu_h;
        out->menu_w = 296.0;

        double mx = std::clamp(out->menu_x, 15.0, out->width - out->menu_w - 15.0);
        double my = std::clamp(out->menu_y, 42.0, out->height - menu_h - 15.0);

        draw_rounded_rect(cr, mx, my, out->menu_w, menu_h, 8.0);
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
        std::string header = out->name + " CONSTELLATION [" + std::to_string(static_cast<int>(std::round(out->scale * 100))) + "%]";
        cairo_show_text(cr, header.c_str());

        cairo_set_source_rgba(cr, 0.18, 0.18, 0.22, 0.90);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, mx + 10.0, my + 34.0);
        cairo_line_to(cr, mx + out->menu_w - 10.0, my + 34.0);
        cairo_stroke(cr);

        double start_y = my + 40.0;
        cairo_set_font_size(cr, 10.5);

        auto draw_menu_row = [&](int idx, const std::string& label, double r_col, double g_col, double b_col) {
            double ry = start_y + idx * (item_h + 2.0);
            if (out->menu_hovered_item == idx) {
                draw_rounded_rect(cr, mx + 6.0, ry, out->menu_w - 12.0, item_h, 5.0);
                cairo_set_source_rgba(cr, 0.16, 0.16, 0.20, 0.95);
                cairo_fill(cr);
            }
            cairo_set_source_rgb(cr, r_col, g_col, b_col);
            cairo_move_to(cr, mx + 14.0, ry + 19.5);
            cairo_show_text(cr, label.c_str());
        };

        draw_menu_row(0, "[+] Enlarge Constellation (+10%)", 0.92, 0.92, 0.95);
        draw_menu_row(1, "[-] Shrink Constellation (-10%)", 0.92, 0.92, 0.95);
        draw_menu_row(2, "[0] Reset Constellation (100%)", 0.82, 0.82, 0.86);
        draw_menu_row(3, "[*] Recenter Orbit Layout", 0.70, 0.85, 1.0);
    }

    // ──────────────── 4. TRANSIENT SCALE OSD HUD ─────────────────────────────
    if (out->scale_osd_alpha > 0.01) {
        cairo_save(cr);
        double a = std::min(1.0, out->scale_osd_alpha);
        std::string osd = out->scale_osd_text;
        cairo_select_font_face(cr, "JetBrains Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.5);
        cairo_text_extents_t te;
        cairo_text_extents(cr, osd.c_str(), &te);
        double bw = te.width + 36.0;
        double bh = 34.0;
        double bx = (out->width - bw) / 2.0;
        double by = 48.0;

        draw_rounded_rect(cr, bx, by, bw, bh, 6.0);
        cairo_set_source_rgba(cr, 0.04, 0.04, 0.06, 0.94 * a);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.35, 0.35, 0.42, 0.88 * a);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, 0.95, 0.95, 0.98, a);
        cairo_move_to(cr, bx + 18.0, by + 21.5);
        cairo_show_text(cr, osd.c_str());
        cairo_restore(cr);

        out->scale_osd_alpha = std::max(0.0, out->scale_osd_alpha - 0.025);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// MOUSE INTERACTION & HIT TESTING
// ──────────────────────────────────────────────────────────────────────────────
static int hit_test_node(OutputWindow* out, double x, double y) {
    if (!out) return -1;
    for (int i = static_cast<int>(out->nodes.size()) - 1; i >= 0; --i) {
        const auto& n = out->nodes[i];
        double dx = x - n.x;
        double dy = y - n.y;
        double hit_r = n.radius * n.cur_scale * out->scale + 6.0;
        if (dx * dx + dy * dy <= hit_r * hit_r) {
            return i;
        }
    }
    return -1;
}

static void focus_or_launch_node(OutputWindow* out, const AppNode& node) {
    if (node.is_running && !node.hypr_address.empty()) {
        if (node.workspace_label == "V" || node.workspace_label == "E") {
            int target_ws_id = g_graph.active_ws_id;
            std::string mon_raw = HyprIPC::request("j/monitors");
            if (!mon_raw.empty() && out) {
                try {
                    json mj = json::parse(mon_raw);
                    if (mj.is_array()) {
                        for (const auto& mon : mj) {
                            if (mon.value("name", "") == out->name) {
                                target_ws_id = mon.value("activeWorkspace", json::object()).value("id", target_ws_id);
                                break;
                            }
                        }
                    }
                } catch (...) {}
            }
            std::string target_ws = std::to_string(std::max(1, target_ws_id));
            std::vector<std::string> cmds;
            if (out && !out->name.empty()) {
                cmds.push_back("dispatch focusmonitor " + out->name);
            }
            cmds.push_back("dispatch movetoworkspace " + target_ws + ",address:" + node.hypr_address);
            cmds.push_back("dispatch focuswindow address:" + node.hypr_address);
            HyprIPC::batch(cmds);
            check_desktop_state();
        } else if (node.workspace_label == "S") {
            HyprIPC::dispatch("togglespecialworkspace spotify");
        } else {
            HyprIPC::dispatch("focuswindow address:" + node.hypr_address);
        }
    } else if (!node.exec_cmd.empty() || !node.id.empty()) {
        std::string launch_cmd = resolve_app_command(node.id, node.exec_cmd);
        exec_bg("monolith-ram-preloader boost 3.5");
        if (out && !out->name.empty()) {
            HyprIPC::dispatch("focusmonitor " + out->name);
        }
        HyprIPC::dispatch("exec sh -c \"" + launch_cmd + "\"");
    }
}

static void on_drag_begin(GtkGestureDrag*, double start_x, double start_y, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return;
    out->is_covered = false;
    ensure_physics_timer_running();

    out->dragged_node_idx = -1;

    if (out->menu_open && out->menu_node_idx >= 0 && out->menu_node_idx < static_cast<int>(out->nodes.size())) {
        double mx = std::clamp(out->menu_x, 15.0, out->width - out->menu_w - 15.0);
        double my = std::clamp(out->menu_y, 42.0, out->height - out->menu_h - 15.0);

        if (start_x >= mx && start_x <= mx + out->menu_w && start_y >= my + 40.0 && start_y <= my + out->menu_h) {
            int clicked_item = static_cast<int>((start_y - (my + 40.0)) / 32.0);
            auto& target_node = out->nodes[out->menu_node_idx];

            if (clicked_item == 0) {
                focus_or_launch_node(out, target_node);
            } else if (clicked_item == 1) {
                if (target_node.is_running && !target_node.hypr_address.empty()) {
                    if (target_node.workspace_label == "E") {
                        std::string target_ws = std::to_string(std::max(1, g_graph.active_ws_id));
                        HyprIPC::dispatch("movetoworkspace " + target_ws + ",address:" + target_node.hypr_address);
                    } else {
                        HyprIPC::dispatch("movetoworkspacesilent special:extra,address:" + target_node.hypr_address);
                    }
                    check_desktop_state();
                } else if (!target_node.exec_cmd.empty() || !target_node.id.empty()) {
                    std::string launch_cmd = resolve_app_command(target_node.id, target_node.exec_cmd);
                    HyprIPC::dispatch("exec sh -c \"" + launch_cmd + "\"");
                }
            } else if (clicked_item == 2) {
                target_node.is_pinned = !target_node.is_pinned;
                if (!target_node.is_pinned) {
                    target_node.load_to_ram = false;
                }
                bool found = false;
                for (auto& tmpl : g_graph.pinned_templates) {
                    if (tmpl.id == target_node.id) {
                        tmpl.load_to_ram = target_node.load_to_ram;
                        found = true;
                        break;
                    }
                }
                if (!found && target_node.is_pinned) {
                    g_graph.pinned_templates.push_back({
                        target_node.id, target_node.name, target_node.icon_name,
                        target_node.exec_cmd, target_node.match_class, target_node.load_to_ram
                    });
                } else if (found && !target_node.is_pinned) {
                    g_graph.pinned_templates.erase(
                        std::remove_if(g_graph.pinned_templates.begin(), g_graph.pinned_templates.end(),
                            [&](const PinnedTemplate& t){ return t.id == target_node.id; }),
                        g_graph.pinned_templates.end()
                    );
                }
                save_config();
                for (auto* o : g_graph.outputs) {
                    if (!o) continue;
                    for (auto& n : o->nodes) {
                        if (n.id == target_node.id) {
                            n.is_pinned = target_node.is_pinned;
                            n.load_to_ram = target_node.load_to_ram;
                        }
                    }
                    if (o->da) gtk_widget_queue_draw(o->da);
                }
            } else if (clicked_item == 3) {
                target_node.load_to_ram = !target_node.load_to_ram;
                if (target_node.load_to_ram) {
                    target_node.is_pinned = true;
                }
                bool found = false;
                for (auto& tmpl : g_graph.pinned_templates) {
                    if (tmpl.id == target_node.id) {
                        tmpl.load_to_ram = target_node.load_to_ram;
                        found = true;
                        break;
                    }
                }
                if (!found && target_node.load_to_ram) {
                    g_graph.pinned_templates.push_back({
                        target_node.id, target_node.name, target_node.icon_name,
                        target_node.exec_cmd, target_node.match_class, target_node.load_to_ram
                    });
                }
                save_config();
                for (auto* o : g_graph.outputs) {
                    if (!o) continue;
                    for (auto& n : o->nodes) {
                        if (n.id == target_node.id) {
                            n.is_pinned = target_node.is_pinned;
                            n.load_to_ram = target_node.load_to_ram;
                        }
                    }
                    if (o->da) gtk_widget_queue_draw(o->da);
                }
                std::string action = target_node.load_to_ram ? "load" : "unload";
                exec_bg("monolith-ram-preloader " + action + " \"" + target_node.id + "\"");
            } else if (clicked_item == 4 && target_node.is_running && !target_node.hypr_address.empty()) {
                HyprIPC::dispatch("closewindow address:" + target_node.hypr_address);
            }

            out->menu_open = false;
            out->menu_node_idx = -1;
            if (out->da) gtk_widget_queue_draw(out->da);
            return;
        } else {
            out->menu_open = false;
            out->menu_node_idx = -1;
            if (out->da) gtk_widget_queue_draw(out->da);
            return;
        }
    }

    if (out->menu_open && out->menu_node_idx == -2) {
        double mx = std::clamp(out->menu_x, 15.0, out->width - out->menu_w - 15.0);
        double my = std::clamp(out->menu_y, 42.0, out->height - out->menu_h - 15.0);

        if (start_x >= mx && start_x <= mx + out->menu_w && start_y >= my + 40.0 && start_y <= my + out->menu_h) {
            int clicked_item = static_cast<int>((start_y - (my + 40.0)) / 32.0);
            if (clicked_item == 0) {
                out->scale = std::clamp(out->scale + 0.10, 0.40, 2.50);
            } else if (clicked_item == 1) {
                out->scale = std::clamp(out->scale - 0.10, 0.40, 2.50);
            } else if (clicked_item == 2) {
                out->scale = 1.0;
            } else if (clicked_item == 3) {
                double cx = out->width / 2.0;
                double cy = out->height / 2.0;
                size_t count = out->nodes.size();
                double rx = std::clamp(cx * 0.44, 250.0, 560.0) * out->scale;
                double ry = std::clamp(cy * 0.48, 170.0, 340.0) * out->scale;
                for (size_t i = 0; i < count; ++i) {
                    double angle = (2.0 * M_PI * i) / std::max(size_t(1), count) - (M_PI / 2.0);
                    out->nodes[i].x = cx + rx * std::cos(angle);
                    out->nodes[i].y = cy + ry * std::sin(angle);
                    out->nodes[i].vx = 0.0;
                    out->nodes[i].vy = 0.0;
                }
            }

            g_graph.monitor_scales[out->name] = out->scale;
            out->scale_osd_alpha = 1.8;
            out->scale_osd_text = out->name + " SCALE: " + std::to_string(static_cast<int>(std::round(out->scale * 100))) + "%";
            save_config();
            wake_output_physics(out, 0.7);
            out->menu_open = false;
            out->menu_node_idx = -1;
            if (out->da) gtk_widget_queue_draw(out->da);
            return;
        } else {
            out->menu_open = false;
            out->menu_node_idx = -1;
            if (out->da) gtk_widget_queue_draw(out->da);
            return;
        }
    }

    int idx = hit_test_node(out, start_x, start_y);
    out->dragged_node_idx = idx;
    out->drag_start_x = start_x;
    out->drag_start_y = start_y;
    out->drag_dist = 0.0;

    if (idx >= 0) {
        out->nodes[idx].is_dragged = true;
        out->nodes[idx].vx = 0.0;
        out->nodes[idx].vy = 0.0;
        wake_output_physics(out, 0.6);
    }
}

static void on_drag_update(GtkGestureDrag*, double offset_x, double offset_y, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return;

    if (out->dragged_node_idx >= 0 && out->dragged_node_idx < static_cast<int>(out->nodes.size())) {
        auto& node = out->nodes[out->dragged_node_idx];
        node.x = out->drag_start_x + offset_x;
        node.y = out->drag_start_y + offset_y;
        out->drag_dist = std::hypot(offset_x, offset_y);
        wake_output_physics(out, 0.45);
    }
}

static void on_drag_end(GtkGestureDrag*, double, double, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return;

    if (out->dragged_node_idx >= 0 && out->dragged_node_idx < static_cast<int>(out->nodes.size())) {
        auto& node = out->nodes[out->dragged_node_idx];
        node.is_dragged = false;

        if (out->drag_dist < 6.0) {
            focus_or_launch_node(out, node);
        }
    }

    out->dragged_node_idx = -1;
    wake_output_physics(out, 0.5);
}

static void on_right_click(GtkGestureClick*, int, double x, double y, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return;
    out->is_covered = false;
    ensure_physics_timer_running();

    int idx = hit_test_node(out, x, y);
    if (idx >= 0) {
        out->menu_open = true;
        out->menu_node_idx = idx;
        out->menu_x = x + 12.0;
        out->menu_y = y + 12.0;
        out->menu_hovered_item = -1;
        if (out->da) gtk_widget_queue_draw(out->da);
    } else {
        out->menu_open = true;
        out->menu_node_idx = -2;
        out->menu_x = x + 12.0;
        out->menu_y = y + 12.0;
        out->menu_hovered_item = -1;
        if (out->da) gtk_widget_queue_draw(out->da);
    }
}

static gboolean on_scroll(GtkEventControllerScroll* ctrl, double, double dy, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return GDK_EVENT_PROPAGATE;

    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    bool has_ctrl = (state & GDK_CONTROL_MASK) != 0;
    bool has_super = (state & GDK_SUPER_MASK) != 0;

    if (has_ctrl || has_super) {
        double delta = (dy < 0) ? 0.05 : -0.05;
        out->scale = std::clamp(out->scale + delta, 0.40, 2.50);
        out->scale_osd_alpha = 1.8;
        out->scale_osd_text = out->name + " SCALE: " + std::to_string(static_cast<int>(std::round(out->scale * 100))) + "%";
        g_graph.monitor_scales[out->name] = out->scale;
        save_config();
        wake_output_physics(out, 0.5);
        if (out->da) gtk_widget_queue_draw(out->da);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void on_middle_click(GtkGestureClick*, int, double x, double y, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return;
    out->is_covered = false;
    ensure_physics_timer_running();

    int idx = hit_test_node(out, x, y);
    if (idx >= 0 && out->nodes[idx].is_running && !out->nodes[idx].hypr_address.empty()) {
        auto& n = out->nodes[idx];
        if (n.workspace_label == "E") {
            std::string target_ws = std::to_string(std::max(1, g_graph.active_ws_id));
            HyprIPC::dispatch("movetoworkspace " + target_ws + ",address:" + n.hypr_address);
        } else {
            HyprIPC::dispatch("movetoworkspacesilent special:extra,address:" + n.hypr_address);
        }
        check_desktop_state();
    }
}

static void on_motion(GtkEventControllerMotion*, double x, double y, gpointer user_data) {
    OutputWindow* out = static_cast<OutputWindow*>(user_data);
    if (!out) return;
    out->is_covered = false;
    ensure_physics_timer_running();

    if (out->menu_open) {
        double mx = std::clamp(out->menu_x, 15.0, out->width - out->menu_w - 15.0);
        double my = std::clamp(out->menu_y, 42.0, out->height - out->menu_h - 15.0);

        if (x >= mx && x <= mx + out->menu_w && y >= my + 40.0 && y <= my + out->menu_h) {
            int item = static_cast<int>((y - (my + 40.0)) / 32.0);
            if (item != out->menu_hovered_item) {
                out->menu_hovered_item = item;
                if (out->da) gtk_widget_queue_draw(out->da);
            }
            return;
        } else if (out->menu_hovered_item != -1) {
            out->menu_hovered_item = -1;
            if (out->da) gtk_widget_queue_draw(out->da);
        }
    }

    int hovered_idx = hit_test_node(out, x, y);
    bool changed = false;
    for (size_t i = 0; i < out->nodes.size(); ++i) {
        bool h = (static_cast<int>(i) == hovered_idx);
        if (out->nodes[i].is_hovered != h) {
            out->nodes[i].is_hovered = h;
            changed = true;
        }
    }

    if (changed) {
        wake_output_physics(out, 0.35);
    }

    if (out->da) {
        GtkNative* native = gtk_widget_get_native(out->da);
        if (native) {
            GdkSurface* surf = gtk_native_get_surface(native);
            if (surf) {
                GdkCursor* cur = gdk_cursor_new_from_name((hovered_idx >= 0 || out->menu_open) ? "pointer" : "default", nullptr);
                gdk_surface_set_cursor(surf, cur);
                g_object_unref(cur);
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// APPLICATION ACTIVATION & MULTI-MONITOR LAYER SHELL
// ──────────────────────────────────────────────────────────────────────────────
static void sync_output_windows(GApplication* app) {
    if (!app) return;
    GdkDisplay* display = gdk_display_get_default();
    if (!display) return;
    GListModel* monitors = gdk_display_get_monitors(display);
    if (!monitors) return;
    guint n_monitors = g_list_model_get_n_items(monitors);
    if (n_monitors == 0) return;

    std::string mode = g_graph.display_output;
    if (mode.empty()) mode = "all";

    std::vector<OutputWindow*> retained;

    for (guint i = 0; i < n_monitors; ++i) {
        GdkMonitor* mon = GDK_MONITOR(g_list_model_get_item(monitors, i));
        if (!mon) continue;
        const char* conn = gdk_monitor_get_connector(mon);
        std::string conn_name = conn ? conn : ("Output-" + std::to_string(i));

        bool want = false;
        if (mode == "all") {
            want = true;
        } else if (mode == "primary") {
            want = (i == 0);
        } else if (mode == conn_name) {
            want = true;
        }

        OutputWindow* existing = nullptr;
        for (auto* out : g_graph.outputs) {
            if (out && (out->monitor == mon || out->name == conn_name)) {
                existing = out;
                break;
            }
        }

        if (want) {
            if (existing) {
                retained.push_back(existing);
            } else {
                OutputWindow* out = new OutputWindow();
                out->monitor = mon;
                g_object_ref(mon);
                out->name = conn_name;

                GdkRectangle geom;
                gdk_monitor_get_geometry(mon, &geom);
                out->width = geom.width > 0 ? geom.width : 1920;
                out->height = geom.height > 0 ? geom.height : 1080;

                out->win = gtk_window_new();
                gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(out->win));

                gtk_layer_init_for_window(GTK_WINDOW(out->win));
                gtk_layer_set_monitor(GTK_WINDOW(out->win), mon);
                gtk_layer_set_layer(GTK_WINDOW(out->win), GTK_LAYER_SHELL_LAYER_BOTTOM);
                gtk_layer_set_exclusive_zone(GTK_WINDOW(out->win), -1);
                gtk_layer_set_keyboard_mode(GTK_WINDOW(out->win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
                gtk_layer_set_namespace(GTK_WINDOW(out->win), "monolith-constellation");

                gtk_layer_set_anchor(GTK_WINDOW(out->win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
                gtk_layer_set_anchor(GTK_WINDOW(out->win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
                gtk_layer_set_anchor(GTK_WINDOW(out->win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
                gtk_layer_set_anchor(GTK_WINDOW(out->win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

                gtk_widget_add_css_class(out->win, "taskgraph-window");

                out->da = gtk_drawing_area_new();
                gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(out->da), draw_graph, out, nullptr);
                gtk_window_set_child(GTK_WINDOW(out->win), out->da);

                GtkGesture* drag = gtk_gesture_drag_new();
                g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), out);
                g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), out);
                g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), out);
                gtk_widget_add_controller(out->da, GTK_EVENT_CONTROLLER(drag));

                GtkGesture* right_click = gtk_gesture_click_new();
                gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
                g_signal_connect(right_click, "pressed", G_CALLBACK(on_right_click), out);
                gtk_widget_add_controller(out->da, GTK_EVENT_CONTROLLER(right_click));

                GtkGesture* middle_click = gtk_gesture_click_new();
                gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle_click), GDK_BUTTON_MIDDLE);
                g_signal_connect(middle_click, "pressed", G_CALLBACK(on_middle_click), out);
                gtk_widget_add_controller(out->da, GTK_EVENT_CONTROLLER(middle_click));

                GtkEventController* motion = gtk_event_controller_motion_new();
                g_signal_connect(motion, "motion", G_CALLBACK(on_motion), out);
                gtk_widget_add_controller(out->da, motion);

                GtkEventController* scroll = gtk_event_controller_scroll_new(
                    static_cast<GtkEventControllerScrollFlags>(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE)
                );
                g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), out);
                gtk_widget_add_controller(out->da, scroll);

                rebuild_output_bg(out);
                init_output_nodes(out, false);
                gtk_window_present(GTK_WINDOW(out->win));

                retained.push_back(out);
            }
        } else {
            if (existing) {
                gtk_window_destroy(GTK_WINDOW(existing->win));
                if (existing->bg_surface) cairo_surface_destroy(existing->bg_surface);
                if (existing->monitor) g_object_unref(existing->monitor);
                delete existing;
            }
        }
        g_object_unref(mon);
    }

    g_graph.outputs = retained;
    check_desktop_state();
    wake_all_outputs_physics(0.5);
}

static gboolean on_physics_tick(gpointer) {
    static uint32_t tick_count = 0;
    ++tick_count;

    if ((tick_count & 3) == 0) {
        check_fuzzel_state();
        check_popup_state();
    }

    bool any_active = false;
    for (auto* out : g_graph.outputs) {
        if (!out) continue;
        bool out_active = (!out->is_covered) || out->fuzzel_active || (out->return_timer > 0.0) || (out->dragged_node_idx >= 0) || (out->scale_osd_alpha > 0.01);
        if (out_active) {
            any_active = true;
            update_physics_for_output(out);
            if (out->da) gtk_widget_queue_draw(out->da);
        }
    }

    // TRUE ZERO-CPU SLEEP: Stop timer completely when all active displays are covered
    if (!any_active && g_graph.auto_freeze) {
        g_graph.physics_timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void on_activate(GApplication* app, gpointer) {
    g_graph.app = app;

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

    sync_output_windows(app);

    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GListModel* monitors = gdk_display_get_monitors(display);
        if (monitors) {
            g_signal_connect(monitors, "items-changed", G_CALLBACK(+[](GListModel*, guint, guint, guint, gpointer data) {
                sync_output_windows(G_APPLICATION(data));
            }), app);
        }
    }

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
