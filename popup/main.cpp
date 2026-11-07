#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <algorithm>
#include <map>
#include <set>
#include <functional>

// ══════════════════════════════════════════════════════════════════════════════
// MONOLITH POPUP SUITE - NATIVE CONTROL PANELS (GTK4 Layer Shell)
// Microsecond startup, completely asynchronous, non-blocking UI
// Panels:
//   - Bluetooth (Live scanning, device list, pair/connect/disconnect inline)
//   - Network (Instant Wi-Fi list, inline password entry, quick connect)
//   - Audio (Output & input microphone sliders, live mute toggles)
//   - System Resources (CPU/RAM/Disk live gauges, top processes)
//   - Power Menu (Lock, Suspend, Reboot, Power Off, Log Out)
// ══════════════════════════════════════════════════════════════════════════════

struct ModuleSpec {
    std::string id;
    std::string title;
    std::string icon;
    int width = 380;
    int height = 380;
    int margin_right = 100;
    double icon_rel_notch = 200.0;
    std::function<GtkWidget*()> build_func = nullptr;
};

static std::map<std::string, ModuleSpec> g_specs;
static std::vector<std::string> g_module_order = {
    "bluetooth", "network", "audio", "backlight", "battery", "system", "power"
};
static GtkApplication* g_app = nullptr;

// Unified morphing window state
static GtkWidget* g_morph_win = nullptr;
static GtkWidget* g_morph_revealer = nullptr;
static GtkWidget* g_morph_panel = nullptr;
static GtkWidget* g_morph_notch_da = nullptr;
static GtkWidget* g_morph_title_lbl = nullptr;
static GtkWidget* g_morph_pin_btn = nullptr;
static GtkWidget* g_morph_stack = nullptr;

static std::string g_active_id = "network";
static bool g_is_open = false;
static bool g_is_pinned = false;
static bool g_cursor_in_window = false;

static guint g_leave_timer = 0;
static guint g_hide_timer = 0;
static guint g_morph_tick_id = 0;

static double g_curr_margin_right = 180.0;
static double g_target_margin_right = 180.0;
static double g_curr_width = 380.0;
static double g_target_width = 380.0;
static double g_curr_height = 440.0;
static double g_target_height = 440.0;
static double g_curr_notch_x = 210.0;
static double g_target_notch_x = 210.0;

static std::atomic<bool> g_bt_scanning{false};
static guint g_sys_refresh_timer = 0;
static guint g_bt_refresh_timer = 0;

// Forward declarations
static void show_morph_window(const std::string& id, bool pin);
static void morph_to_module(const std::string& id);
static void hide_morph_window();
static void toggle_module(const std::string& id, bool pin);
static void show_module(const std::string& id, bool pin);
static void hide_module(const std::string& id = "");
static void hide_all_unpinned();
static void update_taskgraph_ipc();
static void schedule_leave_check();
static void update_pin_button_ui();

// Synchronously run a quick command and capture stdout
static std::string run_cmd(const std::string& cmd) {
    char buf[512];
    std::string out = "";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "";
    while (fgets(buf, sizeof(buf), fp) != nullptr) {
        out += buf;
    }
    pclose(fp);
    return out;
}

// Fire and forget shell command asynchronously
static void exec_async(const std::string& cmd) {
    std::string full_cmd = cmd + " >/dev/null 2>&1 &";
    system(full_cmd.c_str());
}

static void close_popup() {
    hide_all_unpinned();
}

// ──────────────────────────────────────────────────────────────────────────────
// CSS STYLING (Pitch-Black OLED, Brutalist 1px Border, JetBrains Mono)
// ──────────────────────────────────────────────────────────────────────────────
static void apply_css() {
    const char* css = R"(
        window.monolith-popup {
            background: transparent;
            background-color: transparent;
            border: none;
            box-shadow: none;
        }

        window.bar-trigger {
            background: transparent;
            background-color: transparent;
            border: none;
            box-shadow: none;
        }

        .growing-bar-panel {
            background-color: #000000;
            border-left: 1px solid #27272a;
            border-right: 1px solid #27272a;
            border-bottom: 1px solid #27272a;
            border-top: none;
            border-bottom-left-radius: 12px;
            border-bottom-right-radius: 12px;
            box-shadow: 0 20px 48px rgba(0, 0, 0, 0.95);
            padding: 2px 14px 14px 14px;
        }

        .popup-card {
            background-color: #000000;
            border: none;
            padding: 0;
        }

        .popup-box {
            background-color: #050508;
            padding: 0;
        }

        .popup-header {
            border-bottom: 1px solid #18181b;
            padding-bottom: 8px;
            margin-bottom: 10px;
        }

        .popup-title {
            font-family: "JetBrains Mono", monospace;
            font-size: 13px;
            font-weight: 700;
            color: #ffffff;
            letter-spacing: 0.5px;
        }

        .popup-btn-icon {
            background: transparent;
            color: #71717a;
            border: none;
            border-radius: 4px;
            padding: 3px 7px;
            font-size: 13px;
            font-family: "JetBrains Mono", monospace;
        }
        .popup-btn-icon:hover {
            background: #18181b;
            color: #ffffff;
        }
        .popup-btn-icon.pinned {
            background: #18181b;
            color: #38bdf8;
            border: 1px solid #0284c7;
        }

        .section-header {
            font-family: "JetBrains Mono", monospace;
            font-size: 10px;
            font-weight: 700;
            color: #71717a;
            letter-spacing: 0.8px;
            text-transform: uppercase;
            margin-top: 8px;
            margin-bottom: 4px;
        }

        .section-label {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            font-weight: 600;
            color: #a1a1aa;
        }

        .value-label {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            font-weight: 500;
            color: #ffffff;
        }

        .dim-label {
            font-family: "JetBrains Mono", monospace;
            font-size: 10px;
            color: #71717a;
        }

        .status-badge {
            font-family: "JetBrains Mono", monospace;
            font-size: 10px;
            font-weight: 600;
            padding: 2px 7px;
            border-radius: 5px;
        }
        .status-connected {
            color: #4ade80;
            background: rgba(34, 197, 94, 0.12);
            border: 1px solid rgba(34, 197, 94, 0.25);
        }
        .status-disconnected {
            color: #71717a;
            background: #121214;
        }
        .status-scanning {
            color: #38bdf8;
            background: rgba(56, 189, 248, 0.12);
            border: 1px solid rgba(56, 189, 248, 0.25);
        }

        .item-row {
            background: #09090b;
            border: 1px solid #18181b;
            border-radius: 6px;
            padding: 6px 10px;
            margin-bottom: 4px;
            transition: all 120ms ease;
        }
        .item-row:hover {
            border-color: #27272a;
            background: #111114;
        }
        .item-row-connected {
            border-color: #1f3624;
            background: #09120c;
        }

        .action-btn {
            background: #18181b;
            border: 1px solid #27272a;
            color: #e4e4e7;
            border-radius: 6px;
            padding: 4px 10px;
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            font-weight: 600;
        }
        .action-btn:hover {
            background: #27272a;
            color: #ffffff;
            border-color: #3f3f46;
        }
        .action-btn:disabled {
            color: #52525b;
            border-color: #18181b;
            background: #09090b;
        }

        .action-btn-primary {
            background: #ffffff;
            border: 1px solid #ffffff;
            color: #000000;
            font-weight: 700;
        }
        .action-btn-primary:hover {
            background: #e4e4e7;
            border-color: #e4e4e7;
            color: #000000;
        }

        .btn-action {
            background: #18181b;
            border: 1px solid #27272a;
            color: #e4e4e7;
            border-radius: 6px;
            padding: 6px 10px;
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            font-weight: 600;
        }
        .btn-action:hover {
            background: #27272a;
            color: #ffffff;
            border-color: #3f3f46;
        }
        .btn-action-primary {
            background: #ffffff;
            border: 1px solid #ffffff;
            color: #000000;
            font-weight: 700;
        }
        .btn-action-primary:hover {
            background: #e4e4e7;
            color: #000000;
        }

        .action-btn-danger {
            background: #200808;
            border: 1px solid #551515;
            color: #f87171;
        }
        .action-btn-danger:hover {
            background: #350c0c;
            border-color: #ef4444;
            color: #ffffff;
        }

        scale trough {
            background-color: #18181b;
            border-radius: 3px;
            min-height: 6px;
        }
        scale highlight {
            background-color: #ffffff;
            border-radius: 3px;
        }
        scale slider {
            min-width: 14px;
            min-height: 14px;
            border-radius: 7px;
            background-color: #ffffff;
            border: 1px solid #a1a1aa;
        }

        progressbar trough {
            background-color: #18181b;
            border-radius: 2px;
            min-height: 6px;
        }
        progressbar progress {
            background-color: #e4e4e7;
            border-radius: 2px;
        }

        entry {
            background-color: #09090b;
            border: 1px solid #27272a;
            border-radius: 4px;
            color: #ffffff;
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            padding: 5px 8px;
        }
        entry:focus {
            border-color: #38bdf8;
            outline: none;
        }

        scrolledwindow {
            border: none;
        }
        scrollbar {
            background: transparent;
        }
        scrollbar trough {
            background: transparent;
        }
        scrollbar slider {
            background-color: #27272a;
            border-radius: 4px;
            min-width: 5px;
            min-height: 5px;
        }
        scrollbar slider:hover {
            background-color: #3f3f46;
        }
    )";

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 1: AUDIO (PipeWire / WirePlumber Volume & Mute)
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_audio_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    // --- Output (Sinks) ---
    std::string out_vol_str = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null");
    double out_vol = 0.5;
    bool out_muted = (out_vol_str.find("[MUTED]") != std::string::npos);
    size_t pos_out = out_vol_str.find("Volume: ");
    if (pos_out != std::string::npos) {
        try { out_vol = std::stod(out_vol_str.substr(pos_out + 8, 4)); } catch (...) {}
    }

    GtkWidget* out_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_out_title = gtk_label_new("󰕾 Audio Output");
    gtk_widget_add_css_class(lbl_out_title, "section-label");
    gtk_widget_set_hexpand(lbl_out_title, TRUE);
    gtk_widget_set_halign(lbl_out_title, GTK_ALIGN_START);

    int out_pct = static_cast<int>(std::round(out_vol * 100.0));
    std::string out_pct_txt = std::to_string(out_pct) + "%" + (out_muted ? " [MUTED]" : "");
    GtkWidget* lbl_out_pct = gtk_label_new(out_pct_txt.c_str());
    gtk_widget_add_css_class(lbl_out_pct, "value-label");

    gtk_box_append(GTK_BOX(out_hdr), lbl_out_title);
    gtk_box_append(GTK_BOX(out_hdr), lbl_out_pct);
    gtk_box_append(GTK_BOX(box), out_hdr);

    GtkAdjustment* adj_out = gtk_adjustment_new(out_vol * 100.0, 0.0, 150.0, 1.0, 5.0, 0.0);
    GtkWidget* scale_out = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj_out);
    gtk_scale_set_draw_value(GTK_SCALE(scale_out), FALSE);
    gtk_box_append(GTK_BOX(box), scale_out);

    g_signal_connect(adj_out, "value-changed", G_CALLBACK(+[](GtkAdjustment* a, gpointer p) {
        double v = gtk_adjustment_get_value(a) / 100.0;
        int p_val = static_cast<int>(std::round(v * 100.0));
        std::string txt = std::to_string(p_val) + "%";
        gtk_label_set_text(GTK_LABEL(p), txt.c_str());
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << v;
        std::string cmd = "wpctl set-volume @DEFAULT_AUDIO_SINK@ " + ss.str();
        system(cmd.c_str());
    }), lbl_out_pct);

    GtkWidget* out_btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* btn_out_mute = gtk_button_new_with_label(out_muted ? "󰖁 Unmute Output" : "󰕾 Mute Output");
    gtk_widget_add_css_class(btn_out_mute, "action-btn");
    gtk_widget_set_hexpand(btn_out_mute, TRUE);
    g_signal_connect(btn_out_mute, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        system("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
        std::string s = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null");
        bool muted = (s.find("[MUTED]") != std::string::npos);
        gtk_button_set_label(b, muted ? "󰖁 Unmute Output" : "󰕾 Mute Output");
    }), nullptr);
    gtk_box_append(GTK_BOX(out_btn_row), btn_out_mute);
    gtk_box_append(GTK_BOX(box), out_btn_row);

    // --- Separator ---
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    // --- Input (Microphone) ---
    std::string mic_vol_str = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null");
    double mic_vol = 0.8;
    bool mic_muted = (mic_vol_str.find("[MUTED]") != std::string::npos);
    size_t pos_mic = mic_vol_str.find("Volume: ");
    if (pos_mic != std::string::npos) {
        try { mic_vol = std::stod(mic_vol_str.substr(pos_mic + 8, 4)); } catch (...) {}
    }

    GtkWidget* mic_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_mic_title = gtk_label_new("󰍬 Audio Input");
    gtk_widget_add_css_class(lbl_mic_title, "section-label");
    gtk_widget_set_hexpand(lbl_mic_title, TRUE);
    gtk_widget_set_halign(lbl_mic_title, GTK_ALIGN_START);

    int mic_pct = static_cast<int>(std::round(mic_vol * 100.0));
    std::string mic_pct_txt = std::to_string(mic_pct) + "%" + (mic_muted ? " [MUTED]" : "");
    GtkWidget* lbl_mic_pct = gtk_label_new(mic_pct_txt.c_str());
    gtk_widget_add_css_class(lbl_mic_pct, "value-label");

    gtk_box_append(GTK_BOX(mic_hdr), lbl_mic_title);
    gtk_box_append(GTK_BOX(mic_hdr), lbl_mic_pct);
    gtk_box_append(GTK_BOX(box), mic_hdr);

    GtkAdjustment* adj_mic = gtk_adjustment_new(mic_vol * 100.0, 0.0, 100.0, 1.0, 5.0, 0.0);
    GtkWidget* scale_mic = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj_mic);
    gtk_scale_set_draw_value(GTK_SCALE(scale_mic), FALSE);
    gtk_box_append(GTK_BOX(box), scale_mic);

    g_signal_connect(adj_mic, "value-changed", G_CALLBACK(+[](GtkAdjustment* a, gpointer p) {
        double v = gtk_adjustment_get_value(a) / 100.0;
        int p_val = static_cast<int>(std::round(v * 100.0));
        std::string txt = std::to_string(p_val) + "%";
        gtk_label_set_text(GTK_LABEL(p), txt.c_str());
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << v;
        std::string cmd = "wpctl set-volume @DEFAULT_AUDIO_SOURCE@ " + ss.str();
        system(cmd.c_str());
    }), lbl_mic_pct);

    GtkWidget* mic_btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* btn_mic_mute = gtk_button_new_with_label(mic_muted ? "󰍭 Unmute Mic" : "󰍬 Mute Mic");
    gtk_widget_add_css_class(btn_mic_mute, "action-btn");
    gtk_widget_set_hexpand(btn_mic_mute, TRUE);
    g_signal_connect(btn_mic_mute, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        system("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle");
        std::string s = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null");
        bool muted = (s.find("[MUTED]") != std::string::npos);
        gtk_button_set_label(b, muted ? "󰍭 Unmute Mic" : "󰍬 Mute Mic");
    }), nullptr);

    GtkWidget* btn_pavu = gtk_button_new_with_label("󰒓 Audio Mixer");
    gtk_widget_add_css_class(btn_pavu, "action-btn");
    gtk_widget_set_hexpand(btn_pavu, TRUE);
    g_signal_connect(btn_pavu, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        exec_async("pavucontrol");
        close_popup();
    }), nullptr);

    gtk_box_append(GTK_BOX(mic_btn_row), btn_mic_mute);
    gtk_box_append(GTK_BOX(mic_btn_row), btn_pavu);
    gtk_box_append(GTK_BOX(box), mic_btn_row);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 2: NETWORK (Wi-Fi Manager with Inline Password & One-Click Connect)
