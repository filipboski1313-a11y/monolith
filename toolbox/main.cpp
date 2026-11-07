#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <cairo.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════════════════════
// MONOLITH NATIVE EDGE DRAWER (monolith-toolbox v2.1)
// Architectural OLED Minimalist Side-Dock Utility Suite
//   - Centered 180px left & right edge handles (never blocks Waybar or scrollbars)
//   - Dwell-hover (260ms) OR click to smoothly slide out via GtkRevealer
//   - KEYBOARD_MODE_ON_DEMAND + focus-loss auto-retract (unless pinned via 󰐃)
//   - Custom segmented tab dock (Chrono • Calc • Sketch • Notes)
// ══════════════════════════════════════════════════════════════════════════════

struct StrokePoint { double x, y; };
struct Stroke {
    std::vector<StrokePoint> points;
    double r, g, b;
    double width;
};

struct AppState {
    GtkApplication* app = nullptr;
    GtkWidget* left_trigger = nullptr;
    GtkWidget* right_trigger = nullptr;

    struct TriggerState {
        std::string edge;
        bool hovered = false;
        double cursor_y = 90.0;
        guint dwell_timer = 0;
        GtkWidget* da = nullptr;
    };
    TriggerState left_trig{"left", false, 90.0, 0, nullptr};
    TriggerState right_trig{"right", false, 90.0, 0, nullptr};

    GtkWidget* popup_win = nullptr;
    GtkWidget* revealer = nullptr;
    GtkWidget* stack = nullptr;
    GtkWidget* pin_btn = nullptr;
    std::vector<std::pair<std::string, GtkWidget*>> tab_btns;

    bool popup_visible = false;
    bool is_pinned = false;
    bool in_open_grace = false;
    guint open_grace_timer = 0;
    guint hide_timer = 0;
    std::string current_edge = "left";
    std::string active_tab = "timer";

    // Chrono / Pomodoro state
    bool sw_running = false;
    bool countdown_mode = false;
    int active_preset_min = 0; // 0 = chrono
    long long countdown_target_ms = 0;
    std::chrono::steady_clock::time_point sw_start_time;
    std::chrono::milliseconds sw_elapsed{0};
    long long last_lap_total_ms = 0;
    guint sw_timer_id = 0;
    GtkWidget* sw_label = nullptr;
    GtkWidget* sw_mode_label = nullptr;
    GtkWidget* sw_prog_bar = nullptr;
    GtkWidget* sw_start_btn = nullptr;
    GtkWidget* sw_lap_box = nullptr;
    std::vector<std::pair<int, GtkWidget*>> preset_btns;
    int sw_lap_count = 0;

    // Calculator state
    std::string calc_expr = "";
    std::string calc_display = "0";
    GtkWidget* calc_expr_label = nullptr;
    GtkWidget* calc_res_label = nullptr;
    GtkWidget* calc_hist_box = nullptr;

    // Sketch Canvas state
    GtkWidget* draw_area = nullptr;
    std::vector<Stroke> strokes;
    Stroke current_stroke;
    bool is_drawing = false;
    int active_color_idx = 0;
    double cur_r = 0.96, cur_g = 0.96, cur_b = 0.97;
    double cur_brush_width = 3.0;
    std::vector<GtkWidget*> color_btns;
    std::vector<std::pair<double, GtkWidget*>> size_btns;

    // Notes state
    GtkTextBuffer* notes_buffer = nullptr;
    GtkWidget* notes_stats_lbl = nullptr;
    std::string notes_path;
} g_state;

static void show_popup(const std::string& edge);
static void toggle_popup(const std::string& edge);
static void hide_popup();
static void switch_tab(const std::string& tab_id);

// ──────────────────────────────────────────────────────────────────────────────
// TAB 1: CHRONO & POMODORO FOCUS TIMER
// ──────────────────────────────────────────────────────────────────────────────
static std::string format_time(long long ms) {
    if (ms < 0) ms = 0;
    long long total_secs = ms / 1000;
    long long m = total_secs / 60;
    long long s = total_secs % 60;
    long long c = (ms % 1000) / 10;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << m << ":"
        << std::setfill('0') << std::setw(2) << s << "."
        << std::setfill('0') << std::setw(2) << c;
    return oss.str();
}

static void update_preset_buttons_ui() {
    for (auto& kv : g_state.preset_btns) {
        if (kv.first == g_state.active_preset_min) {
            gtk_widget_add_css_class(kv.second, "seg-btn-active");
        } else {
            gtk_widget_remove_css_class(kv.second, "seg-btn-active");
        }
    }
}

static gboolean sw_tick(gpointer) {
    if (!g_state.sw_running) return G_SOURCE_REMOVE;
    auto now = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_state.sw_start_time);
    long long elapsed_ms = (g_state.sw_elapsed + diff).count();

    if (g_state.countdown_mode && g_state.countdown_target_ms > 0) {
        long long rem = g_state.countdown_target_ms - elapsed_ms;
        double frac = std::clamp(1.0 - (double)elapsed_ms / (double)g_state.countdown_target_ms, 0.0, 1.0);
        if (g_state.sw_prog_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_state.sw_prog_bar), frac);

        if (rem <= 0) {
            g_state.sw_running = false;
            g_state.sw_elapsed = std::chrono::milliseconds{g_state.countdown_target_ms};
            g_state.sw_timer_id = 0;
            gtk_label_set_text(GTK_LABEL(g_state.sw_label), "00:00.00");
            gtk_button_set_label(GTK_BUTTON(g_state.sw_start_btn), "START");
            gtk_widget_add_css_class(GTK_WIDGET(g_state.sw_start_btn), "action-btn-primary");
            int r = std::system("notify-send -a Monolith -u critical 'Focus Session Complete' 'Countdown finished.' >/dev/null 2>&1 &");
            (void)r;
            return G_SOURCE_REMOVE;
        }
        gtk_label_set_text(GTK_LABEL(g_state.sw_label), format_time(rem).c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(g_state.sw_label), format_time(elapsed_ms).c_str());
        if (g_state.sw_prog_bar) {
            double cycle = std::fmod(elapsed_ms / 1000.0, 60.0) / 60.0;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_state.sw_prog_bar), cycle);
        }
    }
    return G_SOURCE_CONTINUE;
}

static void sw_toggle_start(GtkButton*, gpointer) {
    if (!g_state.sw_running) {
        if (g_state.countdown_mode && g_state.sw_elapsed.count() >= g_state.countdown_target_ms) {
            g_state.sw_elapsed = std::chrono::milliseconds{0};
        }
        g_state.sw_running = true;
        g_state.sw_start_time = std::chrono::steady_clock::now();
        gtk_button_set_label(GTK_BUTTON(g_state.sw_start_btn), "PAUSE");
        gtk_widget_remove_css_class(GTK_WIDGET(g_state.sw_start_btn), "action-btn-primary");
        g_state.sw_timer_id = g_timeout_add(16, sw_tick, nullptr);
    } else {
        g_state.sw_running = false;
        auto now = std::chrono::steady_clock::now();
        g_state.sw_elapsed += std::chrono::duration_cast<std::chrono::milliseconds>(now - g_state.sw_start_time);
        gtk_button_set_label(GTK_BUTTON(g_state.sw_start_btn), "START");
        gtk_widget_add_css_class(GTK_WIDGET(g_state.sw_start_btn), "action-btn-primary");
        if (g_state.sw_timer_id > 0) {
            g_source_remove(g_state.sw_timer_id);
            g_state.sw_timer_id = 0;
        }
    }
}

