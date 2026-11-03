#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>

// ══════════════════════════════════════════════════════════════════════════════
// MONOLITH POPUP SUITE
// Native GTK4 Layer-Shell Control Panels:
//   - Audio (PipeWire / WirePlumber volume, mute, mic)
//   - Network (Wi-Fi scanning, toggle, connect, ethernet)
//   - Bluetooth (Device list, power toggle, connect/disconnect)
//   - System Resources (Live CPU/RAM progress bars, top processes)
//   - Power (OLED minimalist session menu)
// ══════════════════════════════════════════════════════════════════════════════

static std::string g_mode = "system";
static GtkWidget* g_win = nullptr;
static GtkApplication* g_app = nullptr;
static guint g_refresh_timer = 0;
static bool g_pinned = false;
static guint g_leave_timer = 0;
static guint g_initial_timer = 0;

// Helper to run a shell command and capture stdout
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

// Background fire-and-forget shell command
static void exec_async(const std::string& cmd) {
    std::string full_cmd = cmd + " >/dev/null 2>&1 &";
    system(full_cmd.c_str());
}

static void close_popup() {
    if (g_refresh_timer > 0) {
        g_source_remove(g_refresh_timer);
        g_refresh_timer = 0;
    }
    if (g_leave_timer > 0) {
        g_source_remove(g_leave_timer);
        g_leave_timer = 0;
    }
    if (g_initial_timer > 0) {
        g_source_remove(g_initial_timer);
        g_initial_timer = 0;
    }
    unlink("/tmp/.monolith_popup.pid");
    unlink("/tmp/.monolith_popup.type");
    if (g_app) {
        g_application_quit(G_APPLICATION(g_app));
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// CSS STYLING (OLED Black, Brutalist 1px Border, JetBrains Mono)
// ──────────────────────────────────────────────────────────────────────────────
static void apply_css() {
    const char* css = R"(
        window.monolith-popup {
            background-color: #000000;
            border: 1px solid #27272a;
            border-radius: 6px;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.8);
        }

        .popup-box {
            background-color: #000000;
            padding: 12px 14px;
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

        .popup-btn-close {
            background: transparent;
            color: #71717a;
            border: none;
            border-radius: 4px;
            padding: 2px 6px;
            font-size: 12px;
        }
        .popup-btn-close:hover {
            background: #18181b;
            color: #ffffff;
        }

        .popup-btn-pin {
            background: transparent;
            color: #71717a;
            border: none;
            border-radius: 4px;
            padding: 2px 6px;
            font-size: 12px;
        }
        .popup-btn-pin:hover {
            background: #18181b;
            color: #ffffff;
        }
        .popup-btn-pin.pinned {
            background: #27272a;
            color: #38bdf8;
            border: 1px solid #0284c7;
        }

        .section-label {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            font-weight: 600;
            color: #a1a1aa;
            margin-bottom: 4px;
        }

        .value-label {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            color: #ffffff;
        }

        .item-row {
            background: #09090b;
            border: 1px solid #18181b;
            border-radius: 4px;
            padding: 8px 10px;
            margin-bottom: 6px;
            transition: all 120ms ease;
        }
        .item-row:hover {
            border-color: #27272a;
            background: #121214;
        }

        .action-btn {
            background: #18181b;
            border: 1px solid #27272a;
            color: #e4e4e7;
            border-radius: 4px;
            padding: 6px 12px;
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            font-weight: 600;
        }
        .action-btn:hover {
            background: #27272a;
            color: #ffffff;
            border-color: #3f3f46;
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
            padding: 4px 8px;
        }
        entry:focus {
            border-color: #71717a;
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
// PANEL 1: AUDIO (PipeWire / WirePlumber)
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_audio_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    // Get current volume
    std::string vol_str = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null");
    double cur_vol = 0.5;
    bool is_muted = (vol_str.find("[MUTED]") != std::string::npos);
    size_t pos = vol_str.find("Volume: ");
    if (pos != std::string::npos) {
        try {
            cur_vol = std::stod(vol_str.substr(pos + 8, 4));
        } catch (...) {}
    }

    // Output volume section
    GtkWidget* out_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_out = gtk_label_new("󰕾 Output Volume");
    gtk_widget_add_css_class(lbl_out, "section-label");
    gtk_widget_set_hexpand(lbl_out, TRUE);
    gtk_widget_set_halign(lbl_out, GTK_ALIGN_START);

    int pct = static_cast<int>(std::round(cur_vol * 100.0));
    std::string pct_txt = std::to_string(pct) + "%" + (is_muted ? " [MUTED]" : "");
    GtkWidget* lbl_pct = gtk_label_new(pct_txt.c_str());
    gtk_widget_add_css_class(lbl_pct, "value-label");

    gtk_box_append(GTK_BOX(out_header), lbl_out);
    gtk_box_append(GTK_BOX(out_header), lbl_pct);
    gtk_box_append(GTK_BOX(box), out_header);

    // Slider
    GtkAdjustment* adj = gtk_adjustment_new(cur_vol * 100.0, 0.0, 150.0, 1.0, 5.0, 0.0);
    GtkWidget* scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_box_append(GTK_BOX(box), scale);

    g_signal_connect(adj, "value-changed", G_CALLBACK(+[](GtkAdjustment* a, gpointer p) {
        double v = gtk_adjustment_get_value(a) / 100.0;
        int p_val = static_cast<int>(std::round(v * 100.0));
        std::string txt = std::to_string(p_val) + "%";
        gtk_label_set_text(GTK_LABEL(p), txt.c_str());
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << v;
        std::string cmd = "wpctl set-volume @DEFAULT_AUDIO_SINK@ " + ss.str();
        system(cmd.c_str());
    }), lbl_pct);

    // Buttons: Mute toggle & Quick audio tools
    GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* btn_mute = gtk_button_new_with_label(is_muted ? "󰖁 Unmute Audio" : "󰕾 Mute Audio");
    gtk_widget_add_css_class(btn_mute, "action-btn");
    gtk_widget_set_hexpand(btn_mute, TRUE);
    g_signal_connect(btn_mute, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        system("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
        close_popup();
    }), nullptr);

    GtkWidget* btn_pavu = gtk_button_new_with_label("󰒓 Pavucontrol Mixer");
    gtk_widget_add_css_class(btn_pavu, "action-btn");
    gtk_widget_set_hexpand(btn_pavu, TRUE);
    g_signal_connect(btn_pavu, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        exec_async("pavucontrol");
        close_popup();
    }), nullptr);

    gtk_box_append(GTK_BOX(btn_row), btn_mute);
    gtk_box_append(GTK_BOX(btn_row), btn_pavu);
    gtk_box_append(GTK_BOX(box), btn_row);

    // Microphone Section
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget* mic_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_mic = gtk_label_new("󰍬 Microphone (Input)");
    gtk_widget_add_css_class(lbl_mic, "section-label");
    gtk_widget_set_hexpand(lbl_mic, TRUE);
    gtk_widget_set_halign(lbl_mic, GTK_ALIGN_START);

    GtkWidget* btn_mic_mute = gtk_button_new_with_label("Toggle Mute");
    gtk_widget_add_css_class(btn_mic_mute, "action-btn");
    g_signal_connect(btn_mic_mute, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        system("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle");
        close_popup();
    }), nullptr);

    gtk_box_append(GTK_BOX(mic_box), lbl_mic);
    gtk_box_append(GTK_BOX(mic_box), btn_mic_mute);
    gtk_box_append(GTK_BOX(box), mic_box);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 2: NETWORK (Wi-Fi & Ethernet)
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_network_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    // Check Wi-Fi radio status
    std::string wifi_state = run_cmd("nmcli -t -f WIFI g 2>/dev/null");
    bool wifi_enabled = (wifi_state.find("enabled") != std::string::npos);

    // Header row with toggle
    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_status = gtk_label_new(wifi_enabled ? "󰖩 Wi-Fi Enabled" : "󰖪 Wi-Fi Disabled");
    gtk_widget_add_css_class(lbl_status, "section-label");
    gtk_widget_set_hexpand(lbl_status, TRUE);
    gtk_widget_set_halign(lbl_status, GTK_ALIGN_START);

    GtkWidget* sw_wifi = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw_wifi), wifi_enabled);
    g_signal_connect(sw_wifi, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer) -> gboolean {
        std::string cmd = state ? "nmcli radio wifi on" : "nmcli radio wifi off";
        system(cmd.c_str());
        close_popup();
        return FALSE;
    }), nullptr);

    gtk_box_append(GTK_BOX(top_row), lbl_status);
    gtk_box_append(GTK_BOX(top_row), sw_wifi);
    gtk_box_append(GTK_BOX(box), top_row);

    // Scan Wi-Fi networks
    std::string raw_wifi = run_cmd("nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list 2>/dev/null | head -n 6");
    std::istringstream iss(raw_wifi);
    std::string line;

    GtkWidget* net_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, ':')) {
            parts.push_back(part);
        }
        if (parts.size() < 3) continue;

        bool in_use = (parts[0] == "*");
        std::string ssid = parts[1];
        if (ssid.empty()) continue;
        std::string signal = parts[2];

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_add_css_class(row, "item-row");

        std::string icon = in_use ? "● " : "○ ";
        std::string label_txt = icon + ssid + " (" + signal + "%)";
        GtkWidget* lbl = gtk_label_new(label_txt.c_str());
        gtk_widget_add_css_class(lbl, "value-label");
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);

        GtkWidget* btn_conn = gtk_button_new_with_label(in_use ? "Connected" : "Connect");
        gtk_widget_add_css_class(btn_conn, "action-btn");
        if (in_use) {
            gtk_widget_set_sensitive(btn_conn, FALSE);
        } else {
            auto* p_ssid = new std::string(ssid);
            g_signal_connect_data(btn_conn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                auto* s = static_cast<std::string*>(p);
                std::string cmd = "nmcli device wifi connect \"" + *s + "\"";
                exec_async(cmd);
                close_popup();
            }), p_ssid, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);
        }

        gtk_box_append(GTK_BOX(row), lbl);
        gtk_box_append(GTK_BOX(row), btn_conn);
        gtk_box_append(GTK_BOX(net_list), row);
    }

    gtk_box_append(GTK_BOX(box), net_list);

    // Quick Action button to open nmtui in foot if manual configuration is desired
    GtkWidget* btn_nmtui = gtk_button_new_with_label("󰈀 Connection Editor (NMTUI)");
    gtk_widget_add_css_class(btn_nmtui, "action-btn");
    g_signal_connect(btn_nmtui, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        exec_async("foot -c ~/.config/monolith/foot/foot.ini -e nmtui");
        close_popup();
    }), nullptr);
    gtk_box_append(GTK_BOX(box), btn_nmtui);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 3: BLUETOOTH
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_bluetooth_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    // Bluetooth power status
    std::string bt_status = run_cmd("bluetoothctl show 2>/dev/null");
    bool bt_powered = (bt_status.find("Powered: yes") != std::string::npos);

    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_status = gtk_label_new(bt_powered ? "󰂯 Bluetooth Enabled" : "󰂲 Bluetooth Disabled");
    gtk_widget_add_css_class(lbl_status, "section-label");
    gtk_widget_set_hexpand(lbl_status, TRUE);
    gtk_widget_set_halign(lbl_status, GTK_ALIGN_START);

    GtkWidget* sw_bt = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw_bt), bt_powered);
    g_signal_connect(sw_bt, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer) -> gboolean {
        std::string cmd = state ? "bluetoothctl power on" : "bluetoothctl power off";
        system(cmd.c_str());
        close_popup();
        return FALSE;
    }), nullptr);

    gtk_box_append(GTK_BOX(top_row), lbl_status);
    gtk_box_append(GTK_BOX(top_row), sw_bt);
    gtk_box_append(GTK_BOX(box), top_row);

    // Devices list
    std::string dev_out = run_cmd("bluetoothctl devices Paired 2>/dev/null");
    std::istringstream iss(dev_out);
    std::string line;

    GtkWidget* dev_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    int count = 0;

    while (std::getline(iss, line)) {
        if (line.rfind("Device ", 0) != 0) continue;
        std::stringstream ss(line);
        std::string dev_kw, mac, name;
        ss >> dev_kw >> mac;
        std::getline(ss >> std::ws, name);
        if (mac.empty()) continue;
        count++;

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_add_css_class(row, "item-row");

        std::string info = run_cmd("bluetoothctl info " + mac + " 2>/dev/null");
        bool is_connected = (info.find("Connected: yes") != std::string::npos);

        std::string title = (is_connected ? "● " : "○ ") + (name.empty() ? mac : name);
        GtkWidget* lbl = gtk_label_new(title.c_str());
        gtk_widget_add_css_class(lbl, "value-label");
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);

        GtkWidget* btn_conn = gtk_button_new_with_label(is_connected ? "Disconnect" : "Connect");
        gtk_widget_add_css_class(btn_conn, "action-btn");

        auto* p_mac = new std::string(mac);
        g_signal_connect_data(btn_conn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
            auto* m = static_cast<std::string*>(p);
            std::string cur_info = run_cmd("bluetoothctl info " + *m + " 2>/dev/null");
            if (cur_info.find("Connected: yes") != std::string::npos) {
                system(("bluetoothctl disconnect " + *m).c_str());
            } else {
                system(("bluetoothctl connect " + *m).c_str());
            }
            close_popup();
        }), p_mac, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);

        gtk_box_append(GTK_BOX(row), lbl);
        gtk_box_append(GTK_BOX(row), btn_conn);
        gtk_box_append(GTK_BOX(dev_list), row);
    }

    if (count == 0) {
        GtkWidget* empty_lbl = gtk_label_new(bt_powered ? "No paired devices found" : "Enable Bluetooth to manage devices");
        gtk_widget_add_css_class(empty_lbl, "section-label");
        gtk_widget_set_margin_top(empty_lbl, 8);
        gtk_widget_set_margin_bottom(empty_lbl, 8);
        gtk_box_append(GTK_BOX(dev_list), empty_lbl);
    }

    gtk_box_append(GTK_BOX(box), dev_list);

    // Bluetoothctl CLI launcher
    GtkWidget* btn_ctl = gtk_button_new_with_label("󰂯 Scan & Pair (Bluetoothctl)");
    gtk_widget_add_css_class(btn_ctl, "action-btn");
    g_signal_connect(btn_ctl, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        exec_async("foot -c ~/.config/monolith/foot/foot.ini --app-id=monolith-bt -e bluetoothctl");
        close_popup();
    }), nullptr);
    gtk_box_append(GTK_BOX(box), btn_ctl);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 4: SYSTEM RESOURCES (CPU & RAM & TOP PROCS)