// ──────────────────────────────────────────────────────────────────────────────
struct WifiItem {
    bool in_use = false;
    std::string ssid;
    int signal = 0;
    std::string security;
    std::string bars;
};

static GtkWidget* g_wifi_list_box = nullptr;
static GtkWidget* g_wifi_active_box = nullptr;
static GtkWidget* g_wifi_status_lbl = nullptr;

static std::string get_wifi_interface() {
    std::string dev = run_cmd("nmcli -t -f DEVICE,TYPE device status 2>/dev/null | grep ':wifi$' | head -n 1 | cut -d: -f1");
    while (!dev.empty() && (dev.back() == '\n' || dev.back() == ' ' || dev.back() == '\r')) dev.pop_back();
    return dev.empty() ? "wlp1s0" : dev;
}

static std::vector<WifiItem> fetch_wifi_networks() {
    std::vector<WifiItem> items;
    // Always use --rescan no for microsecond response!
    std::string raw = run_cmd("nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY,BARS device wifi list --rescan no 2>/dev/null");
    std::istringstream iss(raw);
    std::string line;
    std::map<std::string, WifiItem> unique_map;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, ':')) {
            parts.push_back(part);
        }
        if (parts.size() < 4) continue;

        bool in_use = (parts[0] == "*");
        std::string ssid = parts[1];
        if (ssid.empty()) continue;

        int sig = 0;
        try { sig = std::stoi(parts[2]); } catch (...) {}
        std::string sec = parts[3];
        std::string bars = (parts.size() > 4) ? parts[4] : "";

        // If duplicate SSID, keep the one currently in use or with highest signal
        if (unique_map.find(ssid) == unique_map.end() || in_use || sig > unique_map[ssid].signal) {
            unique_map[ssid] = {in_use, ssid, sig, sec, bars};
        }
    }

    for (const auto& pair : unique_map) {
        items.push_back(pair.second);
    }

    // Sort: connected first, then by signal descending
    std::sort(items.begin(), items.end(), [](const WifiItem& a, const WifiItem& b) {
        if (a.in_use != b.in_use) return a.in_use;
        return a.signal > b.signal;
    });

    return items;
}

static std::set<std::string> get_saved_connections() {
    std::set<std::string> conns;
    std::string raw = run_cmd("nmcli -t -f NAME connection show 2>/dev/null");
    std::istringstream iss(raw);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) conns.insert(line);
    }
    return conns;
}

static void refresh_wifi_ui();