static void sw_reset(GtkButton*, gpointer) {
    g_state.sw_running = false;
    g_state.sw_elapsed = std::chrono::milliseconds{0};
    g_state.last_lap_total_ms = 0;
    if (g_state.sw_timer_id > 0) {
        g_source_remove(g_state.sw_timer_id);
        g_state.sw_timer_id = 0;
    }

    if (g_state.countdown_mode && g_state.countdown_target_ms > 0) {
        gtk_label_set_text(GTK_LABEL(g_state.sw_label), format_time(g_state.countdown_target_ms).c_str());
        if (g_state.sw_prog_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_state.sw_prog_bar), 1.0);
    } else {
        gtk_label_set_text(GTK_LABEL(g_state.sw_label), "00:00.00");
        if (g_state.sw_prog_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_state.sw_prog_bar), 0.0);
    }

    gtk_button_set_label(GTK_BUTTON(g_state.sw_start_btn), "START");
    gtk_widget_add_css_class(GTK_WIDGET(g_state.sw_start_btn), "action-btn-primary");

    GtkWidget* child = gtk_widget_get_first_child(g_state.sw_lap_box);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(g_state.sw_lap_box), child);
        child = next;
    }
    g_state.sw_lap_count = 0;
}

static void sw_select_mode(int minutes) {
    g_state.active_preset_min = minutes;
    g_state.countdown_mode = (minutes > 0);
    g_state.countdown_target_ms = (long long)minutes * 60LL * 1000LL;
    update_preset_buttons_ui();
    sw_reset(nullptr, nullptr);
    if (g_state.sw_mode_label) {
        std::string txt = (minutes == 0) ? "CHRONOGRAPH" : ("FOCUS  ·  " + std::to_string(minutes) + " MIN");
        gtk_label_set_text(GTK_LABEL(g_state.sw_mode_label), txt.c_str());
    }
}

static void sw_lap(GtkButton*, gpointer) {
    long long total_ms = g_state.sw_elapsed.count();
    if (g_state.sw_running) {
        auto now = std::chrono::steady_clock::now();
        total_ms += std::chrono::duration_cast<std::chrono::milliseconds>(now - g_state.sw_start_time).count();
    }
    if (total_ms == 0) return;

    long long delta_ms = total_ms - g_state.last_lap_total_ms;
    g_state.last_lap_total_ms = total_ms;

    g_state.sw_lap_count++;
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "lap-row");

    std::ostringstream idx_s, del_s;
    idx_s << "#" << std::setw(2) << std::setfill('0') << g_state.sw_lap_count;
    del_s << "+" << format_time(delta_ms);

    GtkWidget* l_idx = gtk_label_new(idx_s.str().c_str());
    gtk_widget_add_css_class(l_idx, "bento-dim");
    GtkWidget* l_del = gtk_label_new(del_s.str().c_str());
    gtk_widget_add_css_class(l_del, "bento-dim");
    gtk_widget_set_hexpand(l_del, TRUE);
    GtkWidget* l_tot = gtk_label_new(format_time(total_ms).c_str());
    gtk_widget_add_css_class(l_tot, "bento-value");

    gtk_box_append(GTK_BOX(row), l_idx);
    gtk_box_append(GTK_BOX(row), l_del);
    gtk_box_append(GTK_BOX(row), l_tot);
    gtk_box_prepend(GTK_BOX(g_state.sw_lap_box), row);
}

static GtkWidget* build_stopwatch_tab() {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(root, 4);

    // Mode Selector Segmented Group
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_widget_add_css_class(seg, "seg-group");
    int modes[] = {0, 5, 15, 25, 45};
    g_state.preset_btns.clear();
    for (int m : modes) {
        std::string label = (m == 0) ? "Chrono" : (std::to_string(m) + "m");
        GtkWidget* b = gtk_button_new_with_label(label.c_str());
        gtk_widget_add_css_class(b, "seg-btn");
        if (m == 0) gtk_widget_add_css_class(b, "seg-btn-active");
        gtk_widget_set_hexpand(b, TRUE);
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            sw_select_mode(GPOINTER_TO_INT(data));
        }), GINT_TO_POINTER(m));
        g_state.preset_btns.push_back({m, b});
        gtk_box_append(GTK_BOX(seg), b);
    }
    gtk_box_append(GTK_BOX(root), seg);

    // Hero Readout Card
    GtkWidget* hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(hero, "bento-card");

    g_state.sw_mode_label = gtk_label_new("CHRONOGRAPH");
    gtk_widget_add_css_class(g_state.sw_mode_label, "bento-title");
    gtk_widget_set_halign(g_state.sw_mode_label, GTK_ALIGN_CENTER);

    g_state.sw_label = gtk_label_new("00:00.00");
    gtk_widget_add_css_class(g_state.sw_label, "timer-hero");
    gtk_widget_set_halign(g_state.sw_label, GTK_ALIGN_CENTER);

    g_state.sw_prog_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_state.sw_prog_bar), 0.0);

    gtk_box_append(GTK_BOX(hero), g_state.sw_mode_label);
    gtk_box_append(GTK_BOX(hero), g_state.sw_label);
    gtk_box_append(GTK_BOX(hero), g_state.sw_prog_bar);
    gtk_box_append(GTK_BOX(root), hero);

    // Controls Row
    GtkWidget* ctrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    g_state.sw_start_btn = gtk_button_new_with_label("START");
    gtk_widget_add_css_class(g_state.sw_start_btn, "action-btn");
    gtk_widget_add_css_class(g_state.sw_start_btn, "action-btn-primary");
    gtk_widget_set_hexpand(g_state.sw_start_btn, TRUE);
    g_signal_connect(g_state.sw_start_btn, "clicked", G_CALLBACK(sw_toggle_start), nullptr);

    GtkWidget* btn_lap = gtk_button_new_with_label("LAP");
    gtk_widget_add_css_class(btn_lap, "action-btn");
    gtk_widget_set_hexpand(btn_lap, TRUE);
    g_signal_connect(btn_lap, "clicked", G_CALLBACK(sw_lap), nullptr);

    GtkWidget* btn_rst = gtk_button_new_with_label("RESET");
    gtk_widget_add_css_class(btn_rst, "action-btn");
    gtk_widget_set_hexpand(btn_rst, TRUE);
    g_signal_connect(btn_rst, "clicked", G_CALLBACK(sw_reset), nullptr);

    gtk_box_append(GTK_BOX(ctrl), g_state.sw_start_btn);
    gtk_box_append(GTK_BOX(ctrl), btn_lap);
    gtk_box_append(GTK_BOX(ctrl), btn_rst);
    gtk_box_append(GTK_BOX(root), ctrl);

    // Laps Scroll Card
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scroll, "bento-card");
    gtk_widget_set_vexpand(scroll, TRUE);
    g_state.sw_lap_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), g_state.sw_lap_box);
    gtk_box_append(GTK_BOX(root), scroll);

    return root;
}

