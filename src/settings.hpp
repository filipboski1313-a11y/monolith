#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "grain_generator.hpp"

namespace Monolith {

inline std::string get_home_dir() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') return std::string(home);
    const char* user = std::getenv("USER");
    if (user && user[0] != '\0') return "/home/" + std::string(user);
    return "";
}

inline std::string config_dir() {
    return get_home_dir() + "/.config/monolith";
}

inline std::string run_cmd(const std::string& cmd) {
    char buffer[256];
    std::string result = "";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

inline void hypr_keyword(const std::string& key, const std::string& val) {
    std::string cmd = "hyprctl keyword " + key + " \"" + val + "\" > /dev/null 2>&1";
    system(cmd.c_str());
}

inline void hypr_keyword(const std::string& key, double val) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    hypr_keyword(key, ss.str());
}

inline void hypr_keyword(const std::string& key, int val) {
    hypr_keyword(key, std::to_string(val));
}

struct Settings {
    // Animations & Bézier Curves
    struct Animations {
        bool enabled = true;
        std::string preset = "Micro Snappy";
        double p1x = 0.05;
        double p1y = 0.90;
        double p2x = 0.18;
        double p2y = 1.00;
        double windows_in = 1.4;
        double windows_out = 1.2;
        double windows_move = 1.4;
        double workspaces = 1.4;
        double fade = 1.2;
        std::string workspace_style = "slide";
        std::string window_style = "popin 90%";
    } anim;

    // Window Geometry & Aesthetic Borders
    struct Geometry {
        int gaps_in = 0;
        int gaps_out = 0;
        int border_size = 1;
        int rounding = 4;
        std::string active_border_1 = "#ffffff";
        std::string active_border_2 = "#333333";
        int border_angle = 45;
        std::string inactive_border = "#141414";
    } geom;

    // Window Opacity & GPU Shaders
    struct Opacity {
        double active_opacity = 1.0;
        double inactive_opacity = 1.0;
        double fullscreen_opacity = 1.0;
        bool blur_enabled = false;
        int blur_size = 5;
        int blur_passes = 2;
        bool shadow_enabled = false;
        int shadow_range = 20;
    } opac;

    // Procedural Mathematical Film Grain & Gradients
    struct Grain {
        std::string gradient_type = "none"; // "none", "vertical", "horizontal", "diagonal_down", "diagonal_up", "mesh_3point"
        std::string color1 = "#000000";      // Base / Bottom
        std::string color2 = "#18181b";      // Top / Left / Point 2
        std::string color3 = "#27272a";      // Point 3 (top-right lower)
        std::string preset = "OLED Monolith";
        double intensity = 14.0;
        std::string algorithm = "gaussian";  // "gaussian", "harmonic", "quantum", "dither"
        std::string tint = "#d4d4d8";
        int scale = 1;
        bool crosshair = false;
    } grain;

    // Desktop Task Graph Physics Engine
    struct TaskGraph {
        double spring_k = 0.0016;
        double rest_length = 320.0;
        double repulsion = 1200.0;
        double center_gravity = 0.012;
        double damping = 0.88;
        double drift_speed = 0.20;
        double edge_width = 1.0;
        double edge_alpha = 0.45;
        double node_radius = 16.0;
        double repulsion_radius = 620.0;
        double fuzzel_repulsion = 450.0;
        bool auto_freeze = true;
    } taskgraph;

    // Display & Monitor Output
    struct Display {
        std::string monitor_name = "eDP-1";
        std::string mode = "1920x1200@60.00Hz";
        double scale = 1.28;
        double text_scale = 1.00;
        int cursor_size = 24;
        int vrr = 0; // 0=off, 1=on, 2=fullscreen only
    } disp;

    // Input & Touchpad Configuration
    struct Input {
        double sensitivity = 0.0;
        bool natural_scroll = true;
        bool tap_to_click = true;
        double scroll_factor = 1.0;
        bool disable_while_typing = true;
    } inp;

    // Night Light (Hyprsunset)
    struct Nightlight {
        bool enabled = false;
        int temp = 4200;
    } night;

    // Minimalist Status Bar (Waybar)
    struct Waybar {
        std::string position = "top";
        int height = 29;
        int font_size = 11;
        bool drawer_enabled = true; // true = hover auto-hide, false = always expanded
        std::string bg_color = "#000000";
        std::string border_color = "#141414";
        std::string text_color = "#e4e4e7";
        std::string active_ws_bg = "#18181b";
        std::string active_ws_text = "#ffffff";
        std::string inactive_ws_text = "#52525b";
        std::string drawer_btn_bg = "#111111";
        std::string drawer_btn_border = "#222222";
        bool show_bluetooth = true;
        bool show_idle_inhibitor = true;
        bool show_cpu = true;
        bool show_memory = true;
        bool show_network = true;
        bool show_audio = true;
        bool show_backlight = true;
        bool show_battery = true;
        bool show_nightlight = true;
        bool show_notifications = true;
        bool show_power = true;
    } waybar;