static void render_wifi_items(const std::vector<WifiItem>& items) {
    if (!g_wifi_list_box || !g_wifi_active_box) return;

    // Clear active card
    GtkWidget* c1 = gtk_widget_get_first_child(g_wifi_active_box);
    while (c1) {
        GtkWidget* nxt = gtk_widget_get_next_sibling(c1);
        gtk_box_remove(GTK_BOX(g_wifi_active_box), c1);
        c1 = nxt;
    }

    // Clear list
    GtkWidget* c2 = gtk_widget_get_first_child(g_wifi_list_box);
    while (c2) {
        GtkWidget* nxt = gtk_widget_get_next_sibling(c2);
        gtk_box_remove(GTK_BOX(g_wifi_list_box), c2);
        c2 = nxt;
    }

    auto saved = get_saved_connections();
    bool has_connected = false;

    for (const auto& item : items) {
        if (item.in_use) {
            has_connected = true;
            // Build Active Connection Card
            GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_add_css_class(card, "item-row");
            gtk_widget_add_css_class(card, "item-row-connected");

            GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            std::string t_text = "󰖩 " + item.ssid;
            GtkWidget* lbl_t = gtk_label_new(t_text.c_str());
            gtk_widget_add_css_class(lbl_t, "value-label");
            gtk_widget_set_hexpand(lbl_t, TRUE);
            gtk_widget_set_halign(lbl_t, GTK_ALIGN_START);

            GtkWidget* badge = gtk_label_new("CONNECTED");
            gtk_widget_add_css_class(badge, "status-badge");
            gtk_widget_add_css_class(badge, "status-connected");

            GtkWidget* btn_disc = gtk_button_new_with_label("Disconnect");
            gtk_widget_add_css_class(btn_disc, "action-btn");
            gtk_widget_add_css_class(btn_disc, "action-btn-danger");

            auto* p_ssid = new std::string(item.ssid);
            g_signal_connect_data(btn_disc, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto* s = static_cast<std::string*>(p);
                std::string dev = get_wifi_interface();
                std::string cmd = "nmcli connection down id \"" + *s + "\" 2>/dev/null || nmcli device disconnect " + dev + " 2>/dev/null";
                system(cmd.c_str());
                refresh_wifi_ui();
            }), p_ssid, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);

            gtk_box_append(GTK_BOX(top), lbl_t);
            gtk_box_append(GTK_BOX(top), badge);
            gtk_box_append(GTK_BOX(top), btn_disc);
            gtk_box_append(GTK_BOX(card), top);

            // Sub info: IP and Signal
            std::string dev = get_wifi_interface();
            std::string ip_out = run_cmd("nmcli -g IP4.ADDRESS dev show " + dev + " 2>/dev/null | head -n 1");
            if (ip_out.empty()) ip_out = run_cmd("hostname -I 2>/dev/null | awk '{print $1}'");
            if (!ip_out.empty() && ip_out.back() == '\n') ip_out.pop_back();

            std::string sub_txt = "IP: " + (ip_out.empty() ? "assigning..." : ip_out) +
                                  " │ Signal: " + std::to_string(item.signal) + "% (" + item.security + ")";
            GtkWidget* lbl_sub = gtk_label_new(sub_txt.c_str());
            gtk_widget_add_css_class(lbl_sub, "dim-label");
            gtk_widget_set_halign(lbl_sub, GTK_ALIGN_START);
            gtk_box_append(GTK_BOX(card), lbl_sub);

            gtk_box_append(GTK_BOX(g_wifi_active_box), card);
        } else {
            // Available network row
            GtkWidget* container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_add_css_class(row, "item-row");

            // Signal icon
            std::string sig_icon = "󰤟 ";
            if (item.signal >= 75) sig_icon = "󰤨 ";
            else if (item.signal >= 50) sig_icon = "󰤥 ";
            else if (item.signal >= 25) sig_icon = "󰤢 ";

            std::string sec_icon = (item.security.empty() || item.security == "--") ? "" : " 󰌾";
            std::string display_name = sig_icon + item.ssid + sec_icon;

            GtkWidget* lbl = gtk_label_new(display_name.c_str());
            gtk_widget_add_css_class(lbl, "value-label");
            gtk_widget_set_hexpand(lbl, TRUE);
            gtk_widget_set_halign(lbl, GTK_ALIGN_START);

            std::string sig_pct_txt = std::to_string(item.signal) + "%";
            GtkWidget* lbl_sig = gtk_label_new(sig_pct_txt.c_str());
            gtk_widget_add_css_class(lbl_sig, "dim-label");

            GtkWidget* btn_conn = gtk_button_new_with_label("Connect");
            gtk_widget_add_css_class(btn_conn, "action-btn");

            // Inline password expansion box (hidden by default)
            GtkWidget* pass_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_set_visible(pass_box, FALSE);
            gtk_widget_set_margin_start(pass_box, 12);
            gtk_widget_set_margin_end(pass_box, 4);
            gtk_widget_set_margin_bottom(pass_box, 4);

            GtkWidget* entry_pass = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(entry_pass), "Enter password...");
            gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
            gtk_widget_set_hexpand(entry_pass, TRUE);

            GtkWidget* btn_pass_conn = gtk_button_new_with_label("Connect");
            gtk_widget_add_css_class(btn_pass_conn, "action-btn action-btn-primary");

            GtkWidget* btn_pass_cancel = gtk_button_new_with_label("Cancel");
            gtk_widget_add_css_class(btn_pass_cancel, "action-btn");

            gtk_box_append(GTK_BOX(pass_box), entry_pass);
            gtk_box_append(GTK_BOX(pass_box), btn_pass_conn);
            gtk_box_append(GTK_BOX(pass_box), btn_pass_cancel);

            bool is_saved = (saved.find(item.ssid) != saved.end());
            bool is_open = (item.security.empty() || item.security == "--");

            struct ConnectContext {
                std::string ssid;
                bool is_saved;
                bool is_open;
                GtkWidget* pass_box;
                GtkWidget* entry_pass;
                GtkWidget* btn_conn;
            };
            auto* ctx = new ConnectContext{item.ssid, is_saved, is_open, pass_box, entry_pass, btn_conn};
            g_object_set_data_full(G_OBJECT(container), "conn_ctx", ctx, [](gpointer p) {
                delete static_cast<ConnectContext*>(p);
            });

            g_signal_connect(btn_conn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto* c = static_cast<ConnectContext*>(p);
                if (c->is_saved) {
                    gtk_widget_set_sensitive(c->btn_conn, FALSE);
                    if (g_wifi_status_lbl) gtk_label_set_text(GTK_LABEL(g_wifi_status_lbl), ("Connecting to " + c->ssid + "...").c_str());
                    std::thread([ssid = c->ssid]() {
                        system(("nmcli connection up id \"" + ssid + "\" 2>/dev/null").c_str());
                        g_idle_add(+[](gpointer) -> gboolean {
                            refresh_wifi_ui();
                            return G_SOURCE_REMOVE;
                        }, nullptr);
                    }).detach();
                } else if (c->is_open) {
                    gtk_widget_set_sensitive(c->btn_conn, FALSE);
                    if (g_wifi_status_lbl) gtk_label_set_text(GTK_LABEL(g_wifi_status_lbl), ("Connecting to " + c->ssid + "...").c_str());
                    std::thread([ssid = c->ssid]() {
                        system(("nmcli device wifi connect \"" + ssid + "\" 2>/dev/null").c_str());
                        g_idle_add(+[](gpointer) -> gboolean {
                            refresh_wifi_ui();
                            return G_SOURCE_REMOVE;
                        }, nullptr);
                    }).detach();
                } else {
                    // Requires password -> reveal entry
                    gtk_widget_set_visible(c->pass_box, TRUE);
                    gtk_widget_grab_focus(c->entry_pass);
                }
            }), ctx);

            // Connect button inside password box
            g_signal_connect(btn_pass_conn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto* c = static_cast<ConnectContext*>(p);
                const char* pass_text = gtk_editable_get_text(GTK_EDITABLE(c->entry_pass));
                std::string pass = pass_text ? pass_text : "";
                if (pass.empty()) return;

                gtk_widget_set_sensitive(c->btn_conn, FALSE);
                gtk_widget_set_visible(c->pass_box, FALSE);
                if (g_wifi_status_lbl) gtk_label_set_text(GTK_LABEL(g_wifi_status_lbl), ("Connecting to " + c->ssid + "...").c_str());

                std::thread([ssid = c->ssid, pass]() {
                    std::string cmd = "nmcli device wifi connect \"" + ssid + "\" password \"" + pass + "\" 2>&1";
                    std::string res = run_cmd(cmd);
                    g_idle_add(+[](gpointer) -> gboolean {
                        refresh_wifi_ui();
                        return G_SOURCE_REMOVE;
                    }, nullptr);
                }).detach();
            }), ctx);

            // Enter key in entry
            g_signal_connect(entry_pass, "activate", G_CALLBACK(+[](GtkEntry*, gpointer b) {
                g_signal_emit_by_name(b, "clicked");
            }), btn_pass_conn);

            // Cancel button
            g_signal_connect_data(btn_pass_cancel, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto* c = static_cast<ConnectContext*>(p);
                gtk_widget_set_visible(c->pass_box, FALSE);
            }), ctx, [](gpointer p, GClosure*) { delete static_cast<ConnectContext*>(p); }, (GConnectFlags)0);

            gtk_box_append(GTK_BOX(row), lbl);
            gtk_box_append(GTK_BOX(row), lbl_sig);
            gtk_box_append(GTK_BOX(row), btn_conn);

            gtk_box_append(GTK_BOX(container), row);
            gtk_box_append(GTK_BOX(container), pass_box);
            gtk_box_append(GTK_BOX(g_wifi_list_box), container);
        }
    }

    if (items.empty()) {
        GtkWidget* empty_lbl = gtk_label_new("No Wi-Fi networks found");
        gtk_widget_add_css_class(empty_lbl, "dim-label");
        gtk_widget_set_margin_top(empty_lbl, 10);
        gtk_box_append(GTK_BOX(g_wifi_list_box), empty_lbl);
    }

    if (g_wifi_status_lbl) {
        gtk_label_set_text(GTK_LABEL(g_wifi_status_lbl), has_connected ? "Connected to network" : "No active connection");
    }
}

static void refresh_wifi_ui() {
    std::thread([]() {
        auto items = fetch_wifi_networks();
        auto* p_items = new std::vector<WifiItem>(std::move(items));
        g_idle_add(+[](gpointer p) -> gboolean {
            auto* it = static_cast<std::vector<WifiItem>*>(p);
            render_wifi_items(*it);
            delete it;
            return G_SOURCE_REMOVE;
        }, p_items);
    }).detach();
}

static GtkWidget* build_network_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    std::string wifi_state = run_cmd("nmcli -t -f WIFI g 2>/dev/null");
    bool wifi_enabled = (wifi_state.find("enabled") != std::string::npos || wifi_state.find("włączone") != std::string::npos);

    // Header Controls: Switch + Rescan button
    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_t = gtk_label_new(wifi_enabled ? "Wi-Fi Adapter Enabled" : "Wi-Fi Adapter Disabled");
    gtk_widget_add_css_class(lbl_t, "section-label");
    gtk_widget_set_hexpand(lbl_t, TRUE);
    gtk_widget_set_halign(lbl_t, GTK_ALIGN_START);

    GtkWidget* btn_rescan = gtk_button_new_with_label("󰑐 Rescan");
    gtk_widget_add_css_class(btn_rescan, "action-btn");
    g_signal_connect(btn_rescan, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        if (g_wifi_status_lbl) gtk_label_set_text(GTK_LABEL(g_wifi_status_lbl), "Scanning networks...");
        std::thread([]() {
            system("nmcli device wifi rescan 2>/dev/null");
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            refresh_wifi_ui();
        }).detach();
    }), nullptr);

    GtkWidget* sw_wifi = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw_wifi), wifi_enabled);
    g_signal_connect(sw_wifi, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer) -> gboolean {
        std::string cmd = state ? "nmcli radio wifi on" : "nmcli radio wifi off";
        system(cmd.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        refresh_wifi_ui();
        return FALSE;
    }), nullptr);

    gtk_box_append(GTK_BOX(top_row), lbl_t);
    gtk_box_append(GTK_BOX(top_row), btn_rescan);
    gtk_box_append(GTK_BOX(top_row), sw_wifi);
    gtk_box_append(GTK_BOX(box), top_row);

    // Active Card Container
    g_wifi_active_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(box), g_wifi_active_box);

    // Status label
    g_wifi_status_lbl = gtk_label_new("Scanning Wi-Fi networks...");
    gtk_widget_add_css_class(g_wifi_status_lbl, "dim-label");
    gtk_widget_set_halign(g_wifi_status_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), g_wifi_status_lbl);

    // Available networks section header
    GtkWidget* lbl_avail = gtk_label_new("AVAILABLE NETWORKS");
    gtk_widget_add_css_class(lbl_avail, "section-header");
    gtk_widget_set_halign(lbl_avail, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl_avail);

    // Scrollable list
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 310);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);

    g_wifi_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), g_wifi_list_box);
    gtk_box_append(GTK_BOX(box), scroll);

    // Initial non-blocking load
    refresh_wifi_ui();

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 3: BLUETOOTH (Native Live Discovery, In-Window Pair & Connect)
// ──────────────────────────────────────────────────────────────────────────────
struct BtDevice {
    std::string mac;
    std::string name;
    bool is_paired = false;
    bool is_connected = false;
};

static GtkWidget* g_bt_paired_box = nullptr;
static GtkWidget* g_bt_avail_box = nullptr;
static GtkWidget* g_bt_status_lbl = nullptr;
static GtkWidget* g_bt_scan_btn = nullptr;