// ──────────────────────────────────────────────────────────────────────────────
// TAB 2: SCIENTIFIC CALCULATOR
// ──────────────────────────────────────────────────────────────────────────────
struct MathParser {
    const std::string& s;
    size_t pos = 0;
    bool ok = true;

    void skip_ws() {
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) pos++;
    }
    double parse_expr() {
        double v = parse_term();
        while (ok) {
            skip_ws();
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
                char op = s[pos++];
                double r = parse_term();
                v = (op == '+') ? (v + r) : (v - r);
            } else break;
        }
        return v;
    }
    double parse_term() {
        double v = parse_power();
        while (ok) {
            skip_ws();
            if (pos < s.size() && (s[pos] == '*' || s[pos] == '/' || s[pos] == '%')) {
                char op = s[pos++];
                double r = parse_power();
                if (op == '*') v *= r;
                else if (op == '/') {
                    if (std::abs(r) < 1e-15) { ok = false; return 0.0; }
                    v /= r;
                } else {
                    if (std::abs(r) < 1e-15) { ok = false; return 0.0; }
                    v = std::fmod(v, r);
                }
            } else break;
        }
        return v;
    }
    double parse_power() {
        double base = parse_unary();
        skip_ws();
        if (ok && pos < s.size() && s[pos] == '^') {
            pos++;
            double exp = parse_power();
            base = std::pow(base, exp);
        }
        return base;
    }
    double parse_unary() {
        skip_ws();
        if (pos < s.size() && s[pos] == '+') { pos++; return parse_unary(); }
        if (pos < s.size() && s[pos] == '-') { pos++; return -parse_unary(); }
        return parse_primary();
    }
    double parse_primary() {
        skip_ws();
        if (pos >= s.size()) { ok = false; return 0.0; }
        if (s[pos] == '(') {
            pos++;
            double v = parse_expr();
            skip_ws();
            if (pos < s.size() && s[pos] == ')') pos++;
            return v;
        }
        if (std::isalpha((unsigned char)s[pos])) {
            size_t start = pos;
            while (pos < s.size() && std::isalpha((unsigned char)s[pos])) pos++;
            std::string ident = s.substr(start, pos - start);
            if (ident == "pi" || ident == "PI") return M_PI;
            if (ident == "e") return M_E;
            double arg = parse_primary();
            if (ident == "sqrt") {
                if (arg < 0) { ok = false; return 0.0; }
                return std::sqrt(arg);
            }
            if (ident == "sin") return std::sin(arg);
            if (ident == "cos") return std::cos(arg);
            if (ident == "tan") return std::tan(arg);
            ok = false;
            return 0.0;
        }
        if (std::isdigit((unsigned char)s[pos]) || s[pos] == '.') {
            size_t start = pos;
            while (pos < s.size() && (std::isdigit((unsigned char)s[pos]) || s[pos] == '.')) pos++;
            try { return std::stod(s.substr(start, pos - start)); }
            catch (...) { ok = false; return 0.0; }
        }
        ok = false;
        return 0.0;
    }
};

static double eval_expression(const std::string& expr, bool& ok) {
    MathParser p{expr, 0, true};
    double res = p.parse_expr();
    p.skip_ws();
    if (p.pos != expr.size() || !std::isfinite(res)) p.ok = false;
    ok = p.ok;
    return res;
}

static std::string format_number(double res) {
    if (std::abs(res - std::round(res)) < 1e-9 && std::abs(res) < 1e15) {
        return std::to_string(static_cast<long long>(std::round(res)));
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6) << res;
    std::string s = ss.str();
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

static void add_calc_history(const std::string& expr, const std::string& result) {
    if (!g_state.calc_hist_box) return;
    GtkWidget* btn = gtk_button_new_with_label((expr + " = " + result).c_str());
    gtk_widget_add_css_class(btn, "hist-chip");
    auto* res_copy = new std::string(result);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* val = static_cast<std::string*>(data);
        g_state.calc_expr = *val;
        g_state.calc_display = *val;
        gtk_label_set_text(GTK_LABEL(g_state.calc_expr_label), g_state.calc_expr.c_str());
        gtk_label_set_text(GTK_LABEL(g_state.calc_res_label), g_state.calc_display.c_str());
    }), res_copy, [](gpointer d, GClosure*) { delete static_cast<std::string*>(d); }, (GConnectFlags)0);
    gtk_box_prepend(GTK_BOX(g_state.calc_hist_box), btn);
}

static void calc_press(const std::string& key) {
    if (key == "C") {
        g_state.calc_expr = "";
        g_state.calc_display = "0";
    } else if (key == "DEL") {
        if (!g_state.calc_expr.empty()) g_state.calc_expr.pop_back();
        g_state.calc_display = g_state.calc_expr.empty() ? "0" : g_state.calc_expr;
    } else if (key == "COPY") {
        GdkClipboard* cb = gdk_display_get_clipboard(gdk_display_get_default());
        if (cb) gdk_clipboard_set_text(cb, g_state.calc_display.c_str());
        return;
    } else if (key == "=") {
        if (!g_state.calc_expr.empty()) {
            bool ok = false;
            std::string old_expr = g_state.calc_expr;
            double res = eval_expression(g_state.calc_expr, ok);
            if (ok) {
                std::string formatted = format_number(res);
                add_calc_history(old_expr, formatted);
                g_state.calc_display = formatted;
                g_state.calc_expr = formatted;
            } else {
                g_state.calc_display = "Syntax Error";
            }
        }
    } else if (key == "sqrt") {
        if (g_state.calc_display == "0" || g_state.calc_display == "Syntax Error") g_state.calc_expr = "sqrt(";
        else g_state.calc_expr += "sqrt(";
        g_state.calc_display = g_state.calc_expr;
    } else {
        if ((g_state.calc_display == "0" || g_state.calc_display == "Syntax Error") &&
            key != "." && key != "+" && key != "-" && key != "*" && key != "/" && key != "^" && key != "%") {
            g_state.calc_expr = key;
        } else {
            g_state.calc_expr += key;
        }
        g_state.calc_display = g_state.calc_expr;
    }

    gtk_label_set_text(GTK_LABEL(g_state.calc_expr_label), g_state.calc_expr.c_str());
    gtk_label_set_text(GTK_LABEL(g_state.calc_res_label), g_state.calc_display.c_str());
}