    // Fuzzel Application Launcher
    struct Fuzzel {
        int lines = 10;
        int width = 46;
        int border_radius = 4;
        int horizontal_pad = 18;
        int vertical_pad = 12;
        std::string bg_color = "#000000fa";
        std::string border_color = "#27272aff";
        std::string text_color = "#e4e4e7ff";
        std::string match_color = "#ffffffff";
        std::string selection_bg = "#18181bff";
        std::string selection_text = "#fffffffa";
    } fuzzel;

    // System Monitor detection cache
    std::string monitor_description = "Built-in Display";

    void detect_monitor() {
        std::string out = run_cmd("hyprctl monitors -j 2>/dev/null");
        if (out.empty()) return;
        try {
            auto j = nlohmann::json::parse(out);
            if (j.is_array() && !j.empty()) {
                auto& m = j[0];
                if (m.contains("name")) disp.monitor_name = m["name"].get<std::string>();
                if (m.contains("scale")) disp.scale = m["scale"].get<double>();
                if (m.contains("vrr")) disp.vrr = m["vrr"].get<bool>() ? 1 : 0;
                if (m.contains("description")) monitor_description = m["description"].get<std::string>();

                if (m.contains("width") && m.contains("height") && m.contains("refreshRate")) {
                    int w = m["width"].get<int>();
                    int h = m["height"].get<int>();
                    double rr = m["refreshRate"].get<double>();
                    std::ostringstream ss;
                    ss << w << "x" << h << "@" << std::fixed << std::setprecision(2) << rr << "Hz";
                    disp.mode = ss.str();
                }
            }
        } catch (...) {}
    }