static std::vector<BtDevice> fetch_bluetooth_devices() {
    std::vector<BtDevice> devices;
    std::map<std::string, BtDevice> dev_map;

    // 1. Get paired devices
    std::string paired_raw = run_cmd("bluetoothctl devices Paired 2>/dev/null");
    {
        std::istringstream iss(paired_raw);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.rfind("Device ", 0) != 0) continue;
            std::stringstream ss(line);
            std::string kw, mac, name;
            ss >> kw >> mac;
            std::getline(ss >> std::ws, name);
            if (!mac.empty()) {
                dev_map[mac] = {mac, name.empty() ? mac : name, true, false};
            }
        }
    }

    // 2. Get connected devices
    std::string conn_raw = run_cmd("bluetoothctl devices Connected 2>/dev/null");
    {
        std::istringstream iss(conn_raw);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.rfind("Device ", 0) != 0) continue;
            std::stringstream ss(line);
            std::string kw, mac, name;
            ss >> kw >> mac;
            std::getline(ss >> std::ws, name);
            if (!mac.empty()) {
                if (dev_map.find(mac) != dev_map.end()) {
                    dev_map[mac].is_connected = true;
                } else {
                    dev_map[mac] = {mac, name.empty() ? mac : name, false, true};
                }
            }
        }
    }

    // 3. Get all cached/discovered devices
    std::string all_raw = run_cmd("bluetoothctl devices 2>/dev/null");
    {
        std::istringstream iss(all_raw);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.rfind("Device ", 0) != 0) continue;
            std::stringstream ss(line);
            std::string kw, mac, name;
            ss >> kw >> mac;
            std::getline(ss >> std::ws, name);
            if (!mac.empty()) {
                if (dev_map.find(mac) == dev_map.end()) {
                    dev_map[mac] = {mac, name.empty() ? mac : name, false, false};
                } else if (dev_map[mac].name == mac && !name.empty()) {
                    dev_map[mac].name = name;
                }
            }
        }
    }

    for (const auto& pair : dev_map) {
        devices.push_back(pair.second);
    }

    return devices;
}

static void refresh_bt_ui();

static void render_bt_items(const std::vector<BtDevice>& devices) {
    if (!g_bt_paired_box || !g_bt_avail_box) return;

    // Clear boxes
    GtkWidget* c1 = gtk_widget_get_first_child(g_bt_paired_box);
    while (c1) {
        GtkWidget* nxt = gtk_widget_get_next_sibling(c1);
        gtk_box_remove(GTK_BOX(g_bt_paired_box), c1);
        c1 = nxt;
    }

    GtkWidget* c2 = gtk_widget_get_first_child(g_bt_avail_box);
    while (c2) {
        GtkWidget* nxt = gtk_widget_get_next_sibling(c2);
        gtk_box_remove(GTK_BOX(g_bt_avail_box), c2);
        c2 = nxt;
    }

    int paired_count = 0;
    int avail_count = 0;

    for (const auto& dev : devices) {
        // Decide icon based on name
        std::string icon = "󰂯 ";
        std::string lower_name = dev.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (lower_name.find("head") != std::string::npos || lower_name.find("buds") != std::string::npos ||
            lower_name.find("wh-") != std::string::npos || lower_name.find("airpod") != std::string::npos) {
            icon = "󰋋 ";
        } else if (lower_name.find("mouse") != std::string::npos) {
            icon = "󰍽 ";
        } else if (lower_name.find("key") != std::string::npos) {
            icon = "󰌌 ";
        } else if (lower_name.find("phone") != std::string::npos || lower_name.find("iphone") != std::string::npos) {
            icon = "󰏲 ";
        }

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_add_css_class(row, "item-row");
        if (dev.is_connected) {
            gtk_widget_add_css_class(row, "item-row-connected");
        }

        GtkWidget* text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(text_box, TRUE);

        std::string title = icon + dev.name;
        GtkWidget* lbl_name = gtk_label_new(title.c_str());
        gtk_widget_add_css_class(lbl_name, "value-label");
        gtk_widget_set_halign(lbl_name, GTK_ALIGN_START);

        GtkWidget* lbl_mac = gtk_label_new(dev.mac.c_str());
        gtk_widget_add_css_class(lbl_mac, "dim-label");
        gtk_widget_set_halign(lbl_mac, GTK_ALIGN_START);

        gtk_box_append(GTK_BOX(text_box), lbl_name);
        gtk_box_append(GTK_BOX(text_box), lbl_mac);
        gtk_box_append(GTK_BOX(row), text_box);

        if (dev.is_paired) {
            paired_count++;

            // Status badge
            GtkWidget* badge = gtk_label_new(dev.is_connected ? "CONNECTED" : "DISCONNECTED");
            gtk_widget_add_css_class(badge, "status-badge");
            gtk_widget_add_css_class(badge, dev.is_connected ? "status-connected" : "status-disconnected");
            gtk_box_append(GTK_BOX(row), badge);

            // Connect / Disconnect button
            GtkWidget* btn_conn = gtk_button_new_with_label(dev.is_connected ? "Disconnect" : "Connect");
            gtk_widget_add_css_class(btn_conn, "action-btn");
            if (dev.is_connected) gtk_widget_add_css_class(btn_conn, "action-btn-danger");

            auto* p_mac = new std::string(dev.mac);
            g_signal_connect_data(btn_conn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer p) {
                auto* m = static_cast<std::string*>(p);
                gtk_widget_set_sensitive(GTK_WIDGET(b), FALSE);
                if (g_bt_status_lbl) gtk_label_set_text(GTK_LABEL(g_bt_status_lbl), "Connecting to device...");
                std::thread([mac = *m]() {
                    std::string info = run_cmd("bluetoothctl info " + mac);
                    if (info.find("Connected: yes") != std::string::npos) {
                        system(("bluetoothctl disconnect " + mac).c_str());
                    } else {
                        system(("bluetoothctl connect " + mac).c_str());
                    }
                    g_idle_add(+[](gpointer) -> gboolean {
                        refresh_bt_ui();
                        return G_SOURCE_REMOVE;
                    }, nullptr);
                }).detach();
            }), p_mac, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);
            gtk_box_append(GTK_BOX(row), btn_conn);

            // Remove / Forget button
            GtkWidget* btn_rem = gtk_button_new_with_label("󰆴");
            gtk_widget_add_css_class(btn_rem, "popup-btn-icon");
            gtk_widget_set_tooltip_text(btn_rem, "Forget device");
            auto* p_mac_rem = new std::string(dev.mac);
            g_signal_connect_data(btn_rem, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto* m = static_cast<std::string*>(p);
                std::thread([mac = *m]() {
                    system(("bluetoothctl remove " + mac).c_str());
                    g_idle_add(+[](gpointer) -> gboolean {
                        refresh_bt_ui();
                        return G_SOURCE_REMOVE;
                    }, nullptr);
                }).detach();
            }), p_mac_rem, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);
            gtk_box_append(GTK_BOX(row), btn_rem);

            gtk_box_append(GTK_BOX(g_bt_paired_box), row);
        } else {
            avail_count++;
            // Pair & Connect button
            GtkWidget* btn_pair = gtk_button_new_with_label("Pair & Connect");
            gtk_widget_add_css_class(btn_pair, "action-btn action-btn-primary");

            auto* p_mac_pair = new std::string(dev.mac);
            g_signal_connect_data(btn_pair, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer p) {
                auto* m = static_cast<std::string*>(p);
                gtk_widget_set_sensitive(GTK_WIDGET(b), FALSE);
                gtk_button_set_label(b, "Connecting...");
                if (g_bt_status_lbl) gtk_label_set_text(GTK_LABEL(g_bt_status_lbl), ("Pairing with " + *m + "...").c_str());
                std::thread([mac = *m]() {
                    // Pair, trust, and connect natively
                    system(("bluetoothctl pair " + mac + " 2>/dev/null").c_str());
                    system(("bluetoothctl trust " + mac + " 2>/dev/null").c_str());
                    system(("bluetoothctl connect " + mac + " 2>/dev/null").c_str());
                    g_idle_add(+[](gpointer) -> gboolean {
                        refresh_bt_ui();
                        return G_SOURCE_REMOVE;
                    }, nullptr);
                }).detach();
            }), p_mac_pair, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);

            gtk_box_append(GTK_BOX(row), btn_pair);
            gtk_box_append(GTK_BOX(g_bt_avail_box), row);
        }
    }

    if (paired_count == 0) {
        GtkWidget* empty_paired = gtk_label_new("No paired devices");
        gtk_widget_add_css_class(empty_paired, "dim-label");
        gtk_widget_set_halign(empty_paired, GTK_ALIGN_START);
        gtk_widget_set_margin_top(empty_paired, 4);
        gtk_widget_set_margin_bottom(empty_paired, 4);
        gtk_box_append(GTK_BOX(g_bt_paired_box), empty_paired);
    }

    if (avail_count == 0) {
        std::string avail_txt = g_bt_scanning.load() ? "Searching for nearby devices..." : "Click 'Scan' to discover devices";
        GtkWidget* empty_avail = gtk_label_new(avail_txt.c_str());
        gtk_widget_add_css_class(empty_avail, "dim-label");
        gtk_widget_set_halign(empty_avail, GTK_ALIGN_START);
        gtk_widget_set_margin_top(empty_avail, 4);
        gtk_widget_set_margin_bottom(empty_avail, 4);
        gtk_box_append(GTK_BOX(g_bt_avail_box), empty_avail);
    }

    if (g_bt_status_lbl) {
        if (g_bt_scanning.load()) {
            gtk_label_set_text(GTK_LABEL(g_bt_status_lbl), "󰑐 Scanning nearby devices...");
        } else {
            gtk_label_set_text(GTK_LABEL(g_bt_status_lbl), "Bluetooth ready");
        }
    }
}

static void refresh_bt_ui() {
    std::thread([]() {
        auto devs = fetch_bluetooth_devices();
        auto* p_devs = new std::vector<BtDevice>(std::move(devs));
        g_idle_add(+[](gpointer p) -> gboolean {
            auto* d = static_cast<std::vector<BtDevice>*>(p);
            render_bt_items(*d);
            delete d;
            return G_SOURCE_REMOVE;
        }, p_devs);
    }).detach();
}

static void toggle_bt_scan() {
    if (g_bt_scanning.load()) {
        g_bt_scanning.store(false);
        exec_async("bluetoothctl scan off");
        if (g_bt_scan_btn) gtk_button_set_label(GTK_BUTTON(g_bt_scan_btn), "󰑐 Scan");
        if (g_bt_status_lbl) gtk_label_set_text(GTK_LABEL(g_bt_status_lbl), "Scan stopped");
    } else {
        g_bt_scanning.store(true);
        if (g_bt_scan_btn) gtk_button_set_label(GTK_BUTTON(g_bt_scan_btn), "󰓛 Stop");
        if (g_bt_status_lbl) gtk_label_set_text(GTK_LABEL(g_bt_status_lbl), "󰑐 Scanning nearby devices...");
        std::thread([]() {
            system("bluetoothctl scan on >/dev/null 2>&1");
        }).detach();
    }
    refresh_bt_ui();
}