// ──────────────────────────────────────────────────────────────────────────────
struct SysState {
    unsigned long long prev_idle = 0;
    unsigned long long prev_total = 0;
    GtkWidget* lbl_cpu = nullptr;
    GtkWidget* bar_cpu = nullptr;
    GtkWidget* lbl_mem = nullptr;
    GtkWidget* bar_mem = nullptr;
    GtkWidget* procs_box = nullptr;
};

static SysState g_sys;

static void update_sys_stats() {
    // Read CPU
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

    // Read Memory
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

    // Top processes
    if (g_sys.procs_box) {
        GtkWidget* child = gtk_widget_get_first_child(g_sys.procs_box);
        while (child) {
            GtkWidget* next = gtk_widget_get_next_sibling(child);
            gtk_box_remove(GTK_BOX(g_sys.procs_box), child);
            child = next;
        }

        std::string top_out = run_cmd("ps -eo comm,%cpu,%mem --sort=-%cpu 2>/dev/null | grep -v 'COMMAND' 2>/dev/null | head -n 4");
        std::istringstream iss(top_out);
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
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
            gtk_widget_add_css_class(l_stats, "section-label");

            gtk_box_append(GTK_BOX(r), l_comm);
            gtk_box_append(GTK_BOX(r), l_stats);
            gtk_box_append(GTK_BOX(g_sys.procs_box), r);
        }
    }
}