static GtkWidget* build_calc_tab() {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(root, 4);

    // Display Screen Card
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(screen, "bento-card");

    GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* h_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(h_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand(h_scroll, TRUE);
    gtk_widget_set_size_request(h_scroll, -1, 22);
    g_state.calc_hist_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(h_scroll), g_state.calc_hist_box);

    GtkWidget* btn_copy = gtk_button_new_with_label("󰅍 Copy");
    gtk_widget_add_css_class(btn_copy, "hist-chip");
    g_signal_connect_swapped(btn_copy, "clicked", G_CALLBACK(+[](const char* k) { calc_press(k); }), (gpointer)"COPY");

    gtk_box_append(GTK_BOX(top_row), h_scroll);
    gtk_box_append(GTK_BOX(top_row), btn_copy);
    gtk_box_append(GTK_BOX(screen), top_row);

    g_state.calc_expr_label = gtk_label_new("");
    gtk_widget_add_css_class(g_state.calc_expr_label, "bento-dim");
    gtk_widget_set_halign(g_state.calc_expr_label, GTK_ALIGN_END);

    g_state.calc_res_label = gtk_label_new("0");
    gtk_widget_add_css_class(g_state.calc_res_label, "calc-hero");
    gtk_widget_set_halign(g_state.calc_res_label, GTK_ALIGN_END);
    gtk_label_set_selectable(GTK_LABEL(g_state.calc_res_label), TRUE);

    gtk_box_append(GTK_BOX(screen), g_state.calc_expr_label);
    gtk_box_append(GTK_BOX(screen), g_state.calc_res_label);
    gtk_box_append(GTK_BOX(root), screen);

    // 5x5 Architectural Keypad
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_widget_set_vexpand(grid, TRUE);

    const char* keys[5][5] = {
        {"C",   "(",   ")",   "^",    "DEL"},
        {"7",   "8",   "9",   "/",    "sqrt"},
        {"4",   "5",   "6",   "*",    "%"},
        {"1",   "2",   "3",   "-",    "pi"},
        {"0",   ".",   "=",   "+",    ""}
    };

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 5; ++c) {
            std::string k = keys[r][c];
            if (k.empty()) continue;

            std::string label = k;
            if (k == "sqrt") label = "√";
            else if (k == "pi") label = "π";
            else if (k == "DEL") label = "⌫";
            else if (k == "*") label = "×";
            else if (k == "/") label = "÷";

            GtkWidget* b = gtk_button_new_with_label(label.c_str());
            gtk_widget_add_css_class(b, "key-btn");
            if (k == "=") {
                gtk_widget_add_css_class(b, "action-btn-primary");
            } else if (k == "C" || k == "DEL") {
                gtk_widget_add_css_class(b, "key-danger");
            } else if (!std::isdigit((unsigned char)k[0]) && k != ".") {
                gtk_widget_add_css_class(b, "key-op");
            }

            g_signal_connect_swapped(b, "clicked", G_CALLBACK(+[](const char* str) {
                calc_press(str);
            }), (gpointer)keys[r][c]);

            if (r == 4 && c == 2) {
                gtk_grid_attach(GTK_GRID(grid), b, 2, 4, 1, 1);
            } else if (r == 4 && c == 3) {
                gtk_grid_attach(GTK_GRID(grid), b, 3, 4, 2, 1);
            } else {
                gtk_grid_attach(GTK_GRID(grid), b, c, r, 1, 1);
            }
        }
    }

    gtk_box_append(GTK_BOX(root), grid);
    return root;
}

// ──────────────────────────────────────────────────────────────────────────────
// TAB 3: ARCHITECTURAL SKETCH CANVAS
// ──────────────────────────────────────────────────────────────────────────────
static void draw_canvas_cb(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer) {
    // Deep OLED surface
    cairo_set_source_rgb(cr, 0.02, 0.02, 0.03);
    cairo_paint(cr);

    // Subtle architectural dot grid
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    for (int y = 14; y < height; y += 18) {
        for (int x = 14; x < width; x += 18) {
            cairo_rectangle(cr, x, y, 1.0, 1.0);
        }
    }
    cairo_fill(cr);

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    auto render_stroke = [&](const Stroke& st) {
        if (st.points.empty()) return;
        cairo_set_source_rgb(cr, st.r, st.g, st.b);
        cairo_set_line_width(cr, st.width);
        cairo_move_to(cr, st.points[0].x, st.points[0].y);
        for (size_t i = 1; i < st.points.size(); ++i) {
            cairo_line_to(cr, st.points[i].x, st.points[i].y);
        }
        if (st.points.size() == 1) {
            cairo_line_to(cr, st.points[0].x + 0.1, st.points[0].y + 0.1);
        }
        cairo_stroke(cr);
    };

    for (const auto& st : g_state.strokes) render_stroke(st);
    if (g_state.is_drawing) render_stroke(g_state.current_stroke);
}

static void draw_undo(GtkButton*, gpointer) {
    if (!g_state.strokes.empty()) {
        g_state.strokes.pop_back();
        gtk_widget_queue_draw(g_state.draw_area);
    }
}

static void draw_clear(GtkButton*, gpointer) {
    g_state.strokes.clear();
    g_state.current_stroke.points.clear();
    gtk_widget_queue_draw(g_state.draw_area);
}

static cairo_surface_t* render_canvas_to_surface() {
    int w = gtk_widget_get_width(g_state.draw_area);
    int h = gtk_widget_get_height(g_state.draw_area);
    if (w <= 0) w = 330;
    if (h <= 0) h = 260;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    draw_canvas_cb(nullptr, cr, w, h, nullptr);
    cairo_destroy(cr);
    return surface;
}

static void draw_save_png(GtkButton*, gpointer) {
    cairo_surface_t* s = render_canvas_to_surface();
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream fn;
    const char* home = getenv("HOME");
    std::string dir = std::string(home ? home : "/tmp") + "/Pictures";
    fs::create_directories(dir);
    fn << dir << "/monolith-sketch-" << std::put_time(std::localtime(&t), "%Y%m%d-%H%M%S") << ".png";
    cairo_surface_write_to_png(s, fn.str().c_str());
    cairo_surface_destroy(s);
}

static void draw_copy_clipboard(GtkButton*, gpointer) {
    cairo_surface_t* s = render_canvas_to_surface();
    std::string tmp = "/tmp/monolith_sketch_clip.png";
    cairo_surface_write_to_png(s, tmp.c_str());
    cairo_surface_destroy(s);
    int r = std::system(("wl-copy < " + tmp + " >/dev/null 2>&1 &").c_str());
    (void)r;
}