static GtkWidget* build_bluetooth_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    std::string bt_status = run_cmd("bluetoothctl show 2>/dev/null");
    bool bt_powered = (bt_status.find("Powered: yes") != std::string::npos);

    // Top Controls: Power Toggle + Scan Button
    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_t = gtk_label_new(bt_powered ? "Bluetooth Adapter Enabled" : "Bluetooth Adapter Disabled");
    gtk_widget_add_css_class(lbl_t, "section-label");
    gtk_widget_set_hexpand(lbl_t, TRUE);
    gtk_widget_set_halign(lbl_t, GTK_ALIGN_START);

    g_bt_scan_btn = gtk_button_new_with_label("󰑐 Scan");
    gtk_widget_add_css_class(g_bt_scan_btn, "action-btn");
    g_signal_connect(g_bt_scan_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        toggle_bt_scan();
    }), nullptr);

    GtkWidget* sw_bt = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw_bt), bt_powered);
    g_signal_connect(sw_bt, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer) -> gboolean {
        std::thread([state]() {
            std::string cmd = state ? "bluetoothctl power on" : "bluetoothctl power off";
            system(cmd.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            g_idle_add(+[](gpointer) -> gboolean {
                refresh_bt_ui();
                return G_SOURCE_REMOVE;
            }, nullptr);
        }).detach();
        return FALSE;
    }), nullptr);

    gtk_box_append(GTK_BOX(top_row), lbl_t);
    gtk_box_append(GTK_BOX(top_row), g_bt_scan_btn);
    gtk_box_append(GTK_BOX(top_row), sw_bt);
    gtk_box_append(GTK_BOX(box), top_row);

    // Status label
    g_bt_status_lbl = gtk_label_new("Scanning Bluetooth devices...");
    gtk_widget_add_css_class(g_bt_status_lbl, "dim-label");
    gtk_widget_set_halign(g_bt_status_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), g_bt_status_lbl);

    // Scrollable Device Area
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 310);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);

    GtkWidget* dev_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    // Section 1: Paired
    GtkWidget* lbl_p = gtk_label_new("PAIRED DEVICES");
    gtk_widget_add_css_class(lbl_p, "section-header");
    gtk_widget_set_halign(lbl_p, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(dev_container), lbl_p);

    g_bt_paired_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(dev_container), g_bt_paired_box);

    // Section 2: Discovered Available
    GtkWidget* lbl_a = gtk_label_new("NEARBY DEVICES");
    gtk_widget_add_css_class(lbl_a, "section-header");
    gtk_widget_set_halign(lbl_a, GTK_ALIGN_START);
    gtk_widget_set_margin_top(lbl_a, 6);
    gtk_box_append(GTK_BOX(dev_container), lbl_a);

    g_bt_avail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(dev_container), g_bt_avail_box);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), dev_container);
    gtk_box_append(GTK_BOX(box), scroll);

    // Initial non-blocking load
    refresh_bt_ui();

    // Auto-scan on open if Bluetooth is powered!
    if (bt_powered) {
        toggle_bt_scan();
    }

    // Periodic live device list refresh while window is open
    if (g_bt_refresh_timer > 0) {
        g_source_remove(g_bt_refresh_timer);
        g_bt_refresh_timer = 0;
    }
    g_bt_refresh_timer = g_timeout_add(2000, +[](gpointer) -> gboolean {
        if (g_bt_scanning.load()) {
            refresh_bt_ui();
        }
        return G_SOURCE_CONTINUE;
    }, nullptr);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 4: SYSTEM RESOURCES (Live CPU/RAM/Disk Gauges & Top Procs)
// ──────────────────────────────────────────────────────────────────────────────
struct SysState {
    unsigned long long prev_idle = 0;
    unsigned long long prev_total = 0;
    GtkWidget* lbl_cpu = nullptr;
    GtkWidget* bar_cpu = nullptr;
    GtkWidget* lbl_mem = nullptr;
    GtkWidget* bar_mem = nullptr;
    GtkWidget* lbl_disk = nullptr;
    GtkWidget* bar_disk = nullptr;
    GtkWidget* procs_box = nullptr;
};

static SysState g_sys;

static void update_sys_stats() {
    // Read CPU instantly from /proc/stat
    std::ifstream fstat("/proc/stat");
    std::string line;
    if (std::getline(fstat, line) && line.rfind("cpu ", 0) == 0) {
        std::stringstream ss(line);
        std::string cpu_kw;
        unsigned long long user, nice, system_t, idle, iowait, irq, softirq, steal;
        ss >> cpu_kw >> user >> nice >> system_t >> idle >> iowait >> irq >> softirq >> steal;
        unsigned long long idle_all = idle + iowait;
        unsigned long long non_idle = user + nice + system_t + irq + softirq + steal;
        unsigned long long total = idle_all + non_idle;

        if (g_sys.prev_total > 0 && total > g_sys.prev_total) {
            unsigned long long total_d = total - g_sys.prev_total;
            unsigned long long idle_d = idle_all - g_sys.prev_idle;
            double cpu_pct = (double)(total_d - idle_d) / (double)total_d;
            cpu_pct = std::clamp(cpu_pct, 0.0, 1.0);

            if (g_sys.bar_cpu) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_sys.bar_cpu), cpu_pct);
            if (g_sys.lbl_cpu) {
                int p = static_cast<int>(std::round(cpu_pct * 100.0));
                gtk_label_set_text(GTK_LABEL(g_sys.lbl_cpu), (std::to_string(p) + "%").c_str());
            }
        }
        g_sys.prev_idle = idle_all;
        g_sys.prev_total = total;
    }

    // Read Memory instantly from /proc/meminfo
    std::ifstream fmem("/proc/meminfo");
    unsigned long long mem_total = 0, mem_available = 0;
    while (std::getline(fmem, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            sscanf(line.c_str(), "MemTotal: %llu kB", &mem_total);
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            sscanf(line.c_str(), "MemAvailable: %llu kB", &mem_available);
        }
    }
    if (mem_total > 0) {
        unsigned long long mem_used = mem_total - mem_available;
        double mem_pct = (double)mem_used / (double)mem_total;
        double used_gb = (double)mem_used / (1024.0 * 1024.0);
        double total_gb = (double)mem_total / (1024.0 * 1024.0);

        if (g_sys.bar_mem) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_sys.bar_mem), mem_pct);
        if (g_sys.lbl_mem) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << used_gb << " / " << total_gb << " GiB (" << (int)(mem_pct * 100) << "%)";
            gtk_label_set_text(GTK_LABEL(g_sys.lbl_mem), ss.str().c_str());
        }
    }

    // Read Disk usage instantly via statvfs("/") (0ms latency, no subshell)
    struct statvfs disk_stat;
    if (statvfs("/", &disk_stat) == 0) {
        unsigned long long total_bytes = disk_stat.f_blocks * disk_stat.f_frsize;
        unsigned long long free_bytes = disk_stat.f_bavail * disk_stat.f_frsize;
        unsigned long long used_bytes = total_bytes - free_bytes;
        double disk_pct = (double)used_bytes / (double)total_bytes;
        double used_gb = (double)used_bytes / (1024.0 * 1024.0 * 1024.0);
        double total_gb = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);

        if (g_sys.bar_disk) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_sys.bar_disk), disk_pct);
        if (g_sys.lbl_disk) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << used_gb << " / " << total_gb << " GB (" << (int)(disk_pct * 100) << "%)";
            gtk_label_set_text(GTK_LABEL(g_sys.lbl_disk), ss.str().c_str());
        }
    }

    // Top processes query asynchronously to prevent any micro-stutters
    std::thread([]() {
        std::string top_out = run_cmd("ps -eo comm,%cpu,%mem --sort=-%cpu --no-headers 2>/dev/null | head -n 4");
        auto* p_out = new std::string(std::move(top_out));
        g_idle_add(+[](gpointer p) -> gboolean {
            auto* s = static_cast<std::string*>(p);
            if (g_sys.procs_box) {
                GtkWidget* child = gtk_widget_get_first_child(g_sys.procs_box);
                while (child) {
                    GtkWidget* next = gtk_widget_get_next_sibling(child);
                    gtk_box_remove(GTK_BOX(g_sys.procs_box), child);
                    child = next;
                }

                std::istringstream iss(*s);
                std::string l;
                while (std::getline(iss, l)) {
                    if (l.empty()) continue;
                    std::stringstream ss(l);
                    std::string comm;
                    double cpu_p, mem_p;
                    ss >> comm >> cpu_p >> mem_p;
                    if (comm.empty()) continue;

                    GtkWidget* r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                    std::string l_txt = comm;
                    if (l_txt.length() > 18) l_txt = l_txt.substr(0, 16) + "..";
                    GtkWidget* l_comm = gtk_label_new(l_txt.c_str());
                    gtk_widget_add_css_class(l_comm, "value-label");
                    gtk_widget_set_hexpand(l_comm, TRUE);
                    gtk_widget_set_halign(l_comm, GTK_ALIGN_START);

                    std::ostringstream ss_stats;
                    ss_stats << std::fixed << std::setprecision(1) << cpu_p << "% CPU  " << mem_p << "% RAM";
                    GtkWidget* l_stats = gtk_label_new(ss_stats.str().c_str());
                    gtk_widget_add_css_class(l_stats, "dim-label");

                    gtk_box_append(GTK_BOX(r), l_comm);
                    gtk_box_append(GTK_BOX(r), l_stats);
                    gtk_box_append(GTK_BOX(g_sys.procs_box), r);
                }
            }
            delete s;
            return G_SOURCE_REMOVE;
        }, p_out);
    }).detach();
}