    void load() {
        std::string path = config_dir() + "/settings.json";
        std::ifstream f(path);
        if (!f.is_open()) return;

        try {
            nlohmann::json j;
            f >> j;

            if (j.contains("animations")) {
                auto& a = j["animations"];
                if (a.contains("enabled")) anim.enabled = a["enabled"].get<bool>();
                if (a.contains("preset")) anim.preset = a["preset"].get<std::string>();
                if (a.contains("p1x")) anim.p1x = a["p1x"].get<double>();
                if (a.contains("p1y")) anim.p1y = a["p1y"].get<double>();
                if (a.contains("p2x")) anim.p2x = a["p2x"].get<double>();
                if (a.contains("p2y")) anim.p2y = a["p2y"].get<double>();
                if (a.contains("windows_in")) anim.windows_in = a["windows_in"].get<double>();
                if (a.contains("windows_out")) anim.windows_out = a["windows_out"].get<double>();
                if (a.contains("windows_move")) anim.windows_move = a["windows_move"].get<double>();
                if (a.contains("workspaces")) anim.workspaces = a["workspaces"].get<double>();
                if (a.contains("fade")) anim.fade = a["fade"].get<double>();
                if (a.contains("workspace_style")) anim.workspace_style = a["workspace_style"].get<std::string>();
                if (a.contains("window_style")) anim.window_style = a["window_style"].get<std::string>();
            }

            if (j.contains("geometry")) {
                auto& g = j["geometry"];
                if (g.contains("gaps_in")) geom.gaps_in = g["gaps_in"].get<int>();
                if (g.contains("gaps_out")) geom.gaps_out = g["gaps_out"].get<int>();
                if (g.contains("border_size")) geom.border_size = g["border_size"].get<int>();
                if (g.contains("rounding")) geom.rounding = g["rounding"].get<int>();
                if (g.contains("active_border_1")) geom.active_border_1 = g["active_border_1"].get<std::string>();
                if (g.contains("active_border_2")) geom.active_border_2 = g["active_border_2"].get<std::string>();
                if (g.contains("inactive_border")) geom.inactive_border = g["inactive_border"].get<std::string>();
                if (g.contains("border_angle")) geom.border_angle = g["border_angle"].get<int>();
            }

            if (j.contains("opacity")) {
                auto& o = j["opacity"];
                if (o.contains("active_opacity")) opac.active_opacity = o["active_opacity"].get<double>();
                if (o.contains("inactive_opacity")) opac.inactive_opacity = o["inactive_opacity"].get<double>();
                if (o.contains("fullscreen_opacity")) opac.fullscreen_opacity = o["fullscreen_opacity"].get<double>();
                if (o.contains("blur_enabled")) opac.blur_enabled = o["blur_enabled"].get<bool>();
                if (o.contains("blur_size")) opac.blur_size = o["blur_size"].get<int>();
                if (o.contains("blur_passes")) opac.blur_passes = o["blur_passes"].get<int>();
                if (o.contains("shadow_enabled")) opac.shadow_enabled = o["shadow_enabled"].get<bool>();
                if (o.contains("shadow_range")) opac.shadow_range = o["shadow_range"].get<int>();
            }

            if (j.contains("grain")) {
                auto& gr = j["grain"];
                if (gr.contains("gradient_type")) grain.gradient_type = gr["gradient_type"].get<std::string>();
                if (gr.contains("color1")) grain.color1 = gr["color1"].get<std::string>();
                else if (gr.contains("base_color")) grain.color1 = gr["base_color"].get<std::string>();
                if (gr.contains("color2")) grain.color2 = gr["color2"].get<std::string>();
                if (gr.contains("color3")) grain.color3 = gr["color3"].get<std::string>();
                if (gr.contains("preset")) grain.preset = gr["preset"].get<std::string>();
                if (gr.contains("intensity")) grain.intensity = gr["intensity"].get<double>();
                if (gr.contains("algorithm")) grain.algorithm = gr["algorithm"].get<std::string>();
                if (gr.contains("tint")) grain.tint = gr["tint"].get<std::string>();
                if (gr.contains("scale")) grain.scale = gr["scale"].get<int>();
                if (gr.contains("crosshair")) grain.crosshair = gr["crosshair"].get<bool>();
            }

            if (j.contains("taskgraph")) {
                auto& t = j["taskgraph"];
                if (t.contains("spring_k")) taskgraph.spring_k = t["spring_k"].get<double>();
                if (t.contains("rest_length")) taskgraph.rest_length = t["rest_length"].get<double>();
                if (t.contains("repulsion")) taskgraph.repulsion = t["repulsion"].get<double>();
                if (t.contains("center_gravity")) taskgraph.center_gravity = t["center_gravity"].get<double>();
                if (t.contains("damping")) taskgraph.damping = t["damping"].get<double>();
                if (t.contains("drift_speed")) taskgraph.drift_speed = t["drift_speed"].get<double>();
                if (t.contains("edge_width")) taskgraph.edge_width = t["edge_width"].get<double>();
                if (t.contains("edge_alpha")) taskgraph.edge_alpha = t["edge_alpha"].get<double>();
                if (t.contains("node_radius")) taskgraph.node_radius = t["node_radius"].get<double>();
                if (t.contains("repulsion_radius")) taskgraph.repulsion_radius = t["repulsion_radius"].get<double>();
                if (t.contains("auto_freeze")) taskgraph.auto_freeze = t["auto_freeze"].get<bool>();
            }

            if (j.contains("display_settings")) {
                auto& d = j["display_settings"];
                if (d.contains("monitor_name")) disp.monitor_name = d["monitor_name"].get<std::string>();
                if (d.contains("mode")) disp.mode = d["mode"].get<std::string>();
                if (d.contains("scale")) disp.scale = d["scale"].get<double>();
                if (d.contains("text_scale")) disp.text_scale = d["text_scale"].get<double>();
                if (d.contains("cursor_size")) disp.cursor_size = d["cursor_size"].get<int>();
                if (d.contains("vrr")) disp.vrr = d["vrr"].get<int>();
            }

            if (j.contains("input")) {
                auto& in = j["input"];
                if (in.contains("sensitivity")) inp.sensitivity = in["sensitivity"].get<double>();
                if (in.contains("natural_scroll")) inp.natural_scroll = in["natural_scroll"].get<bool>();
                if (in.contains("tap_to_click")) inp.tap_to_click = in["tap_to_click"].get<bool>();
                if (in.contains("scroll_factor")) inp.scroll_factor = in["scroll_factor"].get<double>();
                if (in.contains("disable_while_typing")) inp.disable_while_typing = in["disable_while_typing"].get<bool>();
            }

            if (j.contains("display")) {
                auto& dy = j["display"];
                if (dy.contains("nightlight_enabled")) night.enabled = dy["nightlight_enabled"].get<bool>();
                if (dy.contains("nightlight_temp")) night.temp = dy["nightlight_temp"].get<int>();
            }

            if (j.contains("waybar")) {
                auto& w = j["waybar"];
                if (w.contains("position")) waybar.position = w["position"].get<std::string>();
                if (w.contains("height")) waybar.height = w["height"].get<int>();
                if (w.contains("font_size")) waybar.font_size = w["font_size"].get<int>();
                if (w.contains("drawer_enabled")) waybar.drawer_enabled = w["drawer_enabled"].get<bool>();
                if (w.contains("bg_color")) waybar.bg_color = w["bg_color"].get<std::string>();
                if (w.contains("border_color")) waybar.border_color = w["border_color"].get<std::string>();
                if (w.contains("text_color")) waybar.text_color = w["text_color"].get<std::string>();
                if (w.contains("active_ws_bg")) waybar.active_ws_bg = w["active_ws_bg"].get<std::string>();
                if (w.contains("active_ws_text")) waybar.active_ws_text = w["active_ws_text"].get<std::string>();
                if (w.contains("inactive_ws_text")) waybar.inactive_ws_text = w["inactive_ws_text"].get<std::string>();
                if (w.contains("drawer_btn_bg")) waybar.drawer_btn_bg = w["drawer_btn_bg"].get<std::string>();
                if (w.contains("drawer_btn_border")) waybar.drawer_btn_border = w["drawer_btn_border"].get<std::string>();
                if (w.contains("show_bluetooth")) waybar.show_bluetooth = w["show_bluetooth"].get<bool>();
                if (w.contains("show_idle_inhibitor")) waybar.show_idle_inhibitor = w["show_idle_inhibitor"].get<bool>();
                if (w.contains("show_cpu")) waybar.show_cpu = w["show_cpu"].get<bool>();
                if (w.contains("show_memory")) waybar.show_memory = w["show_memory"].get<bool>();
                if (w.contains("show_network")) waybar.show_network = w["show_network"].get<bool>();
                if (w.contains("show_audio")) waybar.show_audio = w["show_audio"].get<bool>();
                if (w.contains("show_backlight")) waybar.show_backlight = w["show_backlight"].get<bool>();
                if (w.contains("show_battery")) waybar.show_battery = w["show_battery"].get<bool>();
                if (w.contains("show_nightlight")) waybar.show_nightlight = w["show_nightlight"].get<bool>();
                if (w.contains("show_notifications")) waybar.show_notifications = w["show_notifications"].get<bool>();
                if (w.contains("show_power")) waybar.show_power = w["show_power"].get<bool>();
            }

            if (j.contains("fuzzel")) {
                auto& fz = j["fuzzel"];
                if (fz.contains("lines")) fuzzel.lines = fz["lines"].get<int>();
                if (fz.contains("width")) fuzzel.width = fz["width"].get<int>();
                if (fz.contains("border_radius")) fuzzel.border_radius = fz["border_radius"].get<int>();
                if (fz.contains("horizontal_pad")) fuzzel.horizontal_pad = fz["horizontal_pad"].get<int>();
                if (fz.contains("vertical_pad")) fuzzel.vertical_pad = fz["vertical_pad"].get<int>();
                if (fz.contains("bg_color")) fuzzel.bg_color = fz["bg_color"].get<std::string>();
                if (fz.contains("border_color")) fuzzel.border_color = fz["border_color"].get<std::string>();
                if (fz.contains("text_color")) fuzzel.text_color = fz["text_color"].get<std::string>();
                if (fz.contains("match_color")) fuzzel.match_color = fz["match_color"].get<std::string>();
                if (fz.contains("selection_bg")) fuzzel.selection_bg = fz["selection_bg"].get<std::string>();
                if (fz.contains("selection_text")) fuzzel.selection_text = fz["selection_text"].get<std::string>();
            }
        } catch (...) {}
    }