static GtkWidget* build_system_panel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    // CPU Section
    GtkWidget* cpu_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* lbl_c = gtk_label_new("󰍛 CPU Load");
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
    GtkWidget* lbl_m = gtk_label_new("󰘚 Memory Usage (RAM)");
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

    // Top processes header
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget* lbl_top = gtk_label_new("Top Background Processes");
    gtk_widget_add_css_class(lbl_top, "section-label");
    gtk_widget_set_halign(lbl_top, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), lbl_top);

    g_sys.procs_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(box), g_sys.procs_box);

    // Button to open btop
    GtkWidget* btn_btop = gtk_button_new_with_label("󰍛 Full Process Monitor (Btop)");
    gtk_widget_add_css_class(btn_btop, "action-btn");
    g_signal_connect(btn_btop, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        exec_async("foot -c ~/.config/monolith/foot/foot.ini --app-id=btop-monitor -e btop");
        close_popup();
    }), nullptr);
    gtk_box_append(GTK_BOX(box), btn_btop);

    // Initial read and periodic 1.5s refresh timer
    update_sys_stats();
    g_refresh_timer = g_timeout_add(1500, +[](gpointer) -> gboolean {
        update_sys_stats();
        return G_SOURCE_CONTINUE;
    }, nullptr);

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PANEL 5: POWER MENU
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
        {"󰐥  Power Off", "systemctl poweroff", true},
        {"󰍃  Log Out (Exit Hyprland)", "hyprctl dispatch exit", true}
    };

    for (const auto& act : actions) {
        GtkWidget* btn = gtk_button_new_with_label(act.label.c_str());
        gtk_widget_add_css_class(btn, act.is_danger ? "action-btn action-btn-danger" : "action-btn");
        gtk_widget_set_hexpand(btn, TRUE);
        gtk_widget_set_size_request(btn, -1, 34);

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
// WINDOW & GTK ACTIVATION
// ──────────────────────────────────────────────────────────────────────────────
static void on_app_activate(GApplication* app, gpointer) {
    g_app = GTK_APPLICATION(app);
    apply_css();

    g_win = gtk_window_new();
    gtk_application_add_window(g_app, GTK_WINDOW(g_win));

    // Layer-Shell Configuration
    gtk_layer_init_for_window(GTK_WINDOW(g_win));
    gtk_layer_set_layer(GTK_WINDOW(g_win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(g_win), 0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(g_win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    gtk_layer_set_namespace(GTK_WINDOW(g_win), "monolith-popup");

    // Anchor to TOP and RIGHT (flush beneath Waybar with zero gap)
    gtk_layer_set_anchor(GTK_WINDOW(g_win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(g_win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(g_win), GTK_LAYER_SHELL_EDGE_TOP, 29);
    gtk_layer_set_margin(GTK_WINDOW(g_win), GTK_LAYER_SHELL_EDGE_RIGHT, 8);

    gtk_widget_add_css_class(g_win, "monolith-popup");
    gtk_window_set_default_size(GTK_WINDOW(g_win), 330, -1);

    // Main box container
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(main_box, "popup-box");

    // Header bar
    GtkWidget* hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(hdr, "popup-header");

    std::string title_text = "󰒓 Monolith Menu";
    if (g_mode == "audio") title_text = "󰕾 Audio & Volume Control";
    else if (g_mode == "network") title_text = "󰖩 Network & Wi-Fi Connections";
    else if (g_mode == "bluetooth") title_text = "󰂯 Bluetooth Device Manager";
    else if (g_mode == "system") title_text = "󰍛 System Hardware Resources";
    else if (g_mode == "power") title_text = "󰐥 Power Management";

    GtkWidget* lbl_t = gtk_label_new(title_text.c_str());
    gtk_widget_add_css_class(lbl_t, "popup-title");
    gtk_widget_set_hexpand(lbl_t, TRUE);
    gtk_widget_set_halign(lbl_t, GTK_ALIGN_START);

    GtkWidget* btn_pin = gtk_button_new_with_label("󰐃");
    gtk_widget_add_css_class(btn_pin, "popup-btn-pin");
    gtk_widget_set_tooltip_text(btn_pin, "Pin Menu (Keep Open)");
    g_signal_connect(btn_pin, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
        g_pinned = !g_pinned;
        if (g_pinned) {
            gtk_widget_add_css_class(GTK_WIDGET(b), "pinned");
            gtk_widget_set_tooltip_text(GTK_WIDGET(b), "Unpin Menu (Auto-close enabled)");
            if (g_leave_timer > 0) {
                g_source_remove(g_leave_timer);
                g_leave_timer = 0;
            }
            if (g_initial_timer > 0) {
                g_source_remove(g_initial_timer);
                g_initial_timer = 0;
            }
        } else {
            gtk_widget_remove_css_class(GTK_WIDGET(b), "pinned");
            gtk_widget_set_tooltip_text(GTK_WIDGET(b), "Pin Menu (Keep Open)");
        }
    }), nullptr);

    GtkWidget* btn_close = gtk_button_new_with_label("󰅖");
    gtk_widget_add_css_class(btn_close, "popup-btn-close");
    gtk_widget_set_tooltip_text(btn_close, "Close Menu (Esc)");
    g_signal_connect(btn_close, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        close_popup();
    }), nullptr);

    gtk_box_append(GTK_BOX(hdr), lbl_t);
    gtk_box_append(GTK_BOX(hdr), btn_pin);
    gtk_box_append(GTK_BOX(hdr), btn_close);
    gtk_box_append(GTK_BOX(main_box), hdr);

    // Panel content based on mode
    GtkWidget* content = nullptr;
    if (g_mode == "audio") content = build_audio_panel();
    else if (g_mode == "network") content = build_network_panel();
    else if (g_mode == "bluetooth") content = build_bluetooth_panel();
    else if (g_mode == "system") content = build_system_panel();
    else if (g_mode == "power") content = build_power_panel();
    else content = build_system_panel();

    gtk_box_append(GTK_BOX(main_box), content);
    gtk_window_set_child(GTK_WINDOW(g_win), main_box);

    // Close on Escape key
    GtkEventControllerKey* key_ctl = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    g_signal_connect(key_ctl, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer) -> gboolean {
        if (keyval == GDK_KEY_Escape) {
            close_popup();
            return TRUE;
        }
        return FALSE;
    }), nullptr);
    gtk_widget_add_controller(g_win, GTK_EVENT_CONTROLLER(key_ctl));

    // Motion controller to track mouse hover smoothly without premature closing
    GtkEventControllerMotion* motion = GTK_EVENT_CONTROLLER_MOTION(gtk_event_controller_motion_new());
    g_signal_connect(motion, "enter", G_CALLBACK(+[](GtkEventControllerMotion*, double, double, gpointer) {
        if (g_leave_timer > 0) {
            g_source_remove(g_leave_timer);
            g_leave_timer = 0;
        }
        if (g_initial_timer > 0) {
            g_source_remove(g_initial_timer);
            g_initial_timer = 0;
        }
    }), nullptr);

    g_signal_connect(motion, "leave", G_CALLBACK(+[](GtkEventControllerMotion*, gpointer) {
        if (g_pinned) return;
        if (g_leave_timer > 0) g_source_remove(g_leave_timer);
        g_leave_timer = g_timeout_add(600, +[](gpointer) -> gboolean {
            if (!g_pinned) close_popup();
            return G_SOURCE_REMOVE;
        }, nullptr);
    }), nullptr);

    gtk_widget_add_controller(g_win, GTK_EVENT_CONTROLLER(motion));

    // Initial grace timeout: If user doesn't hover onto the popup within 2.5s, close it unless pinned
    g_initial_timer = g_timeout_add(2500, +[](gpointer) -> gboolean {
        if (!g_pinned) close_popup();
        return G_SOURCE_REMOVE;
    }, nullptr);

    gtk_window_present(GTK_WINDOW(g_win));
}