static GtkWidget* build_system_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    // CPU Section
    GtkWidget* cpu_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_c = gtk_label_new("󰍛 Processor Load (CPU)");
    gtk_widget_add_css_class(lbl_c, "section-label");
    gtk_widget_set_hexpand(lbl_c, TRUE);
    gtk_widget_set_halign(lbl_c, GTK_ALIGN_START);

    g_sys.lbl_cpu = gtk_label_new("0%");
    gtk_widget_add_css_class(g_sys.lbl_cpu, "value-label");

    gtk_box_append(GTK_BOX(cpu_hdr), lbl_c);
    gtk_box_append(GTK_BOX(cpu_hdr), g_sys.lbl_cpu);
    gtk_box_append(GTK_BOX(box), cpu_hdr);

    g_sys.bar_cpu = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(box), g_sys.bar_cpu);

    // RAM Section
    GtkWidget* mem_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_m = gtk_label_new("󰘚 Memory Allocation (RAM)");
    gtk_widget_add_css_class(lbl_m, "section-label");
    gtk_widget_set_hexpand(lbl_m, TRUE);
    gtk_widget_set_halign(lbl_m, GTK_ALIGN_START);

    g_sys.lbl_mem = gtk_label_new("0 GiB");
    gtk_widget_add_css_class(g_sys.lbl_mem, "value-label");

    gtk_box_append(GTK_BOX(mem_hdr), lbl_m);
    gtk_box_append(GTK_BOX(mem_hdr), g_sys.lbl_mem);
    gtk_box_append(GTK_BOX(box), mem_hdr);

    g_sys.bar_mem = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(box), g_sys.bar_mem);

    // Disk Section
    GtkWidget* disk_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_d = gtk_label_new("󰋊 Storage Utilization (/)");
    gtk_widget_add_css_class(lbl_d, "section-label");
    gtk_widget_set_hexpand(lbl_d, TRUE);
    gtk_widget_set_halign(lbl_d, GTK_ALIGN_START);

    g_sys.lbl_disk = gtk_label_new("0 GB");
    gtk_widget_add_css_class(g_sys.lbl_disk, "value-label");

    gtk_box_append(GTK_BOX(disk_hdr), lbl_d);
    gtk_box_append(GTK_BOX(disk_hdr), g_sys.lbl_disk);
    gtk_box_append(GTK_BOX(box), disk_hdr);

    g_sys.bar_disk = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(box), g_sys.bar_disk);

    // Top processes header
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget* lbl_top = gtk_label_new("ACTIVE BACKGROUND PROCESSES");
    gtk_widget_add_css_class(lbl_top, "section-header");
    gtk_widget_set_halign(lbl_top, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl_top);

    g_sys.procs_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(box), g_sys.procs_box);

    // Button to open btop
    GtkWidget* btn_btop = gtk_button_new_with_label("󰍛 Open System Monitor (btop)");
    gtk_widget_add_css_class(btn_btop, "action-btn");
    g_signal_connect(btn_btop, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        exec_async("foot -c ~/.config/monolith/foot/foot.ini --app-id=btop-monitor -e btop");
        close_popup();
    }), nullptr);
    gtk_box_append(GTK_BOX(box), btn_btop);

    // Initial read and periodic 2.0s refresh timer
    update_sys_stats();
    if (g_sys_refresh_timer > 0) {
        g_source_remove(g_sys_refresh_timer);
        g_sys_refresh_timer = 0;
    }
    g_sys_refresh_timer = g_timeout_add(2000, +[](gpointer) -> gboolean {
        update_sys_stats();
        return G_SOURCE_CONTINUE;
    }, nullptr);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 5: POWER MENU (Instant OLED Actions)
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_power_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    struct PowerAction {
        std::string label;
        std::string cmd;
        bool is_danger;
    };

    std::vector<PowerAction> actions = {
        {"󰌾  Lock Screen", "pidof hyprlock || hyprlock --config ~/.config/monolith/hyprlock.conf", false},
        {"󰤄  Suspend System", "systemctl suspend", false},
        {"󰜉  Reboot System", "systemctl reboot", true},
        {"󰐥  Shut Down", "systemctl poweroff", true},
        {"󰍃  Log Out", "hyprctl dispatch exit", true}
    };

    for (const auto& act : actions) {
        GtkWidget* btn = gtk_button_new_with_label(act.label.c_str());
        gtk_widget_add_css_class(btn, act.is_danger ? "action-btn action-btn-danger" : "action-btn");
        gtk_widget_set_hexpand(btn, TRUE);
        gtk_widget_set_size_request(btn, -1, 36);

        auto* p_cmd = new std::string(act.cmd);
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
            auto* c = static_cast<std::string*>(p);
            exec_async(*c);
            close_popup();
        }), p_cmd, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);

        gtk_box_append(GTK_BOX(box), btn);
    }

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 6: BATTERY & POWER PROFILES
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_battery_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 2);

    int cap = 100;
    std::string status = "Discharging";
    {
        std::ifstream fc("/sys/class/power_supply/BAT0/capacity");
        if (fc.is_open()) fc >> cap;
        std::ifstream fs("/sys/class/power_supply/BAT0/status");
        if (fs.is_open()) fs >> status;
    }

    GtkWidget* stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(stat_row, "item-row");

    std::string icon_bat = (status == "Charging") ? "󰂄" : (cap > 80 ? "󰁹" : (cap > 40 ? "󰁾" : "󰁻"));
    GtkWidget* lbl_icon = gtk_label_new(icon_bat.c_str());
    gtk_widget_add_css_class(lbl_icon, "popup-title");

    std::string status_str = status;
    if (status == "Charging") status_str = "Charging";
    else if (status == "Discharging") status_str = "On Battery";
    else if (status == "Full") status_str = "Full";

    GtkWidget* lbl_val = gtk_label_new((std::to_string(cap) + "% (" + status_str + ")").c_str());
    gtk_widget_add_css_class(lbl_val, "value-label");
    gtk_widget_set_hexpand(lbl_val, TRUE);
    gtk_widget_set_halign(lbl_val, GTK_ALIGN_START);

    gtk_box_append(GTK_BOX(stat_row), lbl_icon);
    gtk_box_append(GTK_BOX(stat_row), lbl_val);
    gtk_box_append(GTK_BOX(box), stat_row);

    GtkWidget* prog = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(prog), std::clamp(cap / 100.0, 0.0, 1.0));
    gtk_box_append(GTK_BOX(box), prog);

    GtkWidget* sec_prof = gtk_label_new("POWER PROFILE");
    gtk_widget_add_css_class(sec_prof, "section-header");
    gtk_widget_set_halign(sec_prof, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), sec_prof);

    std::string curr_profile = run_cmd("powerprofilesctl get 2>/dev/null");
    while (!curr_profile.empty() && (curr_profile.back() == '\n' || curr_profile.back() == ' '))
        curr_profile.pop_back();

    auto make_prof_btn = [&](const std::string& name, const std::string& prof_id) -> GtkWidget* {
        GtkWidget* btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "btn-action");
        if (curr_profile == prof_id) {
            gtk_widget_add_css_class(btn, "btn-action-primary");
        }
        GtkWidget* b_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* n_lbl = gtk_label_new(name.c_str());
        gtk_widget_set_hexpand(n_lbl, TRUE);
        gtk_widget_set_halign(n_lbl, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(b_box), n_lbl);
        gtk_button_set_child(GTK_BUTTON(btn), b_box);

        std::string cmd = "powerprofilesctl set " + prof_id;
        auto* p_cmd = new std::string(cmd);
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            auto* c = static_cast<std::string*>(user_data);
            exec_async(*c);
        }), p_cmd, [](gpointer data, GClosure*) {
            delete static_cast<std::string*>(data);
        }, (GConnectFlags)0);
        return btn;
    };

    gtk_box_append(GTK_BOX(box), make_prof_btn("󰓅  Performance", "performance"));
    gtk_box_append(GTK_BOX(box), make_prof_btn("󰾆  Balanced", "balanced"));
    gtk_box_append(GTK_BOX(box), make_prof_btn("󰌪  Power Saver", "power-saver"));

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 7: SCREEN BRIGHTNESS & NIGHT LIGHT
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_backlight_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 2);

    std::string cur_str = run_cmd("brightnessctl -m 2>/dev/null | cut -d, -f4 | tr -d '%'");
    int cur_pct = 75;
    try { if (!cur_str.empty()) cur_pct = std::stoi(cur_str); } catch (...) {}

    GtkWidget* stat_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(stat_row, "item-row");

    GtkWidget* lbl_icon = gtk_label_new("󰃠");
    gtk_widget_add_css_class(lbl_icon, "popup-title");

    GtkWidget* lbl_val = gtk_label_new(("Display Brightness: " + std::to_string(cur_pct) + "%").c_str());
    gtk_widget_add_css_class(lbl_val, "value-label");
    gtk_widget_set_hexpand(lbl_val, TRUE);
    gtk_widget_set_halign(lbl_val, GTK_ALIGN_START);

    gtk_box_append(GTK_BOX(stat_row), lbl_icon);
    gtk_box_append(GTK_BOX(stat_row), lbl_val);
    gtk_box_append(GTK_BOX(box), stat_row);

    GtkAdjustment* adj_b = gtk_adjustment_new(cur_pct, 5.0, 100.0, 1.0, 5.0, 0.0);
    GtkWidget* slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj_b);
    gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
    g_signal_connect(adj_b, "value-changed", G_CALLBACK(+[](GtkAdjustment* a, gpointer p) {
        int val = static_cast<int>(std::round(gtk_adjustment_get_value(a)));
        std::string cmd = "brightnessctl set " + std::to_string(val) + "%";
        exec_async(cmd);
        if (p) {
            gtk_label_set_text(GTK_LABEL(p), ("Display Brightness: " + std::to_string(val) + "%").c_str());
        }
    }), lbl_val);
    gtk_box_append(GTK_BOX(box), slider);

    // Quick presets
    GtkWidget* preset_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    int presets[] = { 25, 50, 75, 100 };
    for (int p : presets) {
        GtkWidget* btn = gtk_button_new_with_label((std::to_string(p) + "%").c_str());
        gtk_widget_add_css_class(btn, "btn-action");
        gtk_widget_set_hexpand(btn, TRUE);
        struct PresetCtx { GtkAdjustment* adj; int val; };
        auto* p_ctx = new PresetCtx{adj_b, p};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* c = static_cast<PresetCtx*>(data);
            gtk_adjustment_set_value(c->adj, c->val);
        }), p_ctx, [](gpointer data, GClosure*) {
            delete static_cast<PresetCtx*>(data);
        }, (GConnectFlags)0);
        gtk_box_append(GTK_BOX(preset_box), btn);
    }
    gtk_box_append(GTK_BOX(box), preset_box);

    GtkWidget* sec_nl = gtk_label_new("NIGHT LIGHT FILTER");
    gtk_widget_add_css_class(sec_nl, "section-header");
    gtk_widget_set_halign(sec_nl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), sec_nl);

    bool nl_active = !run_cmd("pgrep -x hyprsunset").empty();
    GtkWidget* nl_btn = gtk_button_new();
    gtk_widget_add_css_class(nl_btn, "btn-action");
    if (nl_active) gtk_widget_add_css_class(nl_btn, "btn-action-primary");

    GtkWidget* nl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* nl_ico = gtk_label_new("󰃝");
    GtkWidget* nl_lbl = gtk_label_new(nl_active ? "Blue Light Filter: ACTIVE" : "Blue Light Filter: DISABLED");
    gtk_widget_set_hexpand(nl_lbl, TRUE);
    gtk_widget_set_halign(nl_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(nl_box), nl_ico);
    gtk_box_append(GTK_BOX(nl_box), nl_lbl);
    gtk_button_set_child(GTK_BUTTON(nl_btn), nl_box);

    g_signal_connect(nl_btn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        exec_async("~/.config/monolith/scripts/nightlight.sh");
        bool active_now = run_cmd("pgrep -x hyprsunset").empty(); // opposite
        if (active_now) {
            gtk_widget_add_css_class(GTK_WIDGET(b), "btn-action-primary");
        } else {
            gtk_widget_remove_css_class(GTK_WIDGET(b), "btn-action-primary");
        }
    }), nullptr);
    gtk_box_append(GTK_BOX(box), nl_btn);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// UNIFIED MORPHING SYSTEM: A SINGLE SURFACE GLIDING AND MORPHING SMOOTHLY
// ──────────────────────────────────────────────────────────────────────────────

static int get_monitor_logical_width() {
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        GListModel* monitors = gdk_display_get_monitors(display);
        if (monitors && g_list_model_get_n_items(monitors) > 0) {
            GdkMonitor* mon = GDK_MONITOR(g_list_model_get_item(monitors, 0));
            if (mon) {
                GdkRectangle geom;
                gdk_monitor_get_geometry(mon, &geom);
                g_object_unref(mon);
                if (geom.width > 0) return geom.width;
            }
        }
    }
    return 1500;
}