    void save() {
        nlohmann::json j;

        j["animations"] = {
            {"enabled", anim.enabled},
            {"preset", anim.preset},
            {"p1x", anim.p1x},
            {"p1y", anim.p1y},
            {"p2x", anim.p2x},
            {"p2y", anim.p2y},
            {"windows_in", anim.windows_in},
            {"windows_out", anim.windows_out},
            {"windows_move", anim.windows_move},
            {"workspaces", anim.workspaces},
            {"fade", anim.fade},
            {"workspace_style", anim.workspace_style},
            {"window_style", anim.window_style}
        };

        j["geometry"] = {
            {"gaps_in", geom.gaps_in},
            {"gaps_out", geom.gaps_out},
            {"border_size", geom.border_size},
            {"rounding", geom.rounding},
            {"active_border_1", geom.active_border_1},
            {"active_border_2", geom.active_border_2},
            {"inactive_border", geom.inactive_border},
            {"border_angle", geom.border_angle}
        };

        j["opacity"] = {
            {"active_opacity", opac.active_opacity},
            {"inactive_opacity", opac.inactive_opacity},
            {"fullscreen_opacity", opac.fullscreen_opacity},
            {"blur_enabled", opac.blur_enabled},
            {"blur_size", opac.blur_size},
            {"blur_passes", opac.blur_passes},
            {"shadow_enabled", opac.shadow_enabled},
            {"shadow_range", opac.shadow_range}
        };

        j["grain"] = {
            {"gradient_type", grain.gradient_type},
            {"color1", grain.color1},
            {"color2", grain.color2},
            {"color3", grain.color3},
            {"preset", grain.preset},
            {"intensity", grain.intensity},
            {"algorithm", grain.algorithm},
            {"tint", grain.tint},
            {"scale", grain.scale},
            {"crosshair", grain.crosshair}
        };

        j["taskgraph"] = {
            {"spring_k", taskgraph.spring_k},
            {"rest_length", taskgraph.rest_length},
            {"repulsion", taskgraph.repulsion},
            {"center_gravity", taskgraph.center_gravity},
            {"damping", taskgraph.damping},
            {"drift_speed", taskgraph.drift_speed},
            {"edge_width", taskgraph.edge_width},
            {"edge_alpha", taskgraph.edge_alpha},
            {"node_radius", taskgraph.node_radius},
            {"repulsion_radius", taskgraph.repulsion_radius},
            {"auto_freeze", taskgraph.auto_freeze}
        };

        j["display_settings"] = {
            {"monitor_name", disp.monitor_name},
            {"mode", disp.mode},
            {"scale", disp.scale},
            {"text_scale", disp.text_scale},
            {"cursor_size", disp.cursor_size},
            {"vrr", disp.vrr}
        };

        j["input"] = {
            {"sensitivity", inp.sensitivity},
            {"natural_scroll", inp.natural_scroll},
            {"tap_to_click", inp.tap_to_click},
            {"scroll_factor", inp.scroll_factor},
            {"disable_while_typing", inp.disable_while_typing}
        };

        j["display"] = {
            {"nightlight_enabled", night.enabled},
            {"nightlight_temp", night.temp}
        };

        j["waybar"] = {
            {"position", waybar.position},
            {"height", waybar.height},
            {"font_size", waybar.font_size},
            {"drawer_enabled", waybar.drawer_enabled},
            {"bg_color", waybar.bg_color},
            {"border_color", waybar.border_color},
            {"text_color", waybar.text_color},
            {"active_ws_bg", waybar.active_ws_bg},
            {"active_ws_text", waybar.active_ws_text},
            {"inactive_ws_text", waybar.inactive_ws_text},
            {"drawer_btn_bg", waybar.drawer_btn_bg},
            {"drawer_btn_border", waybar.drawer_btn_border},
            {"show_bluetooth", waybar.show_bluetooth},
            {"show_idle_inhibitor", waybar.show_idle_inhibitor},
            {"show_cpu", waybar.show_cpu},
            {"show_memory", waybar.show_memory},
            {"show_network", waybar.show_network},
            {"show_audio", waybar.show_audio},
            {"show_backlight", waybar.show_backlight},
            {"show_battery", waybar.show_battery},
            {"show_nightlight", waybar.show_nightlight},
            {"show_notifications", waybar.show_notifications},
            {"show_power", waybar.show_power}
        };

        j["fuzzel"] = {
            {"lines", fuzzel.lines},
            {"width", fuzzel.width},
            {"border_radius", fuzzel.border_radius},
            {"horizontal_pad", fuzzel.horizontal_pad},
            {"vertical_pad", fuzzel.vertical_pad},
            {"bg_color", fuzzel.bg_color},
            {"border_color", fuzzel.border_color},
            {"text_color", fuzzel.text_color},
            {"match_color", fuzzel.match_color},
            {"selection_bg", fuzzel.selection_bg},
            {"selection_text", fuzzel.selection_text}
        };

        // Write settings.json
        {
            std::ofstream f(config_dir() + "/settings.json");
            if (f.is_open()) f << j.dump(2) << std::endl;
        }

        // Generate user-settings.conf
        write_user_conf();

        // Update Waybar JSON & CSS
        update_waybar();

        // Update Fuzzel Config
        update_fuzzel();

        // Generate procedural gradient grain wallpaper and reload hyprpaper
        render_wallpaper();
    }