static GtkWidget* build_draw_tab() {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(root, 4);

    // Top Palette & Brush Width Bar
    GtkWidget* top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    struct ColSpec { const char* css; double r, g, b; };
    ColSpec cols[] = {
        {"swatch-white",   0.96, 0.96, 0.97},
        {"swatch-cyan",    0.22, 0.74, 0.97},
        {"swatch-emerald", 0.29, 0.87, 0.50},
        {"swatch-amber",   0.96, 0.62, 0.04},
        {"swatch-red",     0.94, 0.27, 0.27},
        {"swatch-eraser",  0.02, 0.02, 0.03}
    };

    g_state.color_btns.clear();
    for (int i = 0; i < 6; ++i) {
        GtkWidget* btn = gtk_button_new();
        gtk_widget_set_size_request(btn, 22, 22);
        gtk_widget_add_css_class(btn, "swatch-btn");
        gtk_widget_add_css_class(btn, cols[i].css);
        if (i == 0) gtk_widget_add_css_class(btn, "swatch-active");

        struct SwCtx { int idx; double r, g, b; };
        auto* ctx = new SwCtx{i, cols[i].r, cols[i].g, cols[i].b};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* c = static_cast<SwCtx*>(data);
            g_state.active_color_idx = c->idx;
            g_state.cur_r = c->r;
            g_state.cur_g = c->g;
            g_state.cur_b = c->b;
            for (size_t j = 0; j < g_state.color_btns.size(); ++j) {
                if ((int)j == c->idx) gtk_widget_add_css_class(g_state.color_btns[j], "swatch-active");
                else gtk_widget_remove_css_class(g_state.color_btns[j], "swatch-active");
            }
        }), ctx, [](gpointer d, GClosure*) { delete static_cast<SwCtx*>(d); }, (GConnectFlags)0);

        g_state.color_btns.push_back(btn);
        gtk_box_append(GTK_BOX(top_bar), btn);
    }

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top_bar), spacer);

    // Brush size pills
    GtkWidget* sz_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(sz_group, "seg-group");
    double sizes[] = {2.0, 4.0, 8.0};
    const char* sz_labels[] = {"Fine", "Med", "Bold"};
    g_state.size_btns.clear();
    for (int i = 0; i < 3; ++i) {
        GtkWidget* sb = gtk_button_new_with_label(sz_labels[i]);
        gtk_widget_add_css_class(sb, "seg-btn");
        if (i == 0) gtk_widget_add_css_class(sb, "seg-btn-active");
        auto* w_ptr = new double(sizes[i]);
        g_signal_connect_data(sb, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            double w = *static_cast<double*>(data);
            g_state.cur_brush_width = w;
            for (auto& kv : g_state.size_btns) {
                if (std::abs(kv.first - w) < 0.1) gtk_widget_add_css_class(kv.second, "seg-btn-active");
                else gtk_widget_remove_css_class(kv.second, "seg-btn-active");
            }
        }), w_ptr, [](gpointer p, GClosure*) { delete static_cast<double*>(p); }, (GConnectFlags)0);
        g_state.size_btns.push_back({sizes[i], sb});
        gtk_box_append(GTK_BOX(sz_group), sb);
    }
    gtk_box_append(GTK_BOX(top_bar), sz_group);
    gtk_box_append(GTK_BOX(root), top_bar);

    // Canvas Area
    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "canvas-card");
    gtk_widget_set_vexpand(frame, TRUE);

    g_state.draw_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(g_state.draw_area, TRUE);
    gtk_widget_set_hexpand(g_state.draw_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_state.draw_area), draw_canvas_cb, nullptr, nullptr);

    GtkGesture* drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(+[](GtkGestureDrag*, double x, double y, gpointer) {
        g_state.is_drawing = true;
        g_state.current_stroke.points.clear();
        g_state.current_stroke.r = g_state.cur_r;
        g_state.current_stroke.g = g_state.cur_g;
        g_state.current_stroke.b = g_state.cur_b;
        g_state.current_stroke.width = (g_state.active_color_idx == 5) ? (g_state.cur_brush_width * 3.5) : g_state.cur_brush_width;
        g_state.current_stroke.points.push_back({x, y});
        gtk_widget_queue_draw(g_state.draw_area);
    }), nullptr);
    g_signal_connect(drag, "drag-update", G_CALLBACK((+[](GtkGestureDrag* g, double ox, double oy, gpointer) {
        if (!g_state.is_drawing) return;
        double sx = 0;
        double sy = 0;
        gtk_gesture_drag_get_start_point(g, &sx, &sy);
        g_state.current_stroke.points.push_back({sx + ox, sy + oy});
        gtk_widget_queue_draw(g_state.draw_area);
    })), nullptr);
    g_signal_connect(drag, "drag-end", G_CALLBACK(+[](GtkGestureDrag*, double, double, gpointer) {
        if (g_state.is_drawing) {
            g_state.is_drawing = false;
            if (g_state.strokes.size() >= 30) g_state.strokes.erase(g_state.strokes.begin());
            g_state.strokes.push_back(g_state.current_stroke);
            g_state.current_stroke.points.clear();
            gtk_widget_queue_draw(g_state.draw_area);
        }
    }), nullptr);
    gtk_widget_add_controller(g_state.draw_area, GTK_EVENT_CONTROLLER(drag));
    gtk_box_append(GTK_BOX(frame), g_state.draw_area);
    gtk_box_append(GTK_BOX(root), frame);

    // Bottom Actions Bar
    GtkWidget* bot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    auto make_act = [&](const char* lbl, GCallback cb) {
        GtkWidget* b = gtk_button_new_with_label(lbl);
        gtk_widget_add_css_class(b, "action-btn");
        gtk_widget_set_hexpand(b, TRUE);
        g_signal_connect(b, "clicked", cb, nullptr);
        gtk_box_append(GTK_BOX(bot), b);
    };
    make_act("󰕌 Undo", G_CALLBACK(draw_undo));
    make_act("󰃢 Clear", G_CALLBACK(draw_clear));
    make_act("󰅍 Copy", G_CALLBACK(draw_copy_clipboard));
    make_act("󰆓 Save", G_CALLBACK(draw_save_png));
    gtk_box_append(GTK_BOX(root), bot);

    return root;
}

// ──────────────────────────────────────────────────────────────────────────────
// TAB 4: SCRATCHPAD NOTES
// ──────────────────────────────────────────────────────────────────────────────
static void update_notes_stats() {
    if (!g_state.notes_buffer || !g_state.notes_stats_lbl) return;
    int chars = gtk_text_buffer_get_char_count(g_state.notes_buffer);
    int lines = gtk_text_buffer_get_line_count(g_state.notes_buffer);
    std::string st = std::to_string(lines) + " LN  ·  " + std::to_string(chars) + " CH";
    gtk_label_set_text(GTK_LABEL(g_state.notes_stats_lbl), st.c_str());
}

static void notes_save() {
    if (!g_state.notes_buffer || g_state.notes_path.empty()) return;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(g_state.notes_buffer, &s, &e);
    char* text = gtk_text_buffer_get_text(g_state.notes_buffer, &s, &e, FALSE);
    if (text) {
        fs::create_directories(fs::path(g_state.notes_path).parent_path());
        std::ofstream ofs(g_state.notes_path);
        if (ofs) ofs << text;
        g_free(text);
    }
    update_notes_stats();
}

static void notes_load() {
    if (!g_state.notes_buffer || g_state.notes_path.empty()) return;
    std::ifstream ifs(g_state.notes_path);
    if (ifs) {
        std::ostringstream ss;
        ss << ifs.rdbuf();
        gtk_text_buffer_set_text(g_state.notes_buffer, ss.str().c_str(), -1);
    }
    update_notes_stats();
}