static void update_taskgraph_ipc() {
    std::ofstream f("/dev/shm/monolith_popup_active");
    if (f.is_open()) {
        if (g_is_open) {
            int screen_w = get_monitor_logical_width();
            int screen_x = screen_w - (int)std::round(g_curr_margin_right) - (int)std::round(g_curr_width);
            int width = (int)std::round(g_curr_width);
            int height = (int)std::round(g_curr_height);
            f << "1 " << screen_x << " 33 " << width << " " << height << "\n";
        } else {
            f << "0 0 0 0 0\n";
        }
    }
}

static void update_pin_button_ui() {
    if (!g_morph_pin_btn) return;
    if (g_is_pinned) {
        gtk_widget_add_css_class(g_morph_pin_btn, "pinned");
        gtk_widget_set_tooltip_text(g_morph_pin_btn, "Unpin window");
        if (g_leave_timer > 0) {
            g_source_remove(g_leave_timer);
            g_leave_timer = 0;
        }
    } else {
        gtk_widget_remove_css_class(g_morph_pin_btn, "pinned");
        gtk_widget_set_tooltip_text(g_morph_pin_btn, "Pin window (keep open)");
    }
}

static gboolean on_morph_tick(GtkWidget*, GdkFrameClock*, gpointer) {
    const double speed = 0.32;
    bool done = true;

    double d_margin = g_target_margin_right - g_curr_margin_right;
    if (std::abs(d_margin) > 0.5) {
        g_curr_margin_right += d_margin * speed;
        done = false;
    } else {
        g_curr_margin_right = g_target_margin_right;
    }

    double d_width = g_target_width - g_curr_width;
    if (std::abs(d_width) > 0.5) {
        g_curr_width += d_width * speed;
        done = false;
    } else {
        g_curr_width = g_target_width;
    }

    double d_height = g_target_height - g_curr_height;
    if (std::abs(d_height) > 0.5) {
        g_curr_height += d_height * speed;
        done = false;
    } else {
        g_curr_height = g_target_height;
    }

    double d_notch = g_target_notch_x - g_curr_notch_x;
    if (std::abs(d_notch) > 0.5) {
        g_curr_notch_x += d_notch * speed;
        done = false;
    } else {
        g_curr_notch_x = g_target_notch_x;
    }

    if (g_morph_win) {
        gtk_layer_set_margin(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_EDGE_RIGHT, (int)std::round(g_curr_margin_right));
    }
    if (g_morph_panel) {
        gtk_widget_set_size_request(g_morph_panel, (int)std::round(g_curr_width), (int)std::round(g_curr_height));
    }
    if (g_morph_notch_da) {
        gtk_widget_queue_draw(g_morph_notch_da);
    }

    update_taskgraph_ipc();

    if (done) {
        g_morph_tick_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void schedule_leave_check() {
    if (g_is_pinned || !g_is_open) return;
    if (g_leave_timer > 0) g_source_remove(g_leave_timer);
    g_leave_timer = g_timeout_add(320, +[](gpointer) -> gboolean {
        g_leave_timer = 0;
        if (!g_is_pinned && g_is_open && !g_cursor_in_window) {
            hide_morph_window();
        }
        return G_SOURCE_REMOVE;
    }, nullptr);
}

static void hide_morph_window() {
    if (!g_is_open) return;

    if (g_leave_timer > 0) {
        g_source_remove(g_leave_timer);
        g_leave_timer = 0;
    }
    if (g_bt_scanning.load()) {
        g_bt_scanning.store(false);
        exec_async("bluetoothctl scan off");
    }
    if (g_bt_refresh_timer > 0) {
        g_source_remove(g_bt_refresh_timer);
        g_bt_refresh_timer = 0;
    }
    if (g_sys_refresh_timer > 0) {
        g_source_remove(g_sys_refresh_timer);
        g_sys_refresh_timer = 0;
    }
    if (g_morph_tick_id > 0) {
        gtk_widget_remove_tick_callback(g_morph_win, g_morph_tick_id);
        g_morph_tick_id = 0;
    }

    g_cursor_in_window = false;
    g_is_open = false;
    g_is_pinned = false;
    update_pin_button_ui();

    if (g_morph_revealer) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(g_morph_revealer), FALSE);
        if (g_hide_timer > 0) g_source_remove(g_hide_timer);
        g_hide_timer = g_timeout_add(170, +[](gpointer) -> gboolean {
            g_hide_timer = 0;
            if (!g_is_open && g_morph_win) {
                gtk_widget_set_visible(g_morph_win, FALSE);
            }
            return G_SOURCE_REMOVE;
        }, nullptr);
    } else if (g_morph_win) {
        gtk_widget_set_visible(g_morph_win, FALSE);
    }

    update_taskgraph_ipc();
}

static void hide_all_unpinned() {
    if (g_is_open && !g_is_pinned) {
        hide_morph_window();
    }
}

static void morph_to_module(const std::string& id) {
    if (id == g_active_id && g_is_open) return;
    auto it = g_specs.find(id);
    if (it == g_specs.end()) return;
    const auto& spec = it->second;

    if (g_leave_timer > 0) {
        g_source_remove(g_leave_timer);
        g_leave_timer = 0;
    }

    std::string old_id = g_active_id;
    g_active_id = id;

    // Service lifecycle
    if (old_id == "bluetooth" && id != "bluetooth") {
        if (g_bt_scanning.load()) {
            g_bt_scanning.store(false);
            exec_async("bluetoothctl scan off");
        }
        if (g_bt_refresh_timer > 0) {
            g_source_remove(g_bt_refresh_timer);
            g_bt_refresh_timer = 0;
        }
    }
    if (old_id == "system" && id != "system") {
        if (g_sys_refresh_timer > 0) {
            g_source_remove(g_sys_refresh_timer);
            g_sys_refresh_timer = 0;
        }
    }

    if (id == "bluetooth") {
        refresh_bt_ui();
        if (g_bt_refresh_timer == 0) {
            g_bt_refresh_timer = g_timeout_add(2000, +[](gpointer) -> gboolean {
                if (g_bt_scanning.load()) refresh_bt_ui();
                return G_SOURCE_CONTINUE;
            }, nullptr);
        }
    } else if (id == "system") {
        update_sys_stats();
        if (g_sys_refresh_timer == 0) {
            g_sys_refresh_timer = g_timeout_add(2000, +[](gpointer) -> gboolean {
                update_sys_stats();
                return G_SOURCE_CONTINUE;
            }, nullptr);
        }
    } else if (id == "network") {
        refresh_wifi_ui();
    }

    // Update Header
    std::string full_title = spec.icon + " " + spec.title;
    if (g_morph_title_lbl) {
        gtk_label_set_text(GTK_LABEL(g_morph_title_lbl), full_title.c_str());
    }

    // Switch Stack with smooth slide
    if (g_morph_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(g_morph_stack), id.c_str());
    }

    // Trigger dynamic lerp animation
    g_target_margin_right = spec.margin_right;
    g_target_width = spec.width;
    g_target_height = spec.height;
    g_target_notch_x = spec.icon_rel_notch;

    if (g_morph_tick_id == 0 && g_morph_win) {
        g_morph_tick_id = gtk_widget_add_tick_callback(g_morph_win, on_morph_tick, nullptr, nullptr);
    }

    {
        std::ofstream tf("/tmp/.monolith_popup.type");
        if (tf.is_open()) tf << g_active_id << "\n";
    }
}

static void show_morph_window(const std::string& id, bool pin) {
    auto it = g_specs.find(id);
    if (it == g_specs.end()) return;
    const auto& spec = it->second;

    if (g_leave_timer > 0) {
        g_source_remove(g_leave_timer);
        g_leave_timer = 0;
    }
    if (g_hide_timer > 0) {
        g_source_remove(g_hide_timer);
        g_hide_timer = 0;
    }

    g_active_id = id;
    g_is_pinned = pin;
    update_pin_button_ui();

    // Snap to position immediately before reveal
    g_curr_margin_right = spec.margin_right;
    g_curr_width = spec.width;
    g_curr_height = spec.height;
    g_curr_notch_x = spec.icon_rel_notch;
    g_target_margin_right = spec.margin_right;
    g_target_width = spec.width;
    g_target_height = spec.height;
    g_target_notch_x = spec.icon_rel_notch;

    if (g_morph_tick_id > 0) {
        gtk_widget_remove_tick_callback(g_morph_win, g_morph_tick_id);
        g_morph_tick_id = 0;
    }

    gtk_layer_set_margin(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_EDGE_RIGHT, (int)g_curr_margin_right);
    gtk_widget_set_size_request(g_morph_panel, (int)g_curr_width, (int)g_curr_height);
    gtk_widget_queue_draw(g_morph_notch_da);

    std::string full_title = spec.icon + " " + spec.title;
    if (g_morph_title_lbl) {
        gtk_label_set_text(GTK_LABEL(g_morph_title_lbl), full_title.c_str());
    }

    if (g_morph_stack) {
        GtkStackTransitionType prev_trans = gtk_stack_get_transition_type(GTK_STACK(g_morph_stack));
        gtk_stack_set_transition_type(GTK_STACK(g_morph_stack), GTK_STACK_TRANSITION_TYPE_NONE);
        gtk_stack_set_visible_child_name(GTK_STACK(g_morph_stack), id.c_str());
        gtk_stack_set_transition_type(GTK_STACK(g_morph_stack), prev_trans);
    }

    if (id == "bluetooth") {
        refresh_bt_ui();
        if (g_bt_refresh_timer == 0) {
            g_bt_refresh_timer = g_timeout_add(2000, +[](gpointer) -> gboolean {
                if (g_bt_scanning.load()) refresh_bt_ui();
                return G_SOURCE_CONTINUE;
            }, nullptr);
        }
    } else if (id == "system") {
        update_sys_stats();
        if (g_sys_refresh_timer == 0) {
            g_sys_refresh_timer = g_timeout_add(2000, +[](gpointer) -> gboolean {
                update_sys_stats();
                return G_SOURCE_CONTINUE;
            }, nullptr);
        }
    } else if (id == "network") {
        refresh_wifi_ui();
    }

    gtk_widget_set_visible(g_morph_win, TRUE);
    if (g_morph_revealer) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(g_morph_revealer), TRUE);
    }
    gtk_window_present(GTK_WINDOW(g_morph_win));
    g_is_open = true;

    update_taskgraph_ipc();

    {
        std::ofstream pf("/tmp/.monolith_popup.pid");
        if (pf.is_open()) pf << getpid() << "\n";
    }
    {
        std::ofstream tf("/tmp/.monolith_popup.type");
        if (tf.is_open()) tf << g_active_id << "\n";
    }
}

static void toggle_module(const std::string& id, bool pin) {
    if (g_is_open) {
        if (g_active_id == id) {
            hide_morph_window();
        } else {
            morph_to_module(id);
            if (pin) {
                g_is_pinned = true;
                update_pin_button_ui();
            }
        }
    } else {
        show_morph_window(id, pin);
    }
}

[[maybe_unused]] static void show_module(const std::string& id, bool pin) {
    toggle_module(id, pin);
}