    void render_wallpaper() {
        std::string wall_path = config_dir() + "/wallpaper.png";
        generate_procedural_grain(
            wall_path,
            1920,
            1200,
            grain.algorithm,
            grain.intensity,
            grain.scale,
            grain.gradient_type,
            grain.color1,
            grain.color2,
            grain.color3,
            grain.tint,
            grain.crosshair
        );

        // Tell hyprpaper to reload wallpaper smoothly
        system("hyprctl hyprpaper reload ,~/.config/monolith/wallpaper.png >/dev/null 2>&1 || (pkill -x hyprpaper; hyprpaper -c ~/.config/monolith/hyprpaper.conf >/dev/null 2>&1 &)");
    }

    void write_user_conf() {
        std::string hex1 = geom.active_border_1;
        std::string hex2 = geom.active_border_2;
        std::string hex_in = geom.inactive_border;
        if (!hex1.empty() && hex1[0] == '#') hex1 = hex1.substr(1);
        if (!hex2.empty() && hex2[0] == '#') hex2 = hex2.substr(1);
        if (!hex_in.empty() && hex_in[0] == '#') hex_in = hex_in.substr(1);

        std::ostringstream ss;
        ss << "# ╔══════════════════════════════════════════════════════════════════╗\n"
           << "# ║             MONOLITH USER SETTINGS (AUTO-GENERATED C++)          ║\n"
           << "# ╚══════════════════════════════════════════════════════════════════╝\n\n"
           << "# --- Monitor & Scaling Configuration ---\n"
           << "monitor = " << disp.monitor_name << ", " << disp.mode << ", auto, " << std::fixed << std::setprecision(2) << disp.scale << "\n\n"
           << "debug {\n"
           << "    disable_scale_checks = true\n"
           << "}\n\n"
           << "misc {\n"
           << "    vrr = " << disp.vrr << "\n"
           << "}\n\n"
           << "animations {\n"
           << "    enabled = " << (anim.enabled ? "true" : "false") << "\n"
           << "    bezier = customCurve, " << std::fixed << std::setprecision(2) << anim.p1x << ", " << anim.p1y << ", " << anim.p2x << ", " << anim.p2y << "\n"
           << "    animation = windowsIn, 1, " << std::fixed << std::setprecision(1) << anim.windows_in << ", customCurve, " << anim.window_style << "\n"
           << "    animation = windowsOut, 1, " << std::fixed << std::setprecision(1) << anim.windows_out << ", customCurve, " << anim.window_style << "\n"
           << "    animation = windowsMove, 1, " << std::fixed << std::setprecision(1) << anim.windows_move << ", customCurve\n"
           << "    animation = workspaces, 1, " << std::fixed << std::setprecision(1) << anim.workspaces << ", customCurve, " << anim.workspace_style << "\n"
           << "    animation = specialWorkspace, 1, " << std::fixed << std::setprecision(1) << anim.workspaces << ", customCurve, slidevert 100%\n"
           << "    animation = fade, 1, " << std::fixed << std::setprecision(1) << anim.fade << ", customCurve\n"
           << "}\n\n"
           << "general {\n"
           << "    gaps_in = " << geom.gaps_in << "\n"
           << "    gaps_out = " << geom.gaps_out << "\n"
           << "    border_size = " << geom.border_size << "\n"
           << "    col.active_border = rgba(" << hex1 << "ee) rgba(" << hex2 << "aa) " << geom.border_angle << "deg\n"
           << "    col.inactive_border = rgba(" << hex_in << "88)\n"
           << "}\n\n"
           << "decoration {\n"
           << "    rounding = " << geom.rounding << "\n"
           << "    active_opacity = " << std::fixed << std::setprecision(2) << opac.active_opacity << "\n"
           << "    inactive_opacity = " << std::fixed << std::setprecision(2) << opac.inactive_opacity << "\n"
           << "    fullscreen_opacity = " << std::fixed << std::setprecision(2) << opac.fullscreen_opacity << "\n"
           << "    shadow {\n"
           << "        enabled = " << (opac.shadow_enabled ? "true" : "false") << "\n"
           << "        range = " << opac.shadow_range << "\n"
           << "    }\n"
           << "    blur {\n"
           << "        enabled = " << (opac.blur_enabled ? "true" : "false") << "\n"
           << "        size = " << opac.blur_size << "\n"
           << "        passes = " << opac.blur_passes << "\n"
           << "    }\n"
           << "}\n\n"
           << "input {\n"
           << "    sensitivity = " << std::fixed << std::setprecision(2) << inp.sensitivity << "\n"
           << "    touchpad {\n"
           << "        natural_scroll = " << (inp.natural_scroll ? "true" : "false") << "\n"
           << "        tap-to-click = " << (inp.tap_to_click ? "true" : "false") << "\n"
           << "        disable_while_typing = " << (inp.disable_while_typing ? "true" : "false") << "\n"
           << "        scroll_factor = " << std::fixed << std::setprecision(2) << inp.scroll_factor << "\n"
           << "    }\n"
           << "}\n";

        std::ofstream f(config_dir() + "/user-settings.conf");
        if (f.is_open()) f << ss.str();

        // Apply immediately via hyprctl without compositor restart
        hypr_keyword("general:gaps_in", geom.gaps_in);
        hypr_keyword("general:gaps_out", geom.gaps_out);
        hypr_keyword("general:border_size", geom.border_size);
        hypr_keyword("decoration:rounding", geom.rounding);
        hypr_keyword("decoration:active_opacity", opac.active_opacity);
        hypr_keyword("decoration:inactive_opacity", opac.inactive_opacity);
        hypr_keyword("input:sensitivity", inp.sensitivity);
    }

