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
#include "hypr_ipc.hpp"
#include "version.hpp"

namespace Monolith {

inline std::string get_home_dir() {
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') return std::string(home);
    const char* user = std::getenv("USER");
    if (user && user[0] != '\0') return "/home/" + std::string(user);
    return "/home/boski";
}

inline std::string config_dir() {
    return get_home_dir() + "/.config/monolith";
}

inline std::string run_cmd(const std::string& cmd) {
    char buffer[512];
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

inline void hypr_keyword(const std::string& key, const std::string& val) {
    if (!HyprIPC::keyword(key, val)) {
        std::string cmd = "hyprctl keyword " + key + " \"" + val + "\" > /dev/null 2>&1";
        system(cmd.c_str());
    }
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
        int rounding = 6;
        std::string active_border_1 = "#ffffff";
        std::string active_border_2 = "#333333";
        int border_angle = 45;
        std::string inactive_border = "#141414";
        bool smart_gaps = false;
    } geom;

    // Window Opacity & GPU Shaders
    struct Opacity {
        double active_opacity = 1.0;
        double inactive_opacity = 1.0;
        double fullscreen_opacity = 1.0;
        bool blur_enabled = false;
        int blur_size = 6;
        int blur_passes = 2;
        bool shadow_enabled = false;
        int shadow_range = 20;
        bool dim_inactive = false;
        double dim_strength = 0.20;
    } opac;

    // Procedural Mathematical Film Grain & Gradients
    struct Grain {
        std::string gradient_type = "diagonal_up"; // "none", "vertical", "horizontal", "diagonal_down", "diagonal_up", "radial", "mesh_3point"
        std::string color1 = "#000000";            // Base / Bottom
        std::string color2 = "#18181b";            // Top / Left / Point 2
        std::string color3 = "#27272a";            // Point 3 (top-right lower)
        std::string preset = "OLED Monolith";
        double intensity = 14.0;
        std::string algorithm = "gaussian";        // "gaussian", "harmonic", "quantum", "dither"
        std::string tint = "#d4d4d8";
        int scale = 1;
        bool crosshair = false;
    } grain;

    // Desktop Task Graph Physics Engine
    struct TaskGraph {
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
    } taskgraph;

    // Display & Monitor Output
    struct Display {
        std::string monitor_name = "eDP-1";
        std::string mode = "1920x1200@60.00Hz";
        double scale = 1.25;
        double text_scale = 1.00;
        int cursor_size = 24;
        int vrr = 0; // 0=off, 1=on, 2=fullscreen only
        bool allow_tearing = false;
        bool direct_scanout = true;
        int idle_timeout_min = 10;
        bool battery_limit_80 = false;
    } disp;

    // Input & Touchpad Configuration
    struct Input {
        double sensitivity = 0.0;
        bool natural_scroll = true;
        bool tap_to_click = true;
        double scroll_factor = 1.0;
        bool disable_while_typing = true;
        int repeat_delay = 250;
        int repeat_rate = 45;
        std::string accel_profile = "adaptive"; // "adaptive" or "flat"
        bool middle_click_paste = true;
        bool left_handed = false;
        int follow_mouse = 1; // 1 = hover, 0 = click, 2 = detached
    } inp;

    // Night Light (Hyprsunset)
    struct Nightlight {
        bool enabled = false;
        int temp = 4200;
    } night;

    // Minimalist Status Bar (Waybar)
    struct Waybar {
        std::string position = "top";
        int height = 33;
        int font_size = 12;
        bool drawer_enabled = false;
        std::string bg_color = "#000000";
        std::string border_color = "#141414";
        std::string text_color = "#e4e4e7";
        std::string active_ws_bg = "#18181b";
        std::string active_ws_text = "#ffffff";
        std::string inactive_ws_text = "#52525b";
        std::string drawer_btn_bg = "#0c0c0e";
        std::string drawer_btn_border = "#1f1f23";
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
        int border_radius = 8;
        int horizontal_pad = 18;
        int vertical_pad = 12;
        std::string bg_color = "#000000fa";
        std::string border_color = "#27272aff";
        std::string text_color = "#e4e4e7ff";
        std::string match_color = "#ffffffff";
        std::string selection_bg = "#18181bff";
        std::string selection_text = "#fffffffa";
    } fuzzel;

    // Autonomous System Updates
    struct Updates {
        std::string repo = MONOLITH_DEFAULT_REPO;
        std::string channel = "releases";
        bool auto_check_on_startup = false;
        bool backup_before_update = true;
    } updates;

    struct MonitorEntry {
        std::string name = "eDP-1";
        std::string description = "Built-in Display";
        std::string mode = "1920x1200@60.00Hz";
        std::string position = "auto";
        double scale = 1.25;
        int vrr = 0;
        bool enabled = true;
        std::string mirror_source = "";
        std::vector<std::string> available_modes;
    };
    std::vector<MonitorEntry> monitors_list;

    std::string monitor_description = "Built-in Display";

    void detect_monitor() {
        std::string out = HyprIPC::request("j/monitors");
        if (out.empty()) out = run_cmd("hyprctl monitors -j 2>/dev/null");
        if (out.empty()) return;
        try {
            auto j = nlohmann::json::parse(out);
            if (j.is_array() && !j.empty()) {
                monitors_list.clear();
                for (size_t idx = 0; idx < j.size(); ++idx) {
                    auto& m = j[idx];
                    MonitorEntry me;
                    if (m.contains("name")) me.name = m["name"].get<std::string>();
                    if (m.contains("description")) me.description = m["description"].get<std::string>();
                    if (m.contains("scale")) me.scale = m["scale"].get<double>();
                    if (m.contains("vrr")) me.vrr = m["vrr"].get<bool>() ? 1 : 0;
                    me.position = (idx == 0) ? "0x0" : "auto-right";
                    if (m.contains("width") && m.contains("height") && m.contains("refreshRate")) {
                        int w = m["width"].get<int>();
                        int h = m["height"].get<int>();
                        double rr = m["refreshRate"].get<double>();
                        std::ostringstream ss;
                        ss << w << "x" << h << "@" << std::fixed << std::setprecision(2) << rr << "Hz";
                        me.mode = ss.str();
                    }
                    if (m.contains("availableModes") && m["availableModes"].is_array()) {
                        for (auto& am : m["availableModes"]) {
                            me.available_modes.push_back(am.get<std::string>());
                        }
                    }
                    if (idx == 0) {
                        disp.monitor_name = me.name;
                        disp.scale = me.scale;
                        disp.mode = me.mode;
                        disp.vrr = me.vrr;
                        monitor_description = me.description;
                    }
                    monitors_list.push_back(me);
                }
            }
        } catch (...) {}
    }

    void detect_battery_limit() {
        std::ifstream f("/sys/class/power_supply/BAT0/charge_control_end_threshold");
        if (f.is_open()) {
            int val = 100;
            f >> val;
            disp.battery_limit_80 = (val <= 85);
        }
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
                anim.enabled = a.value("enabled", anim.enabled);
                anim.preset = a.value("preset", anim.preset);
                anim.p1x = a.value("p1x", anim.p1x);
                anim.p1y = a.value("p1y", anim.p1y);
                anim.p2x = a.value("p2x", anim.p2x);
                anim.p2y = a.value("p2y", anim.p2y);
                anim.windows_in = a.value("windows_in", anim.windows_in);
                anim.windows_out = a.value("windows_out", anim.windows_out);
                anim.windows_move = a.value("windows_move", anim.windows_move);
                anim.workspaces = a.value("workspaces", anim.workspaces);
                anim.fade = a.value("fade", anim.fade);
                anim.workspace_style = a.value("workspace_style", anim.workspace_style);
                anim.window_style = a.value("window_style", anim.window_style);
            }

            if (j.contains("geometry")) {
                auto& g = j["geometry"];
                geom.gaps_in = g.value("gaps_in", geom.gaps_in);
                geom.gaps_out = g.value("gaps_out", geom.gaps_out);
                geom.border_size = g.value("border_size", geom.border_size);
                geom.rounding = g.value("rounding", geom.rounding);
                geom.active_border_1 = g.value("active_border_1", geom.active_border_1);
                geom.active_border_2 = g.value("active_border_2", geom.active_border_2);
                geom.inactive_border = g.value("inactive_border", geom.inactive_border);
                geom.border_angle = g.value("border_angle", geom.border_angle);
                geom.smart_gaps = g.value("smart_gaps", geom.smart_gaps);
            }

            if (j.contains("opacity")) {
                auto& o = j["opacity"];
                opac.active_opacity = o.value("active_opacity", opac.active_opacity);
                opac.inactive_opacity = o.value("inactive_opacity", opac.inactive_opacity);
                opac.fullscreen_opacity = o.value("fullscreen_opacity", opac.fullscreen_opacity);
                opac.blur_enabled = o.value("blur_enabled", opac.blur_enabled);
                opac.blur_size = o.value("blur_size", opac.blur_size);
                opac.blur_passes = o.value("blur_passes", opac.blur_passes);
                opac.shadow_enabled = o.value("shadow_enabled", opac.shadow_enabled);
                opac.shadow_range = o.value("shadow_range", opac.shadow_range);
                opac.dim_inactive = o.value("dim_inactive", opac.dim_inactive);
                opac.dim_strength = o.value("dim_strength", opac.dim_strength);
            }

            if (j.contains("grain")) {
                auto& gr = j["grain"];
                grain.gradient_type = gr.value("gradient_type", grain.gradient_type);
                if (gr.contains("color1")) grain.color1 = gr["color1"].get<std::string>();
                else if (gr.contains("base_color")) grain.color1 = gr["base_color"].get<std::string>();
                grain.color2 = gr.value("color2", grain.color2);
                grain.color3 = gr.value("color3", grain.color3);
                grain.preset = gr.value("preset", grain.preset);
                grain.intensity = gr.value("intensity", grain.intensity);
                grain.algorithm = gr.value("algorithm", grain.algorithm);
                grain.tint = gr.value("tint", grain.tint);
                grain.scale = gr.value("scale", grain.scale);
                grain.crosshair = gr.value("crosshair", grain.crosshair);
            }

            if (j.contains("taskgraph")) {
                auto& t = j["taskgraph"];
                taskgraph.spring_k = t.value("spring_k", taskgraph.spring_k);
                taskgraph.rest_length = t.value("rest_length", taskgraph.rest_length);
                taskgraph.repulsion = t.value("repulsion", taskgraph.repulsion);
                taskgraph.center_gravity = t.value("center_gravity", taskgraph.center_gravity);
                taskgraph.damping = t.value("damping", taskgraph.damping);
                taskgraph.drift_speed = t.value("drift_speed", taskgraph.drift_speed);
                taskgraph.edge_width = t.value("edge_width", taskgraph.edge_width);
                taskgraph.edge_alpha = t.value("edge_alpha", taskgraph.edge_alpha);
                taskgraph.node_radius = t.value("node_radius", taskgraph.node_radius);
                taskgraph.repulsion_radius = t.value("repulsion_radius", taskgraph.repulsion_radius);
                taskgraph.auto_freeze = t.value("auto_freeze", taskgraph.auto_freeze);
            }

            if (j.contains("display_settings")) {
                auto& d = j["display_settings"];
                disp.monitor_name = d.value("monitor_name", disp.monitor_name);
                disp.mode = d.value("mode", disp.mode);
                disp.scale = d.value("scale", disp.scale);
                disp.text_scale = d.value("text_scale", disp.text_scale);
                disp.cursor_size = d.value("cursor_size", disp.cursor_size);
                disp.vrr = d.value("vrr", disp.vrr);
                disp.allow_tearing = d.value("allow_tearing", disp.allow_tearing);
                disp.direct_scanout = d.value("direct_scanout", disp.direct_scanout);
                disp.idle_timeout_min = d.value("idle_timeout_min", disp.idle_timeout_min);
                disp.battery_limit_80 = d.value("battery_limit_80", disp.battery_limit_80);
            }

            if (j.contains("monitors") && j["monitors"].is_array()) {
                for (const auto& mj : j["monitors"]) {
                    std::string mname = mj.value("name", "");
                    if (mname.empty()) continue;
                    bool found = false;
                    for (auto& m : monitors_list) {
                        if (m.name == mname) {
                            m.mode = mj.value("mode", m.mode);
                            m.position = mj.value("position", m.position);
                            m.scale = mj.value("scale", m.scale);
                            m.vrr = mj.value("vrr", m.vrr);
                            m.enabled = mj.value("enabled", m.enabled);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        MonitorEntry me;
                        me.name = mname;
                        me.description = mj.value("description", "External Display");
                        me.mode = mj.value("mode", "preferred");
                        me.position = mj.value("position", "auto-right");
                        me.scale = mj.value("scale", 1.0);
                        me.vrr = mj.value("vrr", 0);
                        me.enabled = mj.value("enabled", true);
                        monitors_list.push_back(me);
                    }
                }
            }

            if (j.contains("input")) {
                auto& in = j["input"];
                inp.sensitivity = in.value("sensitivity", inp.sensitivity);
                inp.natural_scroll = in.value("natural_scroll", inp.natural_scroll);
                inp.tap_to_click = in.value("tap_to_click", inp.tap_to_click);
                inp.scroll_factor = in.value("scroll_factor", inp.scroll_factor);
                inp.disable_while_typing = in.value("disable_while_typing", inp.disable_while_typing);
                inp.repeat_delay = in.value("repeat_delay", inp.repeat_delay);
                inp.repeat_rate = in.value("repeat_rate", inp.repeat_rate);
                inp.accel_profile = in.value("accel_profile", inp.accel_profile);
                inp.middle_click_paste = in.value("middle_click_paste", inp.middle_click_paste);
                inp.left_handed = in.value("left_handed", inp.left_handed);
                inp.follow_mouse = in.value("follow_mouse", inp.follow_mouse);
            }

            if (j.contains("display")) {
                auto& dy = j["display"];
                night.enabled = dy.value("nightlight_enabled", night.enabled);
                night.temp = dy.value("nightlight_temp", night.temp);
            }

            if (j.contains("waybar")) {
                auto& w = j["waybar"];
                waybar.position = w.value("position", waybar.position);
                waybar.height = w.value("height", waybar.height);
                waybar.font_size = w.value("font_size", waybar.font_size);
                waybar.drawer_enabled = w.value("drawer_enabled", waybar.drawer_enabled);
                waybar.bg_color = w.value("bg_color", waybar.bg_color);
                waybar.border_color = w.value("border_color", waybar.border_color);
                waybar.text_color = w.value("text_color", waybar.text_color);
                waybar.active_ws_bg = w.value("active_ws_bg", waybar.active_ws_bg);
                waybar.active_ws_text = w.value("active_ws_text", waybar.active_ws_text);
                waybar.inactive_ws_text = w.value("inactive_ws_text", waybar.inactive_ws_text);
                waybar.drawer_btn_bg = w.value("drawer_btn_bg", waybar.drawer_btn_bg);
                waybar.drawer_btn_border = w.value("drawer_btn_border", waybar.drawer_btn_border);
                waybar.show_bluetooth = w.value("show_bluetooth", waybar.show_bluetooth);
                waybar.show_idle_inhibitor = w.value("show_idle_inhibitor", waybar.show_idle_inhibitor);
                waybar.show_cpu = w.value("show_cpu", waybar.show_cpu);
                waybar.show_memory = w.value("show_memory", waybar.show_memory);
                waybar.show_network = w.value("show_network", waybar.show_network);
                waybar.show_audio = w.value("show_audio", waybar.show_audio);
                waybar.show_backlight = w.value("show_backlight", waybar.show_backlight);
                waybar.show_battery = w.value("show_battery", waybar.show_battery);
                waybar.show_nightlight = w.value("show_nightlight", waybar.show_nightlight);
                waybar.show_notifications = w.value("show_notifications", waybar.show_notifications);
                waybar.show_power = w.value("show_power", waybar.show_power);
            }

            if (j.contains("fuzzel")) {
                auto& fz = j["fuzzel"];
                fuzzel.lines = fz.value("lines", fuzzel.lines);
                fuzzel.width = fz.value("width", fuzzel.width);
                fuzzel.border_radius = fz.value("border_radius", fuzzel.border_radius);
                fuzzel.horizontal_pad = fz.value("horizontal_pad", fuzzel.horizontal_pad);
                fuzzel.vertical_pad = fz.value("vertical_pad", fuzzel.vertical_pad);
                fuzzel.bg_color = fz.value("bg_color", fuzzel.bg_color);
                fuzzel.border_color = fz.value("border_color", fuzzel.border_color);
                fuzzel.text_color = fz.value("text_color", fuzzel.text_color);
                fuzzel.match_color = fz.value("match_color", fuzzel.match_color);
                fuzzel.selection_bg = fz.value("selection_bg", fuzzel.selection_bg);
                fuzzel.selection_text = fz.value("selection_text", fuzzel.selection_text);
            }

            if (j.contains("updates")) {
                auto& u = j["updates"];
                updates.repo = u.value("repo", updates.repo);
                updates.channel = u.value("channel", updates.channel);
                updates.auto_check_on_startup = u.value("auto_check_on_startup", updates.auto_check_on_startup);
                updates.backup_before_update = u.value("backup_before_update", updates.backup_before_update);
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
            {"border_angle", geom.border_angle},
            {"smart_gaps", geom.smart_gaps}
        };

        j["opacity"] = {
            {"active_opacity", opac.active_opacity},
            {"inactive_opacity", opac.inactive_opacity},
            {"fullscreen_opacity", opac.fullscreen_opacity},
            {"blur_enabled", opac.blur_enabled},
            {"blur_size", opac.blur_size},
            {"blur_passes", opac.blur_passes},
            {"shadow_enabled", opac.shadow_enabled},
            {"shadow_range", opac.shadow_range},
            {"dim_inactive", opac.dim_inactive},
            {"dim_strength", opac.dim_strength}
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
            {"vrr", disp.vrr},
            {"allow_tearing", disp.allow_tearing},
            {"direct_scanout", disp.direct_scanout},
            {"idle_timeout_min", disp.idle_timeout_min},
            {"battery_limit_80", disp.battery_limit_80}
        };

        nlohmann::json mons_arr = nlohmann::json::array();
        for (const auto& m : monitors_list) {
            mons_arr.push_back({
                {"name", m.name},
                {"description", m.description},
                {"mode", m.mode},
                {"position", m.position},
                {"scale", m.scale},
                {"vrr", m.vrr},
                {"enabled", m.enabled}
            });
        }
        j["monitors"] = mons_arr;

        j["input"] = {
            {"sensitivity", inp.sensitivity},
            {"natural_scroll", inp.natural_scroll},
            {"tap_to_click", inp.tap_to_click},
            {"scroll_factor", inp.scroll_factor},
            {"disable_while_typing", inp.disable_while_typing},
            {"repeat_delay", inp.repeat_delay},
            {"repeat_rate", inp.repeat_rate},
            {"accel_profile", inp.accel_profile},
            {"middle_click_paste", inp.middle_click_paste},
            {"left_handed", inp.left_handed},
            {"follow_mouse", inp.follow_mouse}
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

        j["updates"] = {
            {"repo", updates.repo},
            {"channel", updates.channel},
            {"auto_check_on_startup", updates.auto_check_on_startup},
            {"backup_before_update", updates.backup_before_update}
        };

        // Write settings.json (which also notifies monolith-taskgraph via GFileMonitor)
        {
            std::ofstream f(config_dir() + "/settings.json");
            if (f.is_open()) f << j.dump(2) << std::endl;
        }

        // Also keep taskgraph/config.json physics synchronized
        sync_taskgraph_config();

        // Generate user-settings.conf & apply live via HyprIPC batch
        write_user_conf();

        // Update Waybar JSON & CSS
        update_waybar();

        // Update Fuzzel Config
        update_fuzzel();

        // Update Hypridle Dynamic Config
        update_hypridle();

        // Apply Hardware Battery Charge Threshold
        apply_battery_limit();

        // Generate procedural gradient grain wallpaper and reload hyprpaper
        render_wallpaper();
    }

    void apply_battery_limit() {
        if (std::ifstream("/sys/class/power_supply/BAT0/charge_control_end_threshold").good()) {
            std::string threshold = disp.battery_limit_80 ? "80" : "100";
            std::string cmd = "echo " + threshold + " | sudo -n tee /sys/class/power_supply/BAT0/charge_control_end_threshold >/dev/null 2>&1";
            system(cmd.c_str());
            if (disp.battery_limit_80) {
                system("sudo -n bash -c \"echo 'w /sys/class/power_supply/BAT0/charge_control_end_threshold - - - - 80' > /etc/tmpfiles.d/battery-limit.conf\" >/dev/null 2>&1");
            } else {
                system("sudo -n rm -f /etc/tmpfiles.d/battery-limit.conf >/dev/null 2>&1");
            }
        }
    }

    void update_hypridle() {
        std::string path = config_dir() + "/hypridle.conf";
        std::ostringstream ss;
        ss << "# Monolith Hypridle Dynamic Configuration\n"
           << "general {\n"
           << "    lock_cmd = pidof hyprlock || hyprlock --config ~/.config/monolith/hyprlock.conf\n"
           << "    before_sleep_cmd = loginctl lock-session\n"
           << "    after_sleep_cmd = hyprctl dispatch dpms on\n"
           << "}\n\n";

        if (disp.idle_timeout_min > 0) {
            int total_sec = disp.idle_timeout_min * 60;
            int dim_sec = std::max(60, total_sec - 120);
            int lock_sec = std::max(120, total_sec - 60);

            ss << "# Dim screen backlight\n"
               << "listener {\n"
               << "    timeout = " << dim_sec << "\n"
               << "    on-timeout = brightnessctl -s set 10%\n"
               << "    on-resume = brightnessctl -r\n"
               << "}\n\n"
               << "# Lock screen\n"
               << "listener {\n"
               << "    timeout = " << lock_sec << "\n"
               << "    on-timeout = loginctl lock-session\n"
               << "}\n\n"
               << "# Screen off (DPMS)\n"
               << "listener {\n"
               << "    timeout = " << total_sec << "\n"
               << "    on-timeout = hyprctl dispatch dpms off\n"
               << "    on-resume = hyprctl dispatch dpms on\n"
               << "}\n\n"
               << "# Suspend after 2x timeout on battery\n"
               << "listener {\n"
               << "    timeout = " << (total_sec * 2) << "\n"
               << "    on-timeout = systemctl suspend\n"
               << "}\n";
        }

        std::ofstream f(path);
        if (f.is_open()) f << ss.str();

        system("pkill -x hypridle; hypridle -c ~/.config/monolith/hypridle.conf >/dev/null 2>&1 &");
    }

    void sync_taskgraph_config() {
        std::string tg_path = config_dir() + "/taskgraph/config.json";
        try {
            nlohmann::json tg_j;
            {
                std::ifstream in(tg_path);
                if (in.is_open()) in >> tg_j;
            }
            tg_j["physics"] = {
                {"spring_k", taskgraph.spring_k},
                {"rest_length", taskgraph.rest_length},
                {"repulsion", taskgraph.repulsion},
                {"center_gravity", taskgraph.center_gravity},
                {"damping", taskgraph.damping},
                {"repulsion_radius", taskgraph.repulsion_radius},
                {"fuzzel_repulsion", 26.0}
            };
            std::ofstream out(tg_path);
            if (out.is_open()) out << tg_j.dump(2) << std::endl;
        } catch (...) {}
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

        // Reload hyprpaper smoothly in background
        system("hyprctl hyprpaper reload ,~/.config/monolith/wallpaper.png >/dev/null 2>&1 &");
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
           << "# --- Multi-Monitor & Scaling Configuration ---\n";
        if (monitors_list.empty()) {
            ss << "monitor = " << disp.monitor_name << ", " << disp.mode << ", auto, " << std::fixed << std::setprecision(2) << disp.scale << "\n";
        } else {
            for (const auto& m : monitors_list) {
                if (!m.enabled) {
                    ss << "monitor = " << m.name << ", disable\n";
                } else if (m.position == "mirror" && !m.mirror_source.empty()) {
                    ss << "monitor = " << m.name << ", " << m.mode << ", auto, " << std::fixed << std::setprecision(2) << m.scale << ", mirror, " << m.mirror_source << "\n";
                } else {
                    std::string pos = m.position.empty() ? "auto" : m.position;
                    ss << "monitor = " << m.name << ", " << m.mode << ", " << pos << ", " << std::fixed << std::setprecision(2) << m.scale << "\n";
                }
            }
        }
        ss << "monitor = , preferred, auto, 1\n\n"
           << "xwayland {\n"
           << "    force_zero_scaling = true\n"
           << "}\n\n"
           << "debug {\n"
           << "    disable_scale_checks = true\n"
           << "}\n\n"
           << "misc {\n"
           << "    vrr = " << disp.vrr << "\n"
           << "    middle_click_paste = " << (inp.middle_click_paste ? "true" : "false") << "\n"
           << "}\n\n"
           << "render {\n"
           << "    direct_scanout = " << (disp.direct_scanout ? "true" : "false") << "\n"
           << "}\n\n";

        if (geom.smart_gaps) {
            ss << "# --- Smart Gaps (Zero gaps when single window tiled) ---\n"
               << "workspace = w[tv1], gapsout:0, gapsin:0\n"
               << "workspace = f[1], gapsout:0, gapsin:0\n\n";
        }

        ss << "animations {\n"
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
           << "    allow_tearing = " << (disp.allow_tearing ? "true" : "false") << "\n"
           << "}\n\n"
           << "decoration {\n"
           << "    rounding = " << geom.rounding << "\n"
           << "    active_opacity = " << std::fixed << std::setprecision(2) << opac.active_opacity << "\n"
           << "    inactive_opacity = " << std::fixed << std::setprecision(2) << opac.inactive_opacity << "\n"
           << "    fullscreen_opacity = " << std::fixed << std::setprecision(2) << opac.fullscreen_opacity << "\n"
           << "    dim_inactive = " << (opac.dim_inactive ? "true" : "false") << "\n"
           << "    dim_strength = " << std::fixed << std::setprecision(2) << opac.dim_strength << "\n"
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
           << "    accel_profile = " << inp.accel_profile << "\n"
           << "    repeat_rate = " << inp.repeat_rate << "\n"
           << "    repeat_delay = " << inp.repeat_delay << "\n"
           << "    left_handed = " << (inp.left_handed ? "true" : "false") << "\n"
           << "    follow_mouse = " << inp.follow_mouse << "\n"
           << "    touchpad {\n"
           << "        natural_scroll = " << (inp.natural_scroll ? "true" : "false") << "\n"
           << "        tap-to-click = " << (inp.tap_to_click ? "true" : "false") << "\n"
           << "        disable_while_typing = " << (inp.disable_while_typing ? "true" : "false") << "\n"
           << "        scroll_factor = " << std::fixed << std::setprecision(2) << inp.scroll_factor << "\n"
           << "    }\n"
           << "}\n";

        std::ofstream f(config_dir() + "/user-settings.conf");
        if (f.is_open()) f << ss.str();

        // Apply all changes atomically via direct UNIX socket batch (0 forks, < 1ms)
        std::ostringstream bez_ss;
        bez_ss << "customCurve," << std::fixed << std::setprecision(2) << anim.p1x << "," << anim.p1y << "," << anim.p2x << "," << anim.p2y;

        std::vector<std::string> batch_cmds = {
            "keyword general:gaps_in " + std::to_string(geom.gaps_in),
            "keyword general:gaps_out " + std::to_string(geom.gaps_out),
            "keyword general:border_size " + std::to_string(geom.border_size),
            "keyword general:col.active_border rgba(" + hex1 + "ee) rgba(" + hex2 + "aa) " + std::to_string(geom.border_angle) + "deg",
            "keyword general:col.inactive_border rgba(" + hex_in + "88)",
            "keyword general:allow_tearing " + std::string(disp.allow_tearing ? "true" : "false"),
            "keyword render:direct_scanout " + std::string(disp.direct_scanout ? "true" : "false"),
            "keyword decoration:rounding " + std::to_string(geom.rounding),
            "keyword decoration:active_opacity " + std::to_string(opac.active_opacity),
            "keyword decoration:inactive_opacity " + std::to_string(opac.inactive_opacity),
            "keyword decoration:dim_inactive " + std::string(opac.dim_inactive ? "true" : "false"),
            "keyword decoration:dim_strength " + std::to_string(opac.dim_strength),
            "keyword decoration:shadow:enabled " + std::string(opac.shadow_enabled ? "true" : "false"),
            "keyword decoration:blur:enabled " + std::string(opac.blur_enabled ? "true" : "false"),
            "keyword animations:enabled " + std::string(anim.enabled ? "true" : "false"),
            "keyword bezier " + bez_ss.str(),
            "keyword input:sensitivity " + std::to_string(inp.sensitivity),
            "keyword input:accel_profile " + inp.accel_profile,
            "keyword input:repeat_rate " + std::to_string(inp.repeat_rate),
            "keyword input:repeat_delay " + std::to_string(inp.repeat_delay),
            "keyword input:left_handed " + std::string(inp.left_handed ? "true" : "false"),
            "keyword input:follow_mouse " + std::to_string(inp.follow_mouse),
            "keyword input:touchpad:natural_scroll " + std::string(inp.natural_scroll ? "true" : "false"),
            "keyword input:touchpad:tap-to-click " + std::string(inp.tap_to_click ? "true" : "false"),
            "keyword input:touchpad:disable_while_typing " + std::string(inp.disable_while_typing ? "true" : "false"),
            "keyword input:touchpad:scroll_factor " + std::to_string(inp.scroll_factor),
            "keyword misc:vrr " + std::to_string(disp.vrr),
            "keyword misc:middle_click_paste " + std::string(inp.middle_click_paste ? "true" : "false")
        };
        if (geom.smart_gaps) {
            batch_cmds.push_back("keyword workspace w[tv1], gapsout:0, gapsin:0");
            batch_cmds.push_back("keyword workspace f[1], gapsout:0, gapsin:0");
        } else {
            batch_cmds.push_back("keyword workspace w[tv1], gapsout:" + std::to_string(geom.gaps_out) + ", gapsin:" + std::to_string(geom.gaps_in));
            batch_cmds.push_back("keyword workspace f[1], gapsout:" + std::to_string(geom.gaps_out) + ", gapsin:" + std::to_string(geom.gaps_in));
        }
        for (const auto& m : monitors_list) {
            if (!m.enabled) {
                batch_cmds.push_back("keyword monitor " + m.name + ",disable");
            } else if (m.position == "mirror" && !m.mirror_source.empty()) {
                batch_cmds.push_back("keyword monitor " + m.name + "," + m.mode + ",auto," + std::to_string(m.scale) + ",mirror," + m.mirror_source);
            } else {
                std::string pos = m.position.empty() ? "auto" : m.position;
                batch_cmds.push_back("keyword monitor " + m.name + "," + m.mode + "," + pos + "," + std::to_string(m.scale));
            }
        }
        HyprIPC::batch(batch_cmds);
    }

    void update_waybar() {
        std::string conf_path = config_dir() + "/waybar/config.jsonc";
        try {
            std::ifstream f(conf_path);
            nlohmann::json wb_j;
            if (f.is_open()) {
                f >> wb_j;
                f.close();
            }

            wb_j["position"] = waybar.position;
            wb_j["height"] = waybar.height;

            wb_j["modules-left"] = {
                "custom/menu",
                "hyprland/workspaces",
                "custom/spatial_tab",
                "hyprland/window"
            };

            wb_j["custom/spatial_tab"] = {
                {"exec", "~/.config/monolith/scripts/waybar-spatial-tab.sh"},
                {"format", "{}"},
                {"interval", 2},
                {"on-click", "~/.config/monolith/scripts/spatial-tab.sh up"},
                {"on-click-right", "~/.config/monolith/scripts/spatial-tab.sh down"},
                {"on-scroll-up", "~/.config/monolith/scripts/spatial-tab.sh up"},
                {"on-scroll-down", "~/.config/monolith/scripts/spatial-tab.sh down"},
                {"return-type", "json"},
                {"signal", 8}
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
            if (waybar.drawer_enabled) {
                wb_j["custom/drawer_toggle"] = {
                    {"format", "󰍜"},
                    {"tooltip", "System Modules Drawer (Click to expand/collapse)"}
                };
                wb_j["group/drawer"] = {
                    {"orientation", "horizontal"},
                    {"drawer", {
                        {"transition-duration", 220},
                        {"children-class", "drawer-child"},
                        {"transition-left-to-right", false},
                        {"click-to-reveal", true}
                    }},
                    {"modules", drawer_mods}
                };
                right.push_back("group/drawer");
            } else {
                for (const auto& m : drawer_mods) {
                    if (m != "custom/drawer_toggle") right.push_back(m);
                }
            }
            right.push_back("custom/settings");
            if (waybar.show_notifications) right.push_back("custom/notification");
            if (waybar.show_power) right.push_back("custom/power");

            wb_j["modules-right"] = right;

            std::ofstream out(conf_path);
            if (out.is_open()) out << wb_j.dump(2) << std::endl;
        } catch (...) {}

        // Regenerate Waybar CSS with architectural pill styling and state colors
        std::string css_path = config_dir() + "/waybar/style.css";
        std::ostringstream css;
        css << "* {\n"
            << "    border: none;\n"
            << "    border-radius: 6px;\n"
            << "    font-family: \"JetBrains Mono\", \"JetBrainsMono Nerd Font\", monospace;\n"
            << "    font-weight: 600;\n"
            << "    font-size: " << waybar.font_size << "px;\n"
            << "    min-height: 0;\n"
            << "}\n\n"
            << "window#waybar {\n"
            << "    background: " << waybar.bg_color << ";\n"
            << "    color: " << waybar.text_color << ";\n"
            << "    border-bottom: 1px solid " << waybar.border_color << ";\n"
            << "}\n\n"
            << "#custom-menu {\n"
            << "    background: #09090c;\n"
            << "    border: 1px solid #1d1d22;\n"
            << "    border-radius: 7px;\n"
            << "    padding: 1px 10px;\n"
            << "    margin: 3px 4px 3px 6px;\n"
            << "    font-size: " << (waybar.font_size + 2) << "px;\n"
            << "    color: #ffffff;\n"
            << "    transition: all 120ms ease;\n"
            << "}\n"
            << "#custom-menu:hover {\n"
            << "    background: " << waybar.active_ws_bg << ";\n"
            << "    border-color: #3f3f46;\n"
            << "    color: #ffffff;\n"
            << "}\n\n"
            << "#workspaces {\n"
            << "    background: #09090c;\n"
            << "    border: 1px solid #18181d;\n"
            << "    border-radius: 7px;\n"
            << "    padding: 1px 3px;\n"
            << "    margin: 3px 4px;\n"
            << "}\n\n"
            << "#workspaces button {\n"
            << "    padding: 1px 8px;\n"
            << "    background: transparent;\n"
            << "    color: " << waybar.inactive_ws_text << ";\n"
            << "    border-radius: 5px;\n"
            << "    margin: 1px 1px;\n"
            << "    transition: all 120ms ease;\n"
            << "}\n"
            << "#workspaces button:hover {\n"
            << "    background: #141418;\n"
            << "    color: #e4e4e7;\n"
            << "    border-radius: 5px;\n"
            << "}\n"
            << "#workspaces button.active {\n"
            << "    background: " << waybar.active_ws_bg << ";\n"
            << "    color: " << waybar.active_ws_text << ";\n"
            << "    border-radius: 5px;\n"
            << "}\n\n"
            << "#custom-spatial_tab {\n"
            << "    padding: 1px 9px;\n"
            << "    margin: 3px 4px;\n"
            << "    font-size: " << std::max(10, waybar.font_size - 1) << "px;\n"
            << "    font-weight: 700;\n"
            << "    letter-spacing: 0.5px;\n"
            << "    border-radius: 7px;\n"
            << "    background: #09090c;\n"
            << "    border: 1px solid #1e1e24;\n"
            << "    color: #a1a1aa;\n"
            << "    transition: all 120ms ease;\n"
            << "}\n"
            << "#custom-spatial_tab:hover {\n"
            << "    border-color: #3f3f46;\n"
            << "    color: #ffffff;\n"
            << "}\n"
            << "#custom-spatial_tab.tab-spotify {\n"
            << "    background: #07190e;\n"
            << "    border-color: #1db954;\n"
            << "    color: #4ade80;\n"
            << "}\n"
            << "#custom-spatial_tab.tab-extra {\n"
            << "    background: #1c1205;\n"
            << "    border-color: #d97706;\n"
            << "    color: #fbbf24;\n"
            << "}\n"
            << "#custom-spatial_tab.tab-desktop {\n"
            << "    background: #09090c;\n"
            << "    color: #d4d4d8;\n"
            << "}\n\n"
            << "#window {\n"
            << "    color: #71717a;\n"
            << "    font-weight: 500;\n"
            << "    padding: 0 6px;\n"
            << "}\n\n"
            << "#clock {\n"
            << "    color: #ffffff;\n"
            << "    font-weight: 700;\n"
            << "    letter-spacing: 0.6px;\n"
            << "    background: #09090c;\n"
            << "    border: 1px solid #1a1a20;\n"
            << "    border-radius: 7px;\n"
            << "    padding: 1px 13px;\n"
            << "    margin: 3px 0;\n"
            << "    transition: all 120ms ease;\n"
            << "}\n"
            << "#clock:hover {\n"
            << "    background: #141418;\n"
            << "    border-color: #27272e;\n"
            << "}\n\n"
            << "#custom-drawer_toggle {\n"
            << "    padding: 1px 8px;\n"
            << "    margin: 3px 2px;\n"
            << "    color: " << waybar.text_color << ";\n"
            << "    background: " << waybar.drawer_btn_bg << ";\n"
            << "    border: 1px solid " << waybar.drawer_btn_border << ";\n"
            << "    border-radius: 6px;\n"
            << "    transition: all 120ms ease;\n"
            << "}\n"
            << "#custom-drawer_toggle:hover {\n"
            << "    background: #18181c;\n"
            << "    border-color: #3f3f46;\n"
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
            << "    padding: 1px 8px;\n"
            << "    margin: 3px 1px;\n"
            << "    color: " << waybar.text_color << ";\n"
            << "    background: transparent;\n"
            << "    border: 1px solid transparent;\n"
            << "    border-radius: 6px;\n"
            << "    transition: all 120ms ease;\n"
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
            << "#custom-notification:hover {\n"
            << "    background: #141418;\n"
            << "    border-color: #27272e;\n"
            << "    color: #ffffff;\n"
            << "}\n\n"
            << "#idle_inhibitor.activated {\n"
            << "    color: #38bdf8;\n"
            << "    background: #071722;\n"
            << "    border-color: #0369a1;\n"
            << "}\n"
            << "#pulseaudio.muted {\n"
            << "    color: #52525b;\n"
            << "}\n"
            << "#network.disconnected {\n"
            << "    color: #52525b;\n"
            << "}\n"
            << "#battery.charging, #battery.plugged {\n"
            << "    color: #4ade80;\n"
            << "}\n"
            << "#battery.warning:not(.charging) {\n"
            << "    color: #fbbf24;\n"
            << "}\n"
            << "#battery.critical:not(.charging) {\n"
            << "    color: #f87171;\n"
            << "    background: #220808;\n"
            << "    border-color: #7f1d1d;\n"
            << "}\n\n"
            << "#custom-power {\n"
            << "    margin-right: 6px;\n"
            << "}\n"
            << "#custom-power:hover {\n"
            << "    background: #250909;\n"
            << "    border-color: #7f1d1d;\n"
            << "    color: #f87171;\n"
            << "}\n\n"
            << "tooltip {\n"
            << "    background: #050507;\n"
            << "    border: 1px solid #27272e;\n"
            << "    border-radius: 8px;\n"
            << "    padding: 3px 5px;\n"
            << "    box-shadow: 0 6px 16px rgba(0, 0, 0, 0.85);\n"
            << "}\n"
            << "tooltip label {\n"
            << "    font-family: \"JetBrains Mono\", monospace;\n"
            << "    font-size: 11px;\n"
            << "    color: #e4e4e7;\n"
            << "    padding: 3px 6px;\n"
            << "}\n"
            << ".drawer-child {\n"
            << "    padding: 1px 6px;\n"
            << "    margin: 2px 1px;\n"
            << "    border-radius: 5px;\n"
            << "}\n";

        std::ofstream css_out(css_path);
        if (css_out.is_open()) css_out << css.str();

        // Silent in-place reload of Waybar
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