[[maybe_unused]] static void hide_module(const std::string& id) {
    if (g_is_open && (id.empty() || g_active_id == id)) {
        hide_morph_window();
    }
}

[[maybe_unused]] static std::string get_module_for_rel_x(double x) {
    if (x >= 438) return "power";
    if (x >= 415 && x < 438) return "notification";
    if (x >= 392 && x < 415) return "settings";
    if (x >= 368 && x < 392) return "nightlight";
    if (x >= 310 && x < 368) return "system";
    if (x >= 265 && x < 310) return "battery";
    if (x >= 212 && x < 265) return "backlight";
    if (x >= 155 && x < 212) return "audio";
    if (x >= 70 && x < 155) return "network";
    if (x >= 15 && x < 70) return "bluetooth";
    return "";
}

[[maybe_unused]] static void on_topbar_hover(double x) {
    std::string target = get_module_for_rel_x(x);
    if (!target.empty()) {
        if (target == "settings" || target == "notification" || target == "nightlight") {
            hide_all_unpinned();
        } else {
            auto it = g_specs.find(target);
            if (it != g_specs.end()) {
                if (g_is_open) {
                    if (g_active_id != target) {
                        morph_to_module(target);
                    }
                } else {
                    show_morph_window(target, false /* hover reveals unpinned */);
                }
            }
        }
    }
}

static void init_windows(GtkApplication* app) {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    g_app = app;
    apply_css();

    // ──────────────────────────────────────────────────────────────────────────
    // 1. REGISTER MODULE SPECIFICATIONS
    // ──────────────────────────────────────────────────────────────────────────
    auto add_spec = [&](const std::string& id, const std::string& title, const std::string& icon,
                        int width, int height, int margin_right, double notch, std::function<GtkWidget*()> bf) {
        ModuleSpec s;
        s.id = id;
        s.title = title;
        s.icon = icon;
        s.width = width;
        s.height = height;
        s.margin_right = margin_right;
        s.icon_rel_notch = notch;
        s.build_func = bf;
        g_specs[id] = s;
    };

    // Pixel-perfect individualized geometry tailored to each module's unique content
    add_spec("bluetooth", "Bluetooth", "󰂯", 380, 440, 230, 198.0, build_bluetooth_panel);
    add_spec("network", "Wi-Fi & Network", "󰖩", 380, 440, 180, 210.0, build_network_panel);
    add_spec("audio", "Sound & Audio", "󰕾", 350, 290, 130, 205.0, build_audio_panel);
    add_spec("backlight", "Display Brightness", "󰃠", 300, 220, 80, 180.0, build_backlight_panel);
    add_spec("battery", "Battery & Power", "󰁹", 320, 270, 50, 197.0, build_battery_panel);
    add_spec("system", "System Resources", "󰍛", 390, 460, 15, 285.0, build_system_panel);
    add_spec("power", "Power Menu", "󰐥", 240, 255, 6, 216.0, build_power_panel);

    // ──────────────────────────────────────────────────────────────────────────
    // 2. UNIFIED MORPHING DROPDOWN WINDOW (Single Surface, Zero Overlap)
    // ──────────────────────────────────────────────────────────────────────────
    g_morph_win = gtk_window_new();
    gtk_application_add_window(app, GTK_WINDOW(g_morph_win));
    gtk_layer_init_for_window(GTK_WINDOW(g_morph_win));
    gtk_layer_set_layer(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(g_morph_win), -1);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    gtk_layer_set_namespace(GTK_WINDOW(g_morph_win), "monolith-popup-morph");
    gtk_layer_set_anchor(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_EDGE_TOP, 32);
    gtk_layer_set_margin(GTK_WINDOW(g_morph_win), GTK_LAYER_SHELL_EDGE_RIGHT, (int)g_curr_margin_right);
    gtk_widget_add_css_class(g_morph_win, "monolith-popup");

    // Slide-down reveal container
    g_morph_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(g_morph_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(g_morph_revealer), 160);

    // Main growing panel
    g_morph_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(g_morph_panel, "growing-bar-panel");
    gtk_widget_set_size_request(g_morph_panel, (int)g_curr_width, (int)g_curr_height);
    gtk_widget_set_overflow(g_morph_panel, GTK_OVERFLOW_HIDDEN);

    // Luminous notch beacon that glides dynamically with cursor
    g_morph_notch_da = gtk_drawing_area_new();
    gtk_widget_set_size_request(g_morph_notch_da, -1, 4);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_morph_notch_da), +[](GtkDrawingArea*, cairo_t* cr, int width, int, gpointer) {
        double notch_cx = g_curr_notch_x;
        notch_cx = std::clamp(notch_cx, 24.0, (double)width - 24.0);

        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_paint(cr);

        cairo_pattern_t* pat = cairo_pattern_create_linear(notch_cx - 24.0, 0, notch_cx + 24.0, 0);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, 1.0, 1.0, 1.0, 0.0);
        cairo_pattern_add_color_stop_rgba(pat, 0.5, 1.0, 1.0, 1.0, 0.90);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 1.0, 1.0, 1.0, 0.0);
        cairo_set_source(cr, pat);
        cairo_rectangle(cr, notch_cx - 24.0, 0, 48.0, 2.0);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    }, nullptr, nullptr);
    gtk_box_append(GTK_BOX(g_morph_panel), g_morph_notch_da);

    // Header with title, pin, and close buttons
    GtkWidget* hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(hdr, "popup-header");

    g_morph_title_lbl = gtk_label_new("󰖩 Wi-Fi & Network");
    gtk_widget_add_css_class(g_morph_title_lbl, "popup-title");
    gtk_widget_set_hexpand(g_morph_title_lbl, TRUE);
    gtk_widget_set_halign(g_morph_title_lbl, GTK_ALIGN_START);

    g_morph_pin_btn = gtk_button_new_with_label("󰐃");
    gtk_widget_add_css_class(g_morph_pin_btn, "popup-btn-icon");
    gtk_widget_set_tooltip_text(g_morph_pin_btn, "Pin window (keep open)");
    g_signal_connect(g_morph_pin_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        g_is_pinned = !g_is_pinned;
        update_pin_button_ui();
    }), nullptr);

    GtkWidget* btn_close = gtk_button_new_with_label("󰅖");
    gtk_widget_add_css_class(btn_close, "popup-btn-icon");
    gtk_widget_set_tooltip_text(btn_close, "Close panel (Esc)");
    g_signal_connect(btn_close, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        hide_morph_window();
    }), nullptr);

    gtk_box_append(GTK_BOX(hdr), g_morph_title_lbl);
    gtk_box_append(GTK_BOX(hdr), g_morph_pin_btn);
    gtk_box_append(GTK_BOX(hdr), btn_close);
    gtk_box_append(GTK_BOX(g_morph_panel), hdr);

    // Morphing stack container with slide transitions and non-homogeneous size interpolation
    g_morph_stack = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(g_morph_stack), FALSE);
    gtk_stack_set_vhomogeneous(GTK_STACK(g_morph_stack), FALSE);
    gtk_stack_set_transition_type(GTK_STACK(g_morph_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(g_morph_stack), 180);
    gtk_stack_set_interpolate_size(GTK_STACK(g_morph_stack), TRUE);
    gtk_widget_set_overflow(g_morph_stack, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_hexpand(g_morph_stack, TRUE);
    gtk_widget_set_vexpand(g_morph_stack, TRUE);

    // Populate all panels into stack in order
    for (const auto& id : g_module_order) {
        GtkWidget* content = g_specs[id].build_func();
        if (content) {
            gtk_stack_add_named(GTK_STACK(g_morph_stack), content, id.c_str());
        }
    }

    // Immediately stop initial background timers until active
    if (g_bt_refresh_timer > 0) {
        g_source_remove(g_bt_refresh_timer);
        g_bt_refresh_timer = 0;
    }
    if (g_bt_scanning.load()) {
        g_bt_scanning.store(false);
        exec_async("bluetoothctl scan off");
    }
    if (g_sys_refresh_timer > 0) {
        g_source_remove(g_sys_refresh_timer);
        g_sys_refresh_timer = 0;
    }

    gtk_box_append(GTK_BOX(g_morph_panel), g_morph_stack);
    gtk_revealer_set_child(GTK_REVEALER(g_morph_revealer), g_morph_panel);
    gtk_window_set_child(GTK_WINDOW(g_morph_win), g_morph_revealer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(g_morph_revealer), FALSE);
    gtk_widget_set_visible(g_morph_win, FALSE);

    // Window motion controller for hover preservation
    GtkEventControllerMotion* pop_motion = GTK_EVENT_CONTROLLER_MOTION(gtk_event_controller_motion_new());
    g_signal_connect(pop_motion, "enter", G_CALLBACK(+[](GtkEventControllerMotion*, double, double, gpointer) {
        g_cursor_in_window = true;
        if (g_leave_timer > 0) {
            g_source_remove(g_leave_timer);
            g_leave_timer = 0;
        }
    }), nullptr);
    g_signal_connect(pop_motion, "motion", G_CALLBACK(+[](GtkEventControllerMotion*, double, double, gpointer) {
        g_cursor_in_window = true;
        if (g_leave_timer > 0) {
            g_source_remove(g_leave_timer);
            g_leave_timer = 0;
        }
    }), nullptr);
    g_signal_connect(pop_motion, "leave", G_CALLBACK(+[](GtkEventControllerMotion*, gpointer) {
        if (g_cursor_in_window) {
            g_cursor_in_window = false;
            schedule_leave_check();
        }
    }), nullptr);
    gtk_widget_add_controller(g_morph_win, GTK_EVENT_CONTROLLER(pop_motion));

    // Close on Escape key
    GtkEventControllerKey* key_ctl = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    g_signal_connect(key_ctl, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer) -> gboolean {
        if (keyval == GDK_KEY_Escape) {
            hide_morph_window();
            return TRUE;
        }
        return FALSE;
    }), nullptr);
    gtk_widget_add_controller(g_morph_win, GTK_EVENT_CONTROLLER(key_ctl));
}

int main(int argc, char* argv[]) {
    g_app = gtk_application_new("org.monolith.popup", G_APPLICATION_HANDLES_COMMAND_LINE);

    g_signal_connect(g_app, "activate", G_CALLBACK(+[](GApplication* app, gpointer) {
        init_windows(GTK_APPLICATION(app));
        g_application_hold(app);
    }), nullptr);

    g_signal_connect(g_app, "command-line", G_CALLBACK(+[](GApplication* app, GApplicationCommandLine* cmd, gpointer) -> int {
        init_windows(GTK_APPLICATION(app));
        g_application_hold(app);

        int c = 0;
        char** v = g_application_command_line_get_arguments(cmd, &c);
        if (c > 1) {
            std::string mode = v[1];
            if (mode != "--daemon") {
                toggle_module(mode, false /* default to UNPINNED */);
            }
        }
        return 0;
    }), nullptr);

    int status = g_application_run(G_APPLICATION(g_app), argc, argv);
    g_object_unref(g_app);
    return status;
}