    void update_waybar() {
        std::string conf_path = config_dir() + "/waybar/config.jsonc";
        try {
            std::ifstream f(conf_path);
            if (!f.is_open()) return;
            nlohmann::json wb_j;
            f >> wb_j;
            f.close();

            wb_j["position"] = waybar.position;
            wb_j["height"] = waybar.height;

            wb_j["modules-left"] = {
                "hyprland/workspaces",
                "custom/spatial_tab",
                "hyprland/window"
            };

            std::vector<std::string> drawer_mods;
            drawer_mods.push_back("custom/drawer_toggle");
            if (waybar.show_idle_inhibitor) drawer_mods.push_back("idle_inhibitor");
            if (waybar.show_bluetooth) drawer_mods.push_back("bluetooth");
            if (waybar.show_network) drawer_mods.push_back("network");
            if (waybar.show_audio) drawer_mods.push_back("pulseaudio");
            if (waybar.show_backlight) drawer_mods.push_back("backlight");
            if (waybar.show_battery) drawer_mods.push_back("battery");
            if (waybar.show_cpu) drawer_mods.push_back("cpu");
            if (waybar.show_memory) drawer_mods.push_back("memory");
            if (waybar.show_nightlight) drawer_mods.push_back("custom/nightlight");

            std::vector<std::string> right;
            std::string toggle_script = config_dir() + "/scripts/drawer-toggle.sh";
            wb_j["custom/drawer_toggle"] = {
                {"format", waybar.drawer_enabled ? "󰍜" : "󰐃"},
                {"tooltip", waybar.drawer_enabled ? "System Drawer (Hover to reveal, Click to pin)" : "System Drawer [Pinned] (Click to collapse)"},
                {"on-click", toggle_script}
            };

            if (waybar.drawer_enabled) {
                // Collapsible drawer mode (Hover to reveal)
                wb_j["group/drawer"] = {
                    {"orientation", "horizontal"},
                    {"drawer", {
                        {"transition-duration", 200},
                        {"children-class", "drawer-child"},
                        {"transition-left-to-right", false},
                        {"click-to-reveal", false}
                    }},
                    {"modules", drawer_mods}
                };
                right.push_back("group/drawer");
            } else {
                // Always expanded / pinned mode: all tray modules visible on the bar
                for (size_t i = 0; i < drawer_mods.size(); ++i) {
                    right.push_back(drawer_mods[i]);
                }
            }

            right.push_back("custom/settings");
            if (waybar.show_notifications) right.push_back("custom/notification");
            if (waybar.show_power) right.push_back("custom/power");

            wb_j["modules-right"] = right;

            std::ofstream out(conf_path);
            if (out.is_open()) out << wb_j.dump(2) << std::endl;
        } catch (...) {}

        // Regenerate Waybar CSS using customized color themes
        std::string css_path = config_dir() + "/waybar/style.css";
        std::ostringstream css;
        css << "* {\n"
            << "    border: none;\n"
            << "    border-radius: 0;\n"
            << "    font-family: \"JetBrains Mono\", monospace;\n"
            << "    font-weight: 600;\n"
            << "    font-size: " << waybar.font_size << "px;\n"
            << "    min-height: 0;\n"
            << "}\n\n"
            << "window#waybar {\n"
            << "    background: " << waybar.bg_color << ";\n"
            << "    color: " << waybar.text_color << ";\n"
            << "    border-bottom: 1px solid " << waybar.border_color << ";\n"
            << "}\n\n"
            << "#workspaces button {\n"
            << "    padding: 0 6px;\n"
            << "    background: transparent;\n"
            << "    color: " << waybar.inactive_ws_text << ";\n"
            << "    border-radius: 2px;\n"
            << "    margin: 2px 1px;\n"
            << "}\n"
            << "#workspaces button.active {\n"
            << "    background: " << waybar.active_ws_bg << ";\n"
            << "    color: " << waybar.active_ws_text << ";\n"
            << "}\n\n"
            << "#custom-spatial_tab {\n"
            << "    padding: 0 8px;\n"
            << "    margin: 3px 6px;\n"
            << "    font-size: 11px;\n"
            << "    font-weight: 700;\n"
            << "    letter-spacing: 0.5px;\n"
            << "    border-radius: 2px;\n"
            << "    background: #0a0a0a;\n"
            << "    border: 1px solid #27272a;\n"
            << "    color: #a1a1aa;\n"
            << "}\n"
            << "#custom-spatial_tab.tab-spotify {\n"
            << "    background: #0f2415;\n"
            << "    border-color: #1db954;\n"
            << "    color: #1ed760;\n"
            << "}\n"
            << "#custom-spatial_tab.tab-extra {\n"
            << "    background: #231808;\n"
            << "    border-color: #d97706;\n"
            << "    color: #f59e0b;\n"
            << "}\n"
            << "#custom-spatial_tab.tab-desktop {\n"
            << "    background: #0f0f10;\n"
            << "    color: #d4d4d8;\n"
            << "}\n\n"
            << "#window {\n"
            << "    color: #71717a;\n"
            << "    font-weight: 500;\n"
            << "}\n\n"
            << "#clock {\n"
            << "    color: " << waybar.text_color << ";\n"
            << "    font-weight: 700;\n"
            << "    letter-spacing: 0.5px;\n"
            << "}\n\n"
            << "#custom-drawer_toggle {\n"
            << "    padding: 0 7px;\n"
            << "    margin: 3px 2px;\n"
            << "    color: " << waybar.text_color << ";\n"
            << "    background: " << waybar.drawer_btn_bg << ";\n"
            << "    border: 1px solid " << waybar.drawer_btn_border << ";\n"
            << "    border-radius: 2px;\n"
            << "}\n"
            << "#custom-drawer_toggle:hover {\n"
            << "    background: #1f1f23;\n"
            << "    border-color: #52525b;\n"
            << "    color: #ffffff;\n"
            << "}\n\n"
            << "#idle_inhibitor,\n"
            << "#bluetooth,\n"
            << "#network,\n"
            << "#pulseaudio,\n"
            << "#backlight,\n"
            << "#battery,\n"
            << "#cpu,\n"
            << "#memory,\n"
            << "#custom-nightlight,\n"
            << "#custom-settings,\n"
            << "#custom-notification,\n"
            << "#custom-power {\n"
            << "    padding: 0 6px;\n"
            << "    margin: 2px 1px;\n"
            << "    color: " << waybar.text_color << ";\n"
            << "    border-radius: 2px;\n"
            << "}\n\n"
            << "#idle_inhibitor:hover,\n"
            << "#bluetooth:hover,\n"
            << "#network:hover,\n"
            << "#pulseaudio:hover,\n"
            << "#backlight:hover,\n"
            << "#battery:hover,\n"
            << "#cpu:hover,\n"
            << "#memory:hover,\n"
            << "#custom-nightlight:hover,\n"
            << "#custom-settings:hover,\n"
            << "#custom-notification:hover,\n"
            << "#custom-power:hover {\n"
            << "    background: #18181b;\n"
            << "    color: #ffffff;\n"
            << "}\n\n"
            << "tooltip {\n"
            << "    background: #000000;\n"
            << "    border: 1px solid #27272a;\n"
            << "    border-radius: 4px;\n"
            << "}\n"
            << "tooltip label {\n"
            << "    font-family: \"JetBrains Mono\", monospace;\n"
            << "    font-size: 11px;\n"
            << "    color: #e4e4e7;\n"
            << "    padding: 3px 6px;\n"
            << "}\n"
            << ".drawer-child {\n"
            << "    padding: 0 5px;\n"
            << "    margin: 2px 1px;\n"
            << "}\n";

        std::ofstream css_out(css_path);
        if (css_out.is_open()) css_out << css.str();

        // Silent reload of Waybar in-place
        system("pkill -SIGUSR2 waybar 2>/dev/null || true");
    }