static GtkWidget* build_notes_tab() {
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(root, 4);

    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    g_state.notes_stats_lbl = gtk_label_new("1 LN  ·  0 CH");
    gtk_widget_add_css_class(g_state.notes_stats_lbl, "bento-title");
    gtk_widget_set_hexpand(g_state.notes_stats_lbl, TRUE);
    gtk_widget_set_halign(g_state.notes_stats_lbl, GTK_ALIGN_START);

    GtkWidget* btn_copy = gtk_button_new_with_label("󰅍 Copy All");
    gtk_widget_add_css_class(btn_copy, "action-btn");
    g_signal_connect(btn_copy, "clicked", G_CALLBACK((+[](GtkButton*, gpointer) {
        if (!g_state.notes_buffer) return;
        GtkTextIter s;
        GtkTextIter e;
        gtk_text_buffer_get_bounds(g_state.notes_buffer, &s, &e);
        char* txt = gtk_text_buffer_get_text(g_state.notes_buffer, &s, &e, FALSE);
        if (txt) {
            GdkClipboard* cb = gdk_display_get_clipboard(gdk_display_get_default());
            if (cb) gdk_clipboard_set_text(cb, txt);
            g_free(txt);
        }
    })), nullptr);

    GtkWidget* btn_clr = gtk_button_new_with_label("󰃢 Clear");
    gtk_widget_add_css_class(btn_clr, "action-btn");
    g_signal_connect(btn_clr, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        if (g_state.notes_buffer) gtk_text_buffer_set_text(g_state.notes_buffer, "", -1);
    }), nullptr);

    gtk_box_append(GTK_BOX(top), g_state.notes_stats_lbl);
    gtk_box_append(GTK_BOX(top), btn_copy);
    gtk_box_append(GTK_BOX(top), btn_clr);
    gtk_box_append(GTK_BOX(root), top);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scroll, "notes-card");
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget* tv = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_widget_add_css_class(tv, "notes-view");
    g_state.notes_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));

    g_signal_connect(g_state.notes_buffer, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer) {
        notes_save();
    }), nullptr);

    notes_load();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tv);
    gtk_box_append(GTK_BOX(root), scroll);

    return root;
}

// ──────────────────────────────────────────────────────────────────────────────
// WINDOW & EDGE DRAWER LIFECYCLE
// ──────────────────────────────────────────────────────────────────────────────
static void switch_tab(const std::string& tab_id) {
    g_state.active_tab = tab_id;
    if (g_state.stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(g_state.stack), tab_id.c_str());
    }
    for (auto& kv : g_state.tab_btns) {
        if (kv.first == tab_id) gtk_widget_add_css_class(kv.second, "nav-pill-active");
        else gtk_widget_remove_css_class(kv.second, "nav-pill-active");
    }
}

static void update_pin_ui() {
    if (!g_state.pin_btn) return;
    if (g_state.is_pinned) gtk_widget_add_css_class(g_state.pin_btn, "pinned");
    else gtk_widget_remove_css_class(g_state.pin_btn, "pinned");
}

static void create_popup_window() {
    g_state.popup_win = gtk_window_new();
    gtk_layer_init_for_window(GTK_WINDOW(g_state.popup_win));
    gtk_layer_set_layer(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(g_state.popup_win), -1);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    gtk_layer_set_namespace(GTK_WINDOW(g_state.popup_win), "monolith-toolbox-popup");
    gtk_widget_add_css_class(g_state.popup_win, "toolbox-window");

    g_state.revealer = gtk_revealer_new();
    gtk_revealer_set_transition_duration(GTK_REVEALER(g_state.revealer), 170);

    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "toolbox-card");
    gtk_widget_set_size_request(card, 356, 456);

    // Top Segmented Tab Dock + Pin + Close
    GtkWidget* top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget* nav_dock = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(nav_dock, "nav-dock");
    gtk_widget_set_hexpand(nav_dock, TRUE);

    struct TabDef { const char* id; const char* label; };
    TabDef tabs[] = {
        {"timer", "󰔛 Chrono"},
        {"calc",  "󰪚 Calc"},
        {"draw",  "󰏘 Sketch"},
        {"notes", "󰠮 Notes"}
    };

    for (const auto& t : tabs) {
        GtkWidget* b = gtk_button_new_with_label(t.label);
        gtk_widget_add_css_class(b, "nav-pill");
        if (std::string(t.id) == "timer") gtk_widget_add_css_class(b, "nav-pill-active");
        gtk_widget_set_hexpand(b, TRUE);
        auto* id_s = new std::string(t.id);
        g_signal_connect_data(b, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            switch_tab(*static_cast<std::string*>(data));
        }), id_s, [](gpointer p, GClosure*) { delete static_cast<std::string*>(p); }, (GConnectFlags)0);
        g_state.tab_btns.push_back({t.id, b});
        gtk_box_append(GTK_BOX(nav_dock), b);
    }

    g_state.pin_btn = gtk_button_new_with_label("󰐃");
    gtk_widget_add_css_class(g_state.pin_btn, "window-ctrl-btn");
    gtk_widget_set_tooltip_text(g_state.pin_btn, "Pin drawer on screen");
    g_signal_connect(g_state.pin_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        g_state.is_pinned = !g_state.is_pinned;
        update_pin_ui();
    }), nullptr);

    GtkWidget* close_btn = gtk_button_new_with_label("󰅖");
    gtk_widget_add_css_class(close_btn, "window-ctrl-btn");
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(hide_popup), nullptr);

    gtk_box_append(GTK_BOX(top_bar), nav_dock);
    gtk_box_append(GTK_BOX(top_bar), g_state.pin_btn);
    gtk_box_append(GTK_BOX(top_bar), close_btn);
    gtk_box_append(GTK_BOX(card), top_bar);

    // Content Stack
    g_state.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(g_state.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(g_state.stack), 120);
    gtk_widget_set_vexpand(g_state.stack, TRUE);

    gtk_stack_add_named(GTK_STACK(g_state.stack), build_stopwatch_tab(), "timer");
    gtk_stack_add_named(GTK_STACK(g_state.stack), build_calc_tab(), "calc");
    gtk_stack_add_named(GTK_STACK(g_state.stack), build_draw_tab(), "draw");
    gtk_stack_add_named(GTK_STACK(g_state.stack), build_notes_tab(), "notes");

    gtk_box_append(GTK_BOX(card), g_state.stack);
    gtk_revealer_set_child(GTK_REVEALER(g_state.revealer), card);
    gtk_window_set_child(GTK_WINDOW(g_state.popup_win), g_state.revealer);

    // Smart Auto-Retract when clicking outside (unless pinned)
    g_signal_connect(g_state.popup_win, "notify::is-active", G_CALLBACK(+[](GObject* obj, GParamSpec*, gpointer) {
        GtkWindow* win = GTK_WINDOW(obj);
        if (!gtk_window_is_active(win) && g_state.popup_visible && !g_state.is_pinned && !g_state.in_open_grace) {
            hide_popup();
        }
    }), nullptr);

    // Keyboard Controller (Bubble phase so Notes TextView receives keys naturally!)
    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK((+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer) -> gboolean {
        if (keyval == GDK_KEY_Escape) {
            hide_popup();
            return TRUE;
        }
        if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F4) {
            const char* ids[] = {"timer", "calc", "draw", "notes"};
            switch_tab(ids[keyval - GDK_KEY_F1]);
            return TRUE;
        }

        if (g_state.active_tab == "draw") {
            if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_z || keyval == GDK_KEY_Z)) {
                draw_undo(nullptr, nullptr);
                return TRUE;
            }
        } else if (g_state.active_tab == "calc") {
            if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) { calc_press(std::string(1, '0' + (keyval - GDK_KEY_0))); return TRUE; }
            if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9) { calc_press(std::string(1, '0' + (keyval - GDK_KEY_KP_0))); return TRUE; }
            if (keyval == GDK_KEY_plus || keyval == GDK_KEY_KP_Add) { calc_press("+"); return TRUE; }
            if (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract) { calc_press("-"); return TRUE; }
            if (keyval == GDK_KEY_asterisk || keyval == GDK_KEY_KP_Multiply) { calc_press("*"); return TRUE; }
            if (keyval == GDK_KEY_slash || keyval == GDK_KEY_KP_Divide) { calc_press("/"); return TRUE; }
            if (keyval == GDK_KEY_period || keyval == GDK_KEY_comma || keyval == GDK_KEY_KP_Decimal) { calc_press("."); return TRUE; }
            if (keyval == GDK_KEY_equal || keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) { calc_press("="); return TRUE; }
            if (keyval == GDK_KEY_BackSpace) { calc_press("DEL"); return TRUE; }
        }
        return FALSE;
    })), nullptr);
    gtk_widget_add_controller(g_state.popup_win, key_ctrl);
}