int main(int argc, char* argv[]) {
    std::string requested_mode = "system";
    if (argc > 1) {
        requested_mode = argv[1];
    }
    g_mode = requested_mode;

    // Toggle logic: If a popup is already running or stale file exists
    pid_t old_pid = 0;
    std::string old_type = "";
    {
        std::ifstream pf("/tmp/.monolith_popup.pid");
        if (pf.is_open()) pf >> old_pid;
    }
    {
        std::ifstream tf("/tmp/.monolith_popup.type");
        if (tf.is_open()) tf >> old_type;
    }

    if (old_pid > 0) {
        if (kill(old_pid, 0) == 0) {
            // Process is currently alive
            kill(old_pid, SIGTERM);
            unlink("/tmp/.monolith_popup.pid");
            unlink("/tmp/.monolith_popup.type");

            // If user clicked the exact same module icon, treat as toggle-off!
            if (old_type == requested_mode) {
                return 0;
            }
        } else {
            // Stale file
            unlink("/tmp/.monolith_popup.pid");
            unlink("/tmp/.monolith_popup.type");
        }
    }

    // Save active PID & type immediately
    {
        std::ofstream pf("/tmp/.monolith_popup.pid");
        pf << getpid() << "\n";
    }
    {
        std::ofstream tf("/tmp/.monolith_popup.type");
        tf << requested_mode << "\n";
    }

    GtkApplication* app = gtk_application_new("org.monolith.popup", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), nullptr);

    int status = g_application_run(G_APPLICATION(app), 0, nullptr);
    g_object_unref(app);

    unlink("/tmp/.monolith_popup.pid");
    unlink("/tmp/.monolith_popup.type");
    return status;
}