    void update_fuzzel() {
        std::string path = config_dir() + "/fuzzel/fuzzel.ini";
        std::ostringstream ss;
        auto fmt_fuzzel_color = [](std::string col) -> std::string {
            if (!col.empty() && col[0] == '#') col = col.substr(1);
            if (col.length() == 6) col += "ff";
            return col;
        };

        ss << "[main]\n"
           << "font=JetBrains Mono:size=11\n"
           << "terminal=foot\n"
           << "prompt=\"❯ \"\n"
           << "icon-theme=Papirus-Dark\n"
           << "icons=yes\n"
           << "lines=" << fuzzel.lines << "\n"
           << "width=" << fuzzel.width << "\n"
           << "horizontal-pad=" << fuzzel.horizontal_pad << "\n"
           << "vertical-pad=" << fuzzel.vertical_pad << "\n"
           << "inner-pad=8\n\n"
           << "[colors]\n"
           << "background=" << fmt_fuzzel_color(fuzzel.bg_color) << "\n"
           << "text=" << fmt_fuzzel_color(fuzzel.text_color) << "\n"
           << "match=" << fmt_fuzzel_color(fuzzel.match_color) << "\n"
           << "selection=" << fmt_fuzzel_color(fuzzel.selection_bg) << "\n"
           << "selection-text=" << fmt_fuzzel_color(fuzzel.selection_text) << "\n"
           << "selection-match=" << fmt_fuzzel_color(fuzzel.match_color) << "\n"
           << "border=" << fmt_fuzzel_color(fuzzel.border_color) << "\n\n"
           << "[border]\n"
           << "width=1\n"
           << "radius=" << fuzzel.border_radius << "\n";

        std::ofstream f(path);
        if (f.is_open()) f << ss.str();
    }
};

} // namespace Monolith