static void show_popup(const std::string& edge) {
    if (!g_state.popup_win) create_popup_window();
    if (g_state.hide_timer > 0) {
        g_source_remove(g_state.hide_timer);
        g_state.hide_timer = 0;
    }

    g_state.current_edge = edge;
    g_state.popup_visible = true;
    g_state.in_open_grace = true;
    if (g_state.open_grace_timer > 0) g_source_remove(g_state.open_grace_timer);
    g_state.open_grace_timer = g_timeout_add(450, +[](gpointer) -> gboolean {
        g_state.open_grace_timer = 0;
        g_state.in_open_grace = false;
        return G_SOURCE_REMOVE;
    }, nullptr);

    gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_TOP, FALSE);
    gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);

    if (edge == "left") {
        gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
        gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, 12);
        gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
        gtk_revealer_set_transition_type(GTK_REVEALER(g_state.revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    } else {
        gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
        gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, 12);
        gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, 0);
        gtk_revealer_set_transition_type(GTK_REVEALER(g_state.revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    }

    gtk_widget_set_visible(g_state.popup_win, TRUE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(g_state.revealer), TRUE);
    gtk_window_present(GTK_WINDOW(g_state.popup_win));
}

static void hide_popup() {
    if (!g_state.popup_win || !g_state.popup_visible) return;
    g_state.popup_visible = false;
    g_state.is_pinned = false;
    update_pin_ui();
    notes_save();

    gtk_revealer_set_reveal_child(GTK_REVEALER(g_state.revealer), FALSE);
    if (g_state.hide_timer > 0) g_source_remove(g_state.hide_timer);
    g_state.hide_timer = g_timeout_add(180, +[](gpointer) -> gboolean {
        g_state.hide_timer = 0;
        if (!g_state.popup_visible && g_state.popup_win) {
            gtk_widget_set_visible(g_state.popup_win, FALSE);
        }
        return G_SOURCE_REMOVE;
    }, nullptr);
}

static void toggle_popup(const std::string& edge) {
    if (g_state.popup_visible) hide_popup();
    else show_popup(edge);
}

// ──────────────────────────────────────────────────────────────────────────────
// FULL-EDGE REACTIVE HANDLE (Hover shows indicator pill • Left-Click opens drawer)
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* create_edge_trigger(const std::string& edge) {
    GtkWidget* win = gtk_window_new();
    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), 0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_namespace(GTK_WINDOW(win), "monolith-toolbox-trigger");

    // Full vertical edge coverage so the indicator pill smoothly tracks the cursor
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);

    if (edge == "left") {
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
    } else {
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
        gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    }

    gtk_widget_add_css_class(win, "edge-trigger-window");

    GtkWidget* da = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da), 3);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da), 100);

    auto& trig_state = (edge == "left") ? g_state.left_trig : g_state.right_trig;
    trig_state.da = da;

    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), +[](GtkDrawingArea*, cairo_t* cr, int, int height, gpointer user_data) {
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);

        auto* ts = static_cast<AppState::TriggerState*>(user_data);
        if (!ts->hovered) return;

        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        double pill_h = 48.0;
        double pill_w = 3.0;
        double pill_x = 0.0;
        double pill_y = std::clamp(ts->cursor_y - pill_h / 2.0, 8.0, height - 8.0 - pill_h);

        double radius = 1.5;
        cairo_new_path(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, pill_x + pill_w - radius, pill_y + radius, radius, -M_PI / 2.0, 0);
        cairo_arc(cr, pill_x + pill_w - radius, pill_y + pill_h - radius, radius, 0, M_PI / 2.0);
        cairo_arc(cr, pill_x + radius, pill_y + pill_h - radius, radius, M_PI / 2.0, M_PI);
        cairo_arc(cr, pill_x + radius, pill_y + radius, radius, M_PI, 3.0 * M_PI / 2.0);
        cairo_close_path(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
        cairo_fill(cr);
    }, &trig_state, nullptr);

    gtk_window_set_child(GTK_WINDOW(win), da);

    // Hover ONLY shows the sleek edge pill; NEVER auto-opens without a click!
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(+[](GtkEventControllerMotion*, double, double y, gpointer user_data) {
        auto* ts = static_cast<AppState::TriggerState*>(user_data);
        ts->hovered = true;
        ts->cursor_y = y;
        gtk_widget_queue_draw(ts->da);
    }), &trig_state);

    g_signal_connect(motion, "motion", G_CALLBACK(+[](GtkEventControllerMotion*, double, double y, gpointer user_data) {
        auto* ts = static_cast<AppState::TriggerState*>(user_data);
        ts->hovered = true;
        ts->cursor_y = y;
        gtk_widget_queue_draw(ts->da);
    }), &trig_state);

    g_signal_connect(motion, "leave", G_CALLBACK(+[](GtkEventControllerMotion*, gpointer user_data) {
        auto* ts = static_cast<AppState::TriggerState*>(user_data);
        ts->hovered = false;
        gtk_widget_queue_draw(ts->da);
    }), &trig_state);
    gtk_widget_add_controller(da, motion);

    // Left-click ONLY (`GDK_BUTTON_PRIMARY`) opens/toggles the drawer
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect_swapped(click, "pressed", G_CALLBACK(+[](const char* e) {
        toggle_popup(e);
    }), (gpointer)(edge == "left" ? "left" : "right"));
    gtk_widget_add_controller(da, GTK_EVENT_CONTROLLER(click));

    return win;
}

// ──────────────────────────────────────────────────────────────────────────────
// ARCHITECTURAL OLED CSS STYLING
// ──────────────────────────────────────────────────────────────────────────────
static void apply_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = R"CSS(
        * {
            font-family: "JetBrains Mono", monospace;
        }

        window.edge-trigger-window, .edge-trigger-window, window.toolbox-window {
            background: transparent;
            background-color: transparent;
            border: none;
            box-shadow: none;
        }

        .toolbox-card {
            background-color: #000000;
            border: 1px solid #27272a;
            border-radius: 12px;
            padding: 12px 14px 14px 14px;
            box-shadow: 0 18px 42px rgba(0, 0, 0, 0.92);
        }

        .nav-dock {
            background-color: #08080b;
            border: 1px solid #18181b;
            border-radius: 9px;
            padding: 3px;
        }

        .nav-pill {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            color: #71717a;
            padding: 5px 6px;
            font-size: 11px;
            font-weight: 700;
            transition: all 100ms ease;
        }
        .nav-pill:hover {
            background-color: #141418;
            color: #e4e4e7;
        }
        .nav-pill-active {
            background-color: #ffffff;
            border-color: #ffffff;
            color: #000000;
        }
        .nav-pill-active:hover {
            background-color: #e4e4e7;
            color: #000000;
        }

        .window-ctrl-btn {
            background-color: #08080b;
            border: 1px solid #18181b;
            border-radius: 8px;
            color: #71717a;
            padding: 5px 8px;
            font-size: 12px;
            transition: all 100ms ease;
        }
        .window-ctrl-btn:hover {
            background-color: #18181b;
            border-color: #27272a;
            color: #ffffff;
        }
        .window-ctrl-btn.pinned {
            background-color: #ffffff;
            border-color: #ffffff;
            color: #000000;
        }

        /* Bento Cards & Typography */
        .bento-card {
            background-color: #07070a;
            border: 1px solid #18181b;
            border-radius: 9px;
            padding: 10px 12px;
        }
        .bento-title {
            font-size: 10px;
            font-weight: 700;
            color: #71717a;
            letter-spacing: 0.6px;
        }
        .bento-value {
            font-size: 11px;
            font-weight: 700;
            color: #f4f4f5;
            font-variant-numeric: tabular-nums;
        }
        .bento-dim {
            font-size: 10px;
            color: #71717a;
            font-variant-numeric: tabular-nums;
        }

        .timer-hero {
            font-size: 38px;
            font-weight: 700;
            color: #ffffff;
            font-variant-numeric: tabular-nums;
            letter-spacing: -1px;
            margin: 2px 0;
        }

        .calc-hero {
            font-size: 26px;
            font-weight: 700;
            color: #ffffff;
            font-variant-numeric: tabular-nums;
            letter-spacing: -0.5px;
        }

        /* Segmented Groups & Buttons */
        .seg-group {
            background-color: #060608;
            border: 1px solid #18181b;
            border-radius: 8px;
            padding: 3px;
        }
        .seg-btn {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            color: #a1a1aa;
            font-size: 10px;
            font-weight: 700;
            padding: 4px 6px;
            transition: all 100ms ease;
        }
        .seg-btn:hover {
            color: #ffffff;
            background-color: #121216;
        }
        .seg-btn-active {
            background-color: #ffffff;
            border-color: #ffffff;
            color: #000000;
        }

        .action-btn {
            background-color: #0c0c10;
            border: 1px solid #222228;
            color: #e4e4e7;
            border-radius: 8px;
            padding: 6px 10px;
            font-size: 11px;
            font-weight: 700;
            transition: all 100ms ease;
        }
        .action-btn:hover {
            background-color: #18181b;
            border-color: #3f3f46;
            color: #ffffff;
        }
        .action-btn-primary {
            background-color: #ffffff;
            border: 1px solid #ffffff;
            color: #000000;
        }
        .action-btn-primary:hover {
            background-color: #e4e4e7;
            border-color: #e4e4e7;
            color: #000000;
        }

        .lap-row {
            padding: 4px 6px;
            border-bottom: 1px solid #111115;
        }

        .hist-chip {
            background-color: #0e0e13;
            border: 1px solid #1e1e24;
            border-radius: 5px;
            color: #a1a1aa;
            font-size: 10px;
            padding: 2px 7px;
        }
        .hist-chip:hover {
            background-color: #18181b;
            color: #ffffff;
        }

        /* Calculator Keypad */
        .key-btn {
            background-color: #08080b;
            border: 1px solid #18181b;
            border-radius: 7px;
            color: #f4f4f5;
            font-size: 13px;
            font-weight: 700;
            transition: all 80ms ease;
        }
        .key-btn:hover {
            background-color: #141419;
            border-color: #27272a;
            color: #ffffff;
        }
        .key-op {
            background-color: #0e0e13;
            border-color: #22222a;
            color: #a1a1aa;
        }
        .key-op:hover {
            background-color: #181820;
            color: #ffffff;
        }
        .key-danger {
            background-color: #140708;
            border-color: #331114;
            color: #f87171;
        }
        .key-danger:hover {
            background-color: #ef4444;
            border-color: #ef4444;
            color: #000000;
        }

        /* Sketch Canvas & Swatches */
        .canvas-card {
            background-color: #050507;
            border: 1px solid #18181b;
            border-radius: 9px;
        }
        .swatch-btn {
            border-radius: 11px;
            border: 2px solid transparent;
            padding: 0;
            min-width: 22px;
            min-height: 22px;
        }
        .swatch-white   { background-color: #f4f4f5; }
        .swatch-cyan    { background-color: #38bdf8; }
        .swatch-emerald { background-color: #4ade80; }
        .swatch-amber   { background-color: #f59e0b; }
        .swatch-red     { background-color: #ef4444; }
        .swatch-eraser  { background-color: #09090b; border: 1px solid #3f3f46; }
        .swatch-active  {
            box-shadow: 0 0 0 2px #000000, 0 0 0 3px #ffffff;
        }

        /* Notes */
        .notes-card {
            background-color: #07070a;
            border: 1px solid #18181b;
            border-radius: 9px;
        }
        .notes-view, .notes-view text {
            background-color: transparent;
            color: #f4f4f5;
            font-size: 12px;
        }

        progressbar trough {
            background-color: #141418;
            border-radius: 3px;
            min-height: 4px;
        }
        progressbar progress {
            background-color: #ffffff;
            border-radius: 3px;
        }
    )CSS";
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

static void on_activate(GApplication* app, gpointer) {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    g_application_hold(app);
    apply_css();

    g_state.notes_path = std::string(getenv("HOME") ? getenv("HOME") : "") + "/.config/monolith/scratchpad.txt";

    g_state.left_trigger = create_edge_trigger("left");
    g_state.right_trigger = create_edge_trigger("right");

    gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(g_state.left_trigger));
    gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(g_state.right_trigger));

    gtk_widget_set_visible(g_state.left_trigger, TRUE);
    gtk_widget_set_visible(g_state.right_trigger, TRUE);

    create_popup_window();
    gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(g_state.popup_win));
}

int main(int argc, char* argv[]) {
    g_state.app = gtk_application_new("org.monolith.toolbox", G_APPLICATION_HANDLES_COMMAND_LINE);

    g_signal_connect(g_state.app, "activate", G_CALLBACK(on_activate), nullptr);
    g_signal_connect(g_state.app, "command-line", G_CALLBACK(+[](GApplication* app, GApplicationCommandLine* cmd, gpointer) -> int {
        on_activate(app, nullptr);
        int c = 0;
        char** v = g_application_command_line_get_arguments(cmd, &c);
        if (c > 1 && std::string(v[1]) == "--toggle") {
            toggle_popup("left");
        }
        g_application_command_line_set_exit_status(cmd, 0);
        return 0;
    }), nullptr);

    int status = g_application_run(G_APPLICATION(g_state.app), argc, argv);
    g_object_unref(g_state.app);
    return status;
}
