#include <adwaita.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>
#include <algorithm>
#include <thread>
#include <atomic>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <filesystem>

#include "settings.hpp"
#include "bezier_widget.hpp"
#include "grain_generator.hpp"
#include "version.hpp"

using namespace Monolith;

static bool g_show_welcome = false;

// Global application context
struct AppContext {
    Settings s;
    BezierData bezier_data;
    GtkWidget* bezier_widget = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* stack = nullptr;
    GtkWidget* sidebar_list = nullptr;
    GtkWidget* toast_overlay = nullptr;
    GtkWidget* search_entry = nullptr;

    // Scale controls references for programmatic updates on preset selection
    GtkAdjustment* adj_p1x = nullptr;
    GtkAdjustment* adj_p1y = nullptr;
    GtkAdjustment* adj_p2x = nullptr;
    GtkAdjustment* adj_p2y = nullptr;
    GtkAdjustment* adj_win_in = nullptr;
    GtkAdjustment* adj_win_out = nullptr;
    GtkAdjustment* adj_win_move = nullptr;
    GtkAdjustment* adj_workspaces = nullptr;
    GtkAdjustment* adj_fade = nullptr;

    // Grain controls
    GtkWidget* grain_preview_da = nullptr;
    cairo_surface_t* grain_preview_surf = nullptr;
    GtkAdjustment* adj_grain_intensity = nullptr;
    GtkAdjustment* adj_grain_scale = nullptr;
    struct ColorControl* ctrl_grain_c1 = nullptr;
    struct ColorControl* ctrl_grain_c2 = nullptr;
    struct ColorControl* ctrl_grain_c3 = nullptr;
    struct ColorControl* ctrl_grain_tint = nullptr;

    // Updates UI state & widgets
    GtkWidget* updates_status_row = nullptr;
    GtkWidget* updates_status_badge = nullptr;
    GtkWidget* updates_check_btn = nullptr;
    GtkWidget* updates_upgrade_btn = nullptr;
    GtkWidget* updates_progress_bar = nullptr;
    GtkWidget* updates_changelog_view = nullptr;
    GtkTextBuffer* updates_changelog_buffer = nullptr;
    GtkWidget* updates_log_view = nullptr;
    GtkTextBuffer* updates_log_buffer = nullptr;
    GtkWidget* updates_revealer = nullptr;
    GtkWidget* updates_revealer_btn = nullptr;
    std::atomic<bool> updates_in_progress{false};
    std::atomic<bool> check_in_progress{false};
};

static AppContext g_app;

static void on_apply_clicked(GtkButton*, gpointer);

// Helper to show toasts
static void show_toast(const std::string& message) {
    if (!g_app.toast_overlay) return;
    AdwToast* toast = adw_toast_new(message.c_str());
    adw_toast_set_timeout(toast, 2);
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(g_app.toast_overlay), toast);
}

// ──────────────────────────────────────────────────────────────────────────────
// HELPER FACTORIES FOR UI ROWS
// ──────────────────────────────────────────────────────────────────────────────
static AdwActionRow* make_slider_row(
    AdwPreferencesGroup* group,
    const std::string& title,
    const std::string& subtitle,
    double min_v, double max_v, double step,
    double initial,
    GtkAdjustment** out_adj,
    std::function<void(double)> on_change,
    std::function<std::string(double)> format_fn = nullptr
) {
    AdwActionRow* row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
    if (!subtitle.empty()) {
        adw_action_row_set_subtitle(row, subtitle.c_str());
    }

    GtkAdjustment* adj = gtk_adjustment_new(initial, min_v, max_v, step, step * 2, 0);
    if (out_adj) *out_adj = adj;

    GtkWidget* scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_size_request(scale, 180, -1);
    gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);

    auto default_fmt = [step](double v) {
        std::ostringstream ss;
        if (step >= 1.0) ss << std::fixed << std::setprecision(0) << v;
        else if (step >= 0.1) ss << std::fixed << std::setprecision(1) << v;
        else ss << std::fixed << std::setprecision(2) << v;
        return ss.str();
    };

    auto fmt = format_fn ? format_fn : default_fmt;
    GtkWidget* val_lbl = gtk_label_new(fmt(initial).c_str());
    gtk_widget_set_size_request(val_lbl, 55, -1);
    gtk_widget_set_valign(val_lbl, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(val_lbl, "dim-label");

    struct CallbackCtx {
        std::function<void(double)> cb;
        std::function<std::string(double)> fmt;
        GtkWidget* lbl;
    };
    auto* ctx = new CallbackCtx{on_change, fmt, val_lbl};

    g_signal_connect_data(adj, "value-changed", G_CALLBACK(+[](GtkAdjustment* a, gpointer p) {
        auto* c = static_cast<CallbackCtx*>(p);
        double v = gtk_adjustment_get_value(a);
        gtk_label_set_text(GTK_LABEL(c->lbl), c->fmt(v).c_str());
        c->cb(v);
    }), ctx, [](gpointer p, GClosure*) { delete static_cast<CallbackCtx*>(p); }, (GConnectFlags)0);

    adw_action_row_add_suffix(row, scale);
    adw_action_row_add_suffix(row, val_lbl);
    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

static AdwSwitchRow* make_switch_row(
    AdwPreferencesGroup* group,
    const std::string& title,
    const std::string& subtitle,
    bool initial,
    std::function<void(bool)> on_change
) {
    AdwSwitchRow* row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
    if (!subtitle.empty()) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle.c_str());
    }
    adw_switch_row_set_active(row, initial);

    auto* cb_holder = new std::function<void(bool)>(on_change);
    g_signal_connect_data(row, "notify::active", G_CALLBACK(+[](GObject* obj, GParamSpec*, gpointer p) {
        auto* cb = static_cast<std::function<void(bool)>*>(p);
        (*cb)(adw_switch_row_get_active(ADW_SWITCH_ROW(obj)));
    }), cb_holder, [](gpointer p, GClosure*) { delete static_cast<std::function<void(bool)>*>(p); }, (GConnectFlags)0);

    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

static AdwComboRow* make_combo_row(
    AdwPreferencesGroup* group,
    const std::string& title,
    const std::string& subtitle,
    const std::vector<std::string>& items,
    int initial_idx,
    std::function<void(int, const std::string&)> on_change
) {
    AdwComboRow* row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
    if (!subtitle.empty()) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle.c_str());
    }

    GtkStringList* sl = gtk_string_list_new(nullptr);
    for (const auto& it : items) {
        gtk_string_list_append(sl, it.c_str());
    }
    adw_combo_row_set_model(row, G_LIST_MODEL(sl));
    adw_combo_row_set_selected(row, initial_idx);

    struct ComboCtx {
        std::function<void(int, const std::string&)> cb;
        std::vector<std::string> items;
    };
    auto* ctx = new ComboCtx{on_change, items};

    g_signal_connect_data(row, "notify::selected", G_CALLBACK(+[](GObject* obj, GParamSpec*, gpointer p) {
        auto* c = static_cast<ComboCtx*>(p);
        guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(obj));
        if (sel < c->items.size()) {
            c->cb(static_cast<int>(sel), c->items[sel]);
        }
    }), ctx, [](gpointer p, GClosure*) { delete static_cast<ComboCtx*>(p); }, (GConnectFlags)0);

    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

[[maybe_unused]] static AdwEntryRow* make_entry_row(
    AdwPreferencesGroup* group,
    const std::string& title,
    const std::string& initial_val,
    std::function<void(const std::string&)> on_change
) {
    AdwEntryRow* row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
    gtk_editable_set_text(GTK_EDITABLE(row), initial_val.c_str());

    auto* cb = new std::function<void(const std::string&)>(on_change);
    g_signal_connect_data(row, "changed", G_CALLBACK(+[](GtkEditable* e, gpointer p) {
        auto* fn = static_cast<std::function<void(const std::string&)>*>(p);
        (*fn)(gtk_editable_get_text(e));
    }), cb, [](gpointer p, GClosure*) { delete static_cast<std::function<void(const std::string&)>*>(p); }, (GConnectFlags)0);

    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

struct ColorControl {
    GtkWidget* swatch_btn = nullptr;
    GtkWidget* swatch_da = nullptr;
    GtkWidget* popover = nullptr;
    GtkWidget* entry = nullptr;
    GtkAdjustment* adj_r = nullptr;
    GtkAdjustment* adj_g = nullptr;
    GtkAdjustment* adj_b = nullptr;
    std::string current_hex = "#000000";
    std::function<void(const std::string&)> on_change;
    bool updating = false;
};

static void set_color_control_value(ColorControl* ctrl, const std::string& hex) {
    if (!ctrl || ctrl->updating) return;
    ctrl->updating = true;

    std::string clean = hex;
    while (!clean.empty() && (clean.front() == '#' || clean.front() == ' ')) {
        clean.erase(clean.begin());
    }
    if (clean.length() == 3) {
        clean = std::string(2, clean[0]) + std::string(2, clean[1]) + std::string(2, clean[2]);
    }
    if (clean.length() != 6) {
        clean = "000000";
    }
    std::string full_hex = "#" + clean;
    ctrl->current_hex = full_hex;

    int r = 0, g = 0, b = 0;
    try {
        r = std::clamp((int)std::strtol(clean.substr(0, 2).c_str(), nullptr, 16), 0, 255);
        g = std::clamp((int)std::strtol(clean.substr(2, 2).c_str(), nullptr, 16), 0, 255);
        b = std::clamp((int)std::strtol(clean.substr(4, 2).c_str(), nullptr, 16), 0, 255);
    } catch (...) {}

    if (ctrl->adj_r) gtk_adjustment_set_value(ctrl->adj_r, r);
    if (ctrl->adj_g) gtk_adjustment_set_value(ctrl->adj_g, g);
    if (ctrl->adj_b) gtk_adjustment_set_value(ctrl->adj_b, b);

    if (ctrl->entry) {
        gtk_editable_set_text(GTK_EDITABLE(ctrl->entry), clean.c_str());
    }
    if (ctrl->swatch_da) {
        gtk_widget_queue_draw(ctrl->swatch_da);
    }
    if (ctrl->on_change) {
        ctrl->on_change(full_hex);
    }
    ctrl->updating = false;
}

static void draw_swatch_func(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer user_data) {
    auto* c = static_cast<ColorControl*>(user_data);
    GdkRGBA rgba;
    if (!gdk_rgba_parse(&rgba, c->current_hex.c_str())) {
        rgba = {0.0, 0.0, 0.0, 1.0};
    }
    double r = 4.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, width - r, r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, width - r, height - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, r, height - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, r, r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);

    cairo_set_source_rgb(cr, rgba.red, rgba.green, rgba.blue);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.22);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

static ColorControl* make_color_row(
    AdwPreferencesGroup* group,
    const std::string& title,
    const std::string& subtitle,
    const std::string& initial_hex,
    std::function<void(const std::string&)> on_change
) {
    AdwActionRow* row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
    if (!subtitle.empty()) {
        adw_action_row_set_subtitle(row, subtitle.c_str());
    }

    GtkWidget* suffix_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_valign(suffix_box, GTK_ALIGN_CENTER);

    auto* ctrl = new ColorControl();
    ctrl->on_change = on_change;
    ctrl->current_hex = initial_hex.empty() ? "#000000" : initial_hex;

    // Swatch Menu Button (Natively hosts the popover inside the window surface)
    ctrl->swatch_btn = gtk_menu_button_new();
    gtk_widget_add_css_class(ctrl->swatch_btn, "swatch-trigger");
    gtk_widget_set_valign(ctrl->swatch_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(ctrl->swatch_btn, "Color palette / RGB");

    ctrl->swatch_da = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(ctrl->swatch_da), 30);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(ctrl->swatch_da), 20);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ctrl->swatch_da), draw_swatch_func, ctrl, nullptr);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(ctrl->swatch_btn), ctrl->swatch_da);

    // Integrated In-Window Popover (No external dialog window!)
    ctrl->popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(ctrl->popover), TRUE);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(ctrl->swatch_btn), ctrl->popover);

    GtkWidget* pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(pop_box, 12);
    gtk_widget_set_margin_end(pop_box, 12);
    gtk_widget_set_margin_top(pop_box, 12);
    gtk_widget_set_margin_bottom(pop_box, 12);

    GtkWidget* pal_title = gtk_label_new("COLOR PALETTE");
    gtk_widget_add_css_class(pal_title, "popover-heading");
    gtk_widget_set_halign(pal_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(pop_box), pal_title);

    // Curated Swatches Grid (16 tones)
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);

    static const std::vector<std::string> palette = {
        "#000000", "#09090b", "#18181b", "#27272a", "#3f3f46", "#71717a", "#a1a1aa", "#ffffff",
        "#ef4444", "#f97316", "#eab308", "#22c55e", "#06b6d4", "#3b82f6", "#6366f1", "#ec4899"
    };

    struct PaletteClickCtx {
        ColorControl* ctrl;
        std::string hex;
    };

    for (size_t i = 0; i < palette.size(); ++i) {
        std::string col = palette[i];
        GtkWidget* pbtn = gtk_button_new();
        gtk_widget_set_size_request(pbtn, 24, 24);

        GtkWidget* pda = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(pda), 22);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(pda), 22);
        auto* p_col = new std::string(col);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(pda), +[](GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer data) {
            auto* s = static_cast<std::string*>(data);
            GdkRGBA c;
            if (!gdk_rgba_parse(&c, s->c_str())) c = {0,0,0,1};
            cairo_rectangle(cr, 1, 1, w - 2, h - 2);
            cairo_set_source_rgb(cr, c.red, c.green, c.blue);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
        }, p_col, [](gpointer d) { delete static_cast<std::string*>(d); });
        gtk_button_set_child(GTK_BUTTON(pbtn), pda);

        auto* pctx = new PaletteClickCtx{ctrl, col};
        g_signal_connect_data(pbtn, "clicked", G_CALLBACK((+[](GtkButton*, gpointer p) {
            auto* d = static_cast<PaletteClickCtx*>(p);
            set_color_control_value(d->ctrl, d->hex);
            gtk_popover_popdown(GTK_POPOVER(d->ctrl->popover));
        })), pctx, +[](gpointer p, GClosure*) {
            delete static_cast<PaletteClickCtx*>(p);
        }, (GConnectFlags)0);

        int row_idx = (int)(i / 8);
        int col_idx = (int)(i % 8);
        gtk_grid_attach(GTK_GRID(grid), pbtn, col_idx, row_idx, 1, 1);
    }
    gtk_box_append(GTK_BOX(pop_box), grid);

    // RGB Fine Tuning
    GtkWidget* rgb_title = gtk_label_new("RGB CHANNELS");
    gtk_widget_add_css_class(rgb_title, "popover-heading");
    gtk_widget_set_margin_top(rgb_title, 4);
    gtk_widget_set_halign(rgb_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(pop_box), rgb_title);

    auto make_rgb_slider = [&](const char* ch_label, GtkAdjustment** out_adj) {
        GtkWidget* r_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* l = gtk_label_new(ch_label);
        gtk_widget_set_size_request(l, 14, -1);
        gtk_widget_add_css_class(l, "dim-label");

        GtkAdjustment* adj = gtk_adjustment_new(0, 0, 255, 1, 10, 0);
        *out_adj = adj;
        GtkWidget* scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
        gtk_scale_set_digits(GTK_SCALE(scale), 0);
        gtk_widget_set_hexpand(scale, TRUE);
        gtk_widget_set_size_request(scale, 160, -1);

        g_signal_connect(adj, "value-changed", G_CALLBACK(+[](GtkAdjustment*, gpointer p) {
            auto* c = static_cast<ColorControl*>(p);
            if (c->updating) return;
            int r = std::clamp((int)gtk_adjustment_get_value(c->adj_r), 0, 255);
            int g = std::clamp((int)gtk_adjustment_get_value(c->adj_g), 0, 255);
            int b = std::clamp((int)gtk_adjustment_get_value(c->adj_b), 0, 255);
            char buf[16];
            snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
            set_color_control_value(c, buf);
        }), ctrl);

        gtk_box_append(GTK_BOX(r_box), l);
        gtk_box_append(GTK_BOX(r_box), scale);
        gtk_box_append(GTK_BOX(pop_box), r_box);
    };

    make_rgb_slider("R", &ctrl->adj_r);
    make_rgb_slider("G", &ctrl->adj_g);
    make_rgb_slider("B", &ctrl->adj_b);

    gtk_popover_set_child(GTK_POPOVER(ctrl->popover), pop_box);

    // Custom Hex Entry with '#' label
    GtkWidget* hash_lbl = gtk_label_new("#");
    gtk_widget_add_css_class(hash_lbl, "dim-label");
    gtk_widget_set_margin_start(hash_lbl, 6);
    gtk_widget_set_margin_end(hash_lbl, 2);

    ctrl->entry = gtk_entry_new();
    gtk_widget_set_size_request(ctrl->entry, 86, -1);
    gtk_entry_set_max_length(GTK_ENTRY(ctrl->entry), 8);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ctrl->entry), "000000");
    gtk_widget_set_tooltip_text(ctrl->entry, "Direct hex input (e.g. 18181b)");

    gtk_box_append(GTK_BOX(suffix_box), ctrl->swatch_btn);
    gtk_box_append(GTK_BOX(suffix_box), hash_lbl);
    gtk_box_append(GTK_BOX(suffix_box), ctrl->entry);

    adw_action_row_add_suffix(row, suffix_box);
    adw_preferences_group_add(group, GTK_WIDGET(row));

    set_color_control_value(ctrl, initial_hex);

    // Sync from Custom Entry
    g_signal_connect(ctrl->entry, "changed", G_CALLBACK((+[](GtkEditable* e, gpointer p) {
        auto* c = static_cast<ColorControl*>(p);
        if (c->updating) return;
        std::string text = gtk_editable_get_text(e);
        while (!text.empty() && (text.front() == '#' || text.front() == ' ')) text.erase(text.begin());
        if (text.length() == 6 || text.length() == 3) {
            set_color_control_value(c, text);
        }
    })), ctrl);

    return ctrl;
}

// ──────────────────────────────────────────────────────────────────────────────
// DEFAULT OVERVIEW & HARDWARE SPECIFICATIONS
// ──────────────────────────────────────────────────────────────────────────────
static std::string get_os_pretty_name() {
    std::ifstream f("/etc/os-release");
    if (!f.is_open()) return "Linux System";
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string name = line.substr(12);
            if (!name.empty() && name.front() == '"') name.erase(0, 1);
            if (!name.empty() && name.back() == '"') name.pop_back();
            return name;
        }
    }
    return "Linux";
}

static std::string get_kernel_info() {
    struct utsname u;
    if (uname(&u) == 0) {
        return std::string(u.sysname) + " " + u.release + " (" + u.machine + ")";
    }
    return "Linux Kernel";
}

static std::string get_cpu_info() {
    std::string model = "Processor";
    std::ifstream f("/proc/cpuinfo");
    if (f.is_open()) {
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("model name") != std::string::npos) {
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    model = line.substr(colon + 2);
                    break;
                }
            }
        }
    }
    unsigned int threads = std::thread::hardware_concurrency();
    if (threads > 0) {
        return model + " (" + std::to_string(threads) + " Threads)";
    }
    return model;
}

static std::string get_ram_info() {
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        double total_gb = (double)si.totalram * si.mem_unit / (1024.0 * 1024.0 * 1024.0);
        double free_gb = (double)si.freeram * si.mem_unit / (1024.0 * 1024.0 * 1024.0);
        double used_gb = total_gb - free_gb;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << total_gb << " GB Total (" << used_gb << " GB Active)";
        return ss.str();
    }
    return "16 GB Total";
}

static std::string get_active_monitors_summary() {
    std::string json_raw = run_cmd("hyprctl monitors -j 2>/dev/null");
    if (json_raw.empty()) return "Internal Display";
    try {
        auto j = nlohmann::json::parse(json_raw);
        if (j.is_array() && !j.empty()) {
            std::string summary = "";
            for (size_t i = 0; i < j.size(); ++i) {
                if (i > 0) summary += " | ";
                std::string name = j[i].value("name", "Display");
                int w = j[i].value("width", 0);
                int h = j[i].value("height", 0);
                double hz = j[i].value("refreshRate", 60.0);
                double scale = j[i].value("scale", 1.0);
                std::ostringstream ss;
                ss << name << " (" << w << "x" << h << "@" << std::fixed << std::setprecision(0) << hz << "Hz, " << std::setprecision(2) << scale << "x)";
                summary += ss.str();
            }
            return summary;
        }
    } catch (...) {}
    return "eDP-1 (1920x1200)";
}

static GtkWidget* build_overview_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    // Hero Banner Card
    GtkWidget* hero_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(hero_card, "hero-card");
    gtk_widget_set_margin_bottom(hero_card, 6);

    GtkWidget* title = gtk_label_new("MONOLITH HYPRLAND");
    gtk_widget_add_css_class(title, "title-1");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    std::string ver_text = "Release v" + std::string(MONOLITH_VERSION) + " • High-Performance C++20 Environment";
    GtkWidget* sub = gtk_label_new(ver_text.c_str());
    gtk_widget_add_css_class(sub, "dim-label");
    gtk_widget_set_halign(sub, GTK_ALIGN_START);

    gtk_box_append(GTK_BOX(hero_card), title);
    gtk_box_append(GTK_BOX(hero_card), sub);
    gtk_box_append(GTK_BOX(box), hero_card);

    // Group 1: Hardware & System Specifications
    AdwPreferencesGroup* g_specs = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_specs, "System & Hardware Specifications");
    adw_preferences_group_set_description(g_specs, "Detected platform and hardware parameters.");

    auto add_spec_row = [&](const std::string& label, const std::string& value) {
        AdwActionRow* row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), label.c_str());
        adw_action_row_set_subtitle(row, value.c_str());
        adw_preferences_group_add(g_specs, GTK_WIDGET(row));
    };

    add_spec_row("Operating System", get_os_pretty_name());
    add_spec_row("Kernel & Architecture", get_kernel_info());
    add_spec_row("Processor (CPU)", get_cpu_info());
    add_spec_row("System Memory (RAM)", get_ram_info());
    add_spec_row("Window Compositor", "Hyprland (Direct Wayland Socket2 IPC)");
    add_spec_row("Active Displays", get_active_monitors_summary());

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_specs));

    // Group 2: Active Monolith Daemons Status
    AdwPreferencesGroup* g_daemons = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_daemons, "Active Monolith Services");
    adw_preferences_group_set_description(g_daemons, "Real-time state of background systemd modules.");

    auto add_daemon_row = [&](const std::string& title_str, const std::string& desc, const std::string& unit) {
        AdwActionRow* r = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title_str.c_str());
        adw_action_row_set_subtitle(r, desc.c_str());

        bool active = (system(("systemctl --user is-active --quiet " + unit).c_str()) == 0);
        GtkWidget* badge = gtk_label_new(active ? "● Running" : "○ Inactive");
        gtk_widget_add_css_class(badge, active ? "badge-uptodate" : "dim-label");
        gtk_widget_set_valign(badge, GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(r, badge);

        adw_preferences_group_add(g_daemons, GTK_WIDGET(r));
    };

    add_daemon_row("Top Status Bar", "Waybar (Pinnable OLED bar with hardware modules)", "monolith-waybar.service");
    add_daemon_row("Desktop Constellation", "Task Graph (Zero-overhead 0.00% background process graph)", "monolith-taskgraph.service");
    add_daemon_row("Edge Toolbox Drawer", "monolith-toolbox (Canvas, Stopwatch, Calc, Scratchpad)", "monolith-toolbox.service");
    add_daemon_row("Native Micro Popups", "monolith-popup (Hardware sliders, Wi-Fi, Bluetooth)", "monolith-popup.service");
    add_daemon_row("Kernel RAM Preload", "monolith-ram-preload (Warm kernel pagecache residency)", "monolith-ram-preload.service");

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_daemons));

    // Group 3: Quick Navigation & Actions
    AdwPreferencesGroup* g_actions = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_actions, "Quick Navigation & Actions");

    GtkWidget* act_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(act_box, 6);
    gtk_widget_set_margin_bottom(act_box, 6);

    auto make_nav_btn = [&](const char* label, const char* target_stack, int sidebar_idx) {
        GtkWidget* btn = gtk_button_new_with_label(label);
        gtk_widget_set_hexpand(btn, TRUE);
        g_object_set_data_full(G_OBJECT(btn), "target", g_strdup(target_stack), g_free);
        g_object_set_data(G_OBJECT(btn), "idx", GINT_TO_POINTER(sidebar_idx));
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
            const char* tgt = (const char*)g_object_get_data(G_OBJECT(b), "target");
            int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "idx"));
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), tgt);
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), idx));
        }), nullptr);
        gtk_box_append(GTK_BOX(act_box), btn);
    };

    make_nav_btn("Appearance & Colors", "appearance", 1);
    make_nav_btn("Wallpapers & Grain", "grain", 2);
    make_nav_btn("Keybindings", "shortcuts", 8);
    make_nav_btn("Check Updates", "updates", 9);

    adw_preferences_group_add(g_actions, act_box);

    // Reload Hyprland action row
    AdwActionRow* reload_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(reload_row), "Reload Shell Configuration");
    adw_action_row_set_subtitle(reload_row, "Hot-reload Hyprland keywords and configs via hyprctl reload");

    GtkWidget* btn_reload = gtk_button_new_with_label("Reload Shell");
    gtk_widget_set_valign(btn_reload, GTK_ALIGN_CENTER);
    g_signal_connect(btn_reload, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        system("hyprctl reload config-only >/dev/null 2>&1");
        show_toast("Shell configuration reloaded successfully");
    }), nullptr);
    adw_action_row_add_suffix(reload_row, btn_reload);
    adw_preferences_group_add(g_actions, GTK_WIDGET(reload_row));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_actions));

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 1: APPEARANCE & COLORS
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_appearance_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    // Group 1: Window Borders
    AdwPreferencesGroup* g_borders = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_borders, "Window Borders and Geometry");
    adw_preferences_group_set_description(g_borders, "Crisp geometry, dual-color gradient borders, and gaps.");

    make_color_row(g_borders, "Active Border Color 1", "Primary gradient accent for focused windows", g_app.s.geom.active_border_1, [](const std::string& s) {
        g_app.s.geom.active_border_1 = s;
    });
    make_color_row(g_borders, "Active Border Color 2", "Secondary gradient accent for focused windows", g_app.s.geom.active_border_2, [](const std::string& s) {
        g_app.s.geom.active_border_2 = s;
    });
    make_slider_row(g_borders, "Border Gradient Angle", "Rotation angle of the dual-color gradient", 0.0, 360.0, 15.0, g_app.s.geom.border_angle, nullptr, [](double v) {
        g_app.s.geom.border_angle = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + "°"; });

    make_color_row(g_borders, "Inactive Border Color", "Border perimeter for unfocused background windows", g_app.s.geom.inactive_border, [](const std::string& s) {
        g_app.s.geom.inactive_border = s;
    });

    make_slider_row(g_borders, "Border Width", "Thickness of window perimeter in pixels", 0.0, 6.0, 1.0, g_app.s.geom.border_size, nullptr, [](double v) {
        g_app.s.geom.border_size = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " px"; });

    make_slider_row(g_borders, "Corner Rounding", "Corner radius for tiled and floating windows", 0.0, 24.0, 1.0, g_app.s.geom.rounding, nullptr, [](double v) {
        g_app.s.geom.rounding = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " px"; });

    make_slider_row(g_borders, "Inner Gaps (Gaps In)", "Spacing between adjacent tiled windows", 0.0, 30.0, 1.0, g_app.s.geom.gaps_in, nullptr, [](double v) {
        g_app.s.geom.gaps_in = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " px"; });

    make_slider_row(g_borders, "Outer Gaps (Gaps Out)", "Spacing between windows and screen edges", 0.0, 40.0, 1.0, g_app.s.geom.gaps_out, nullptr, [](double v) {
        g_app.s.geom.gaps_out = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " px"; });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_borders));

    // Group 2: Window Opacity & Shaders
    AdwPreferencesGroup* g_shd = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_shd, "Opacity and Compositing");
    adw_preferences_group_set_description(g_shd, "Pure 1.0 opacity recommended for razor-sharp OLED contrast.");

    make_slider_row(g_shd, "Active Window Opacity", "Transparency of focused window", 0.50, 1.00, 0.05, g_app.s.opac.active_opacity, nullptr, [](double v) {
        g_app.s.opac.active_opacity = v;
    });
    make_slider_row(g_shd, "Inactive Window Opacity", "Transparency of unfocused windows", 0.50, 1.00, 0.05, g_app.s.opac.inactive_opacity, nullptr, [](double v) {
        g_app.s.opac.inactive_opacity = v;
    });
    make_switch_row(g_shd, "GPU Background Blur", "Dual Kawase blur filter (Disabled by default for zero latency)", g_app.s.opac.blur_enabled, [](bool b) {
        g_app.s.opac.blur_enabled = b;
    });
    make_switch_row(g_shd, "Window Drop Shadows", "Shadow rendering beneath windows (Disabled for minimal GPU overhead)", g_app.s.opac.shadow_enabled, [](bool b) {
        g_app.s.opac.shadow_enabled = b;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_shd));

    // Group 3: Status Bar Color Customization
    AdwPreferencesGroup* g_wb_col = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_wb_col, "Status Bar Color Scheme");
    adw_preferences_group_set_description(g_wb_col, "Individual color overrides for the top bar and widgets.");

    make_color_row(g_wb_col, "Bar Background Color", "Surface color of the top bar container", g_app.s.waybar.bg_color, [](const std::string& s) {
        g_app.s.waybar.bg_color = s;
    });
    make_color_row(g_wb_col, "Bar Border Color", "Bottom separator line color", g_app.s.waybar.border_color, [](const std::string& s) {
        g_app.s.waybar.border_color = s;
    });
    make_color_row(g_wb_col, "Bar Text and Icon Color", "Default foreground tint for clocks and labels", g_app.s.waybar.text_color, [](const std::string& s) {
        g_app.s.waybar.text_color = s;
    });
    make_color_row(g_wb_col, "Active Workspace Accent", "Pill highlight for the currently focused workspace", g_app.s.waybar.active_ws_bg, [](const std::string& s) {
        g_app.s.waybar.active_ws_bg = s;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_wb_col));

    // Group 4: Smart Tiling & Focus Clarity
    AdwPreferencesGroup* g_smart = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_smart, "Smart Tiling and Focus Clarity");
    adw_preferences_group_set_description(g_smart, "Dynamic gap reduction and unfocused window dimming for distraction-free workflows.");

    make_switch_row(g_smart, "Smart Gaps (No Gaps When Only)", "Automatically collapses gaps and borders when only one window is tiled, maximizing screen area", g_app.s.geom.smart_gaps, [](bool b) {
        g_app.s.geom.smart_gaps = b;
    });

    make_switch_row(g_smart, "Dim Inactive Windows", "Shades unfocused windows to bring immediate cognitive clarity to the active window", g_app.s.opac.dim_inactive, [](bool b) {
        g_app.s.opac.dim_inactive = b;
    });

    make_slider_row(g_smart, "Dim Inactive Intensity", "Darkening intensity for unfocused windows", 0.05, 0.50, 0.05, g_app.s.opac.dim_strength, nullptr, [](double v) {
        g_app.s.opac.dim_strength = v;
    }, [](double v) { return std::to_string(static_cast<int>(v * 100)) + "%"; });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_smart));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 2: GRADIENT GRAIN BACKGROUND (With Live Cairo Preview Canvas)
// ──────────────────────────────────────────────────────────────────────────────
static void refresh_grain_preview() {
    if (!g_app.grain_preview_da) return;
    if (!g_app.grain_preview_surf) {
        g_app.grain_preview_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 420, 170);
    }
    render_procedural_grain_to_surface(
        g_app.grain_preview_surf,
        g_app.s.grain.algorithm,
        g_app.s.grain.intensity,
        g_app.s.grain.scale,
        g_app.s.grain.gradient_type,
        g_app.s.grain.color1,
        g_app.s.grain.color2,
        g_app.s.grain.color3,
        g_app.s.grain.tint,
        g_app.s.grain.crosshair
    );
    gtk_widget_queue_draw(g_app.grain_preview_da);
}

static GtkWidget* build_grain_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_grad = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_grad, "Gradient and Film Grain Synthesis");
    adw_preferences_group_set_description(g_grad, "Seamless procedural gradients with tactile film grain woven directly into the field.");

    // Live Procedural Preview Canvas (420x170)
    g_app.grain_preview_da = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(g_app.grain_preview_da), 420);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(g_app.grain_preview_da), 170);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_app.grain_preview_da), +[](GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer) {
        if (!g_app.grain_preview_surf) refresh_grain_preview();
        double r = 8.0;
        cairo_new_sub_path(cr);
        cairo_arc(cr, w - r, r, r, -M_PI/2, 0);
        cairo_arc(cr, w - r, h - r, r, 0, M_PI/2);
        cairo_arc(cr, r, h - r, r, M_PI/2, M_PI);
        cairo_arc(cr, r, r, r, M_PI, 3*M_PI/2);
        cairo_close_path(cr);
        cairo_save(cr);
        cairo_clip_preserve(cr);
        if (g_app.grain_preview_surf) {
            cairo_set_source_surface(cr, g_app.grain_preview_surf, 0, 0);
            cairo_paint(cr);
        }
        cairo_restore(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }, nullptr, nullptr);

    GtkWidget* prev_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(prev_center, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(prev_center, 4);
    gtk_widget_set_margin_bottom(prev_center, 10);
    gtk_box_append(GTK_BOX(prev_center), g_app.grain_preview_da);
    adw_preferences_group_add(g_grad, prev_center);

    // Gradient Presets
    std::vector<std::string> presets = {
        "OLED Monolith (Pure Black and Charcoal)",
        "Cyber Titanium (Charcoal to Electric Slate)",
        "Obsidian Ember (Black to Smoked Burgundy)",
        "Abyssal Blue (Midnight to Oceanic Steel)",
        "Aurora Borealis (3-Color: Navy, Emerald, Indigo)",
        "Solar Dusk (3-Color: Obsidian, Crimson, Bronze)",
        "Monochrome Studio (3-Color: Pure Black, Silver, Ash)",
        "Custom"
    };

    int initial_preset_idx = 0;
    if (g_app.s.grain.preset.find("Cyber") != std::string::npos) initial_preset_idx = 1;
    else if (g_app.s.grain.preset.find("Ember") != std::string::npos) initial_preset_idx = 2;
    else if (g_app.s.grain.preset.find("Abyssal") != std::string::npos) initial_preset_idx = 3;
    else if (g_app.s.grain.preset.find("Aurora") != std::string::npos) initial_preset_idx = 4;
    else if (g_app.s.grain.preset.find("Solar") != std::string::npos) initial_preset_idx = 5;
    else if (g_app.s.grain.preset.find("Studio") != std::string::npos) initial_preset_idx = 6;

    make_combo_row(g_grad, "Gradient and Grain Preset", "Hand-crafted brutalist and chromatic templates", presets, initial_preset_idx, [](int idx, const std::string& name) {
        g_app.s.grain.preset = name;
        switch (idx) {
            case 0: // OLED Monolith
                g_app.s.grain.gradient_type = "vertical";
                g_app.s.grain.color1 = "#000000";
                g_app.s.grain.color2 = "#141416";
                g_app.s.grain.tint = "#d4d4d8";
                break;
            case 1: // Cyber Titanium
                g_app.s.grain.gradient_type = "diagonal_down";
                g_app.s.grain.color1 = "#09090b";
                g_app.s.grain.color2 = "#1e293b";
                g_app.s.grain.tint = "#cbd5e1";
                break;
            case 2: // Obsidian Ember
                g_app.s.grain.gradient_type = "vertical";
                g_app.s.grain.color1 = "#000000";
                g_app.s.grain.color2 = "#260e12";
                g_app.s.grain.tint = "#fca5a5";
                break;
            case 3: // Abyssal Blue
                g_app.s.grain.gradient_type = "diagonal_up";
                g_app.s.grain.color1 = "#020617";
                g_app.s.grain.color2 = "#0f172a";
                g_app.s.grain.tint = "#94a3b8";
                break;
            case 4: // Aurora Borealis (3-Color)
                g_app.s.grain.gradient_type = "mesh_3point";
                g_app.s.grain.color1 = "#030712";
                g_app.s.grain.color2 = "#064e3b";
                g_app.s.grain.color3 = "#312e81";
                g_app.s.grain.tint = "#a7f3d0";
                break;
            case 5: // Solar Dusk (3-Color)
                g_app.s.grain.gradient_type = "mesh_3point";
                g_app.s.grain.color1 = "#050505";
                g_app.s.grain.color2 = "#450a0a";
                g_app.s.grain.color3 = "#78350f";
                g_app.s.grain.tint = "#fed7aa";
                break;
            case 6: // Monochrome Studio (3-Color)
                g_app.s.grain.gradient_type = "mesh_3point";
                g_app.s.grain.color1 = "#000000";
                g_app.s.grain.color2 = "#27272a";
                g_app.s.grain.color3 = "#52525b";
                g_app.s.grain.tint = "#ffffff";
                break;
            default:
                break;
        }
        if (g_app.ctrl_grain_c1) set_color_control_value(g_app.ctrl_grain_c1, g_app.s.grain.color1);
        if (g_app.ctrl_grain_c2) set_color_control_value(g_app.ctrl_grain_c2, g_app.s.grain.color2);
        if (g_app.ctrl_grain_c3) set_color_control_value(g_app.ctrl_grain_c3, g_app.s.grain.color3);
        if (g_app.ctrl_grain_tint) set_color_control_value(g_app.ctrl_grain_tint, g_app.s.grain.tint);
        refresh_grain_preview();
    });

    // Gradient Direction / Geometry
    std::vector<std::string> grad_types = {
        "Solid OLED Field (No Gradient)",
        "2-Color Vertical (Top-to-Bottom)",
        "2-Color Horizontal (Left-to-Right)",
        "2-Color Diagonal Down (Top-Left to Bottom-Right)",
        "2-Color Diagonal Up (Bottom-Left to Top-Right)",
        "Radial Center Vignette (Center to Edges)",
        "3-Color Mesh Gradient (Bottom Center, Top-Left, Top-Right Lower)"
    };
    int cur_gt = 0;
    if (g_app.s.grain.gradient_type == "vertical") cur_gt = 1;
    else if (g_app.s.grain.gradient_type == "horizontal") cur_gt = 2;
    else if (g_app.s.grain.gradient_type == "diagonal_down") cur_gt = 3;
    else if (g_app.s.grain.gradient_type == "diagonal_up") cur_gt = 4;
    else if (g_app.s.grain.gradient_type == "radial") cur_gt = 5;
    else if (g_app.s.grain.gradient_type == "mesh_3point") cur_gt = 6;

    make_combo_row(g_grad, "Gradient Geometry", "Direction and coordinate model for color transitions", grad_types, cur_gt, [](int idx, const std::string&) {
        const char* keys[] = {"none", "vertical", "horizontal", "diagonal_down", "diagonal_up", "radial", "mesh_3point"};
        g_app.s.grain.gradient_type = keys[std::clamp(idx, 0, 6)];
        refresh_grain_preview();
    });

    // Color Pickers
    g_app.ctrl_grain_c1 = make_color_row(g_grad, "Color 1 (Base / Bottom)", "Gradient baseline anchor at bottom center", g_app.s.grain.color1, [](const std::string& s) {
        g_app.s.grain.color1 = s;
        refresh_grain_preview();
    });

    g_app.ctrl_grain_c2 = make_color_row(g_grad, "Color 2 (Top / Left)", "Upper gradient tone at top-left", g_app.s.grain.color2, [](const std::string& s) {
        g_app.s.grain.color2 = s;
        refresh_grain_preview();
    });

    g_app.ctrl_grain_c3 = make_color_row(g_grad, "Color 3 (Top-Right Lower)", "Secondary upper tone for 3-color mesh gradients", g_app.s.grain.color3, [](const std::string& s) {
        g_app.s.grain.color3 = s;
        refresh_grain_preview();
    });

    g_app.ctrl_grain_tint = make_color_row(g_grad, "Grain Particle Tint", "Color tint of the stochastically scattered film grain", g_app.s.grain.tint, [](const std::string& s) {
        g_app.s.grain.tint = s;
        refresh_grain_preview();
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_grad));

    // Group: Mathematical Grain Physics
    AdwPreferencesGroup* g_math = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_math, "Noise Generator and Particle Distribution");
    adw_preferences_group_set_description(g_math, "Mathematical equation governing pixel scattering across the canvas.");

    std::vector<std::string> algos = {
        "Gaussian Box-Muller (Tactile Film Grain)",
        "Harmonic Wave Modulation (Sine and Cosine Fields)",
        "Quantum Poisson Dots (Sparse Micro-Points)",
        "Stochastic Uniform Dither (Digital Texture)"
    };
    int cur_alg = 0;
    if (g_app.s.grain.algorithm == "harmonic") cur_alg = 1;
    else if (g_app.s.grain.algorithm == "quantum") cur_alg = 2;
    else if (g_app.s.grain.algorithm == "dither") cur_alg = 3;

    make_combo_row(g_math, "Noise Distribution Model", "Mathematical distribution of the grain particles", algos, cur_alg, [](int idx, const std::string&) {
        const char* names[] = {"gaussian", "harmonic", "quantum", "dither"};
        g_app.s.grain.algorithm = names[std::clamp(idx, 0, 3)];
        refresh_grain_preview();
    });

    make_slider_row(g_math, "Grain Noise Intensity", "Standard deviation of the random distribution", 0.0, 40.0, 1.0, g_app.s.grain.intensity, &g_app.adj_grain_intensity, [](double v) {
        g_app.s.grain.intensity = v;
        refresh_grain_preview();
    });

    make_slider_row(g_math, "Pixel Grain Scale", "Pixel block multiplier for analog aesthetic", 1.0, 4.0, 1.0, g_app.s.grain.scale, &g_app.adj_grain_scale, [](double v) {
        g_app.s.grain.scale = static_cast<int>(v);
        refresh_grain_preview();
    }, [](double v) { return std::to_string(static_cast<int>(v)) + "x"; });

    make_switch_row(g_math, "Architectural Center Crosshair", "Subtle coordinate alignment marker at screen center", g_app.s.grain.crosshair, [](bool b) {
        g_app.s.grain.crosshair = b;
        refresh_grain_preview();
    });

    // Render Action Button
    AdwActionRow* render_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(render_row), "Render Wallpaper Now");
    adw_action_row_set_subtitle(render_row, "Computes full gradient grain and updates Task Graph &amp; Hyprpaper in RAM");

    GtkWidget* render_btn = gtk_button_new_with_label("Render In RAM");
    gtk_widget_add_css_class(render_btn, "suggested-action");
    gtk_widget_set_valign(render_btn, GTK_ALIGN_CENTER);
    g_signal_connect(render_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        g_app.s.save();
        show_toast("Gradient grain wallpaper rendered and applied!");
    }), nullptr);

    adw_action_row_add_suffix(render_row, render_btn);
    adw_preferences_group_add(g_math, GTK_WIDGET(render_row));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_math));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 3: WINDOW MOTION & BÉZIER CURVES (With Interactive Handle Dragging)
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_motion_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_curv = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_curv, "Interactive Cubic Bézier Curve Engine");
    adw_preferences_group_set_description(g_curv, "Drag P1 (cyan) and P2 (amber) handles directly on the canvas or use the precision sliders.");

    // Bézier widget with drag callback wired to sliders
    g_app.bezier_data.p1x = g_app.s.anim.p1x;
    g_app.bezier_data.p1y = g_app.s.anim.p1y;
    g_app.bezier_data.p2x = g_app.s.anim.p2x;
    g_app.bezier_data.p2y = g_app.s.anim.p2y;
    g_app.bezier_data.on_drag_changed = [](double p1x, double p1y, double p2x, double p2y) {
        g_app.s.anim.p1x = p1x;
        g_app.s.anim.p1y = p1y;
        g_app.s.anim.p2x = p2x;
        g_app.s.anim.p2y = p2y;
        if (g_app.adj_p1x) gtk_adjustment_set_value(g_app.adj_p1x, g_app.s.anim.p1x);
        if (g_app.adj_p1y) gtk_adjustment_set_value(g_app.adj_p1y, g_app.s.anim.p1y);
        if (g_app.adj_p2x) gtk_adjustment_set_value(g_app.adj_p2x, g_app.s.anim.p2x);
        if (g_app.adj_p2y) gtk_adjustment_set_value(g_app.adj_p2y, g_app.s.anim.p2y);
    };
    g_app.bezier_widget = create_bezier_widget(&g_app.bezier_data);

    GtkWidget* center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(center_box, 6);
    gtk_widget_set_margin_bottom(center_box, 10);
    gtk_box_append(GTK_BOX(center_box), g_app.bezier_widget);
    adw_preferences_group_add(g_curv, center_box);

    // Presets
    std::vector<std::string> curve_presets = {
        "Micro Snappy (0.05, 0.90, 0.18, 1.00)",
        "Instant Minimal (0.16, 1.00, 0.30, 1.00)",
        "Gentle Arc (0.25, 0.10, 0.25, 1.00)",
        "Dynamic Smooth (0.10, 0.85, 0.20, 1.00)",
        "Custom Parameters"
    };

    int init_c = 0;
    if (g_app.s.anim.preset.find("Instant") != std::string::npos) init_c = 1;
    else if (g_app.s.anim.preset.find("Gentle") != std::string::npos) init_c = 2;
    else if (g_app.s.anim.preset.find("Dynamic") != std::string::npos) init_c = 3;

    make_combo_row(g_curv, "Animation Physics Preset", "Curated acceleration dynamics for Hyprland window movement", curve_presets, init_c, [](int idx, const std::string& name) {
        g_app.s.anim.preset = name;
        if (idx == 0) {
            g_app.s.anim.p1x = 0.05; g_app.s.anim.p1y = 0.90; g_app.s.anim.p2x = 0.18; g_app.s.anim.p2y = 1.00;
        } else if (idx == 1) {
            g_app.s.anim.p1x = 0.16; g_app.s.anim.p1y = 1.00; g_app.s.anim.p2x = 0.30; g_app.s.anim.p2y = 1.00;
        } else if (idx == 2) {
            g_app.s.anim.p1x = 0.25; g_app.s.anim.p1y = 0.10; g_app.s.anim.p2x = 0.25; g_app.s.anim.p2y = 1.00;
        } else if (idx == 3) {
            g_app.s.anim.p1x = 0.10; g_app.s.anim.p1y = 0.85; g_app.s.anim.p2x = 0.20; g_app.s.anim.p2y = 1.00;
        }
        g_app.bezier_data.p1x = g_app.s.anim.p1x;
        g_app.bezier_data.p1y = g_app.s.anim.p1y;
        g_app.bezier_data.p2x = g_app.s.anim.p2x;
        g_app.bezier_data.p2y = g_app.s.anim.p2y;
        if (g_app.adj_p1x) gtk_adjustment_set_value(g_app.adj_p1x, g_app.s.anim.p1x);
        if (g_app.adj_p1y) gtk_adjustment_set_value(g_app.adj_p1y, g_app.s.anim.p1y);
        if (g_app.adj_p2x) gtk_adjustment_set_value(g_app.adj_p2x, g_app.s.anim.p2x);
        if (g_app.adj_p2y) gtk_adjustment_set_value(g_app.adj_p2y, g_app.s.anim.p2y);
        if (g_app.bezier_widget) gtk_widget_queue_draw(g_app.bezier_widget);
    });

    make_slider_row(g_curv, "Control Point P1.X", "Initial tangent vector on horizontal axis", 0.0, 1.0, 0.01, g_app.s.anim.p1x, &g_app.adj_p1x, [](double v) {
        g_app.s.anim.p1x = v; g_app.bezier_data.p1x = v;
        if (g_app.bezier_widget) gtk_widget_queue_draw(g_app.bezier_widget);
    });
    make_slider_row(g_curv, "Control Point P1.Y", "Initial tangent vector on vertical axis", 0.0, 1.5, 0.01, g_app.s.anim.p1y, &g_app.adj_p1y, [](double v) {
        g_app.s.anim.p1y = v; g_app.bezier_data.p1y = v;
        if (g_app.bezier_widget) gtk_widget_queue_draw(g_app.bezier_widget);
    });
    make_slider_row(g_curv, "Control Point P2.X", "Arrival tangent vector on horizontal axis", 0.0, 1.0, 0.01, g_app.s.anim.p2x, &g_app.adj_p2x, [](double v) {
        g_app.s.anim.p2x = v; g_app.bezier_data.p2x = v;
        if (g_app.bezier_widget) gtk_widget_queue_draw(g_app.bezier_widget);
    });
    make_slider_row(g_curv, "Control Point P2.Y", "Arrival tangent vector on vertical axis", 0.0, 1.5, 0.01, g_app.s.anim.p2y, &g_app.adj_p2y, [](double v) {
        g_app.s.anim.p2y = v; g_app.bezier_data.p2y = v;
        if (g_app.bezier_widget) gtk_widget_queue_draw(g_app.bezier_widget);
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_curv));

    // Durations Group
    AdwPreferencesGroup* g_dur = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_dur, "Animation Durations and Easing");
    adw_preferences_group_set_description(g_dur, "Durations in tenths of a second (1.4 = 140ms).");

    make_slider_row(g_dur, "Window Spawn Speed", "Opening speed when new process launches", 0.5, 4.0, 0.1, g_app.s.anim.windows_in, &g_app.adj_win_in, [](double v) {
        g_app.s.anim.windows_in = v;
    });
    make_slider_row(g_dur, "Window Close Speed", "Dismissal speed when window is terminated", 0.5, 4.0, 0.1, g_app.s.anim.windows_out, &g_app.adj_win_out, [](double v) {
        g_app.s.anim.windows_out = v;
    });
    make_slider_row(g_dur, "Window Move and Resize Speed", "Reflow speed during layout recalculations", 0.5, 4.0, 0.1, g_app.s.anim.windows_move, &g_app.adj_win_move, [](double v) {
        g_app.s.anim.windows_move = v;
    });
    make_slider_row(g_dur, "Workspace Switch Speed", "Horizontal desktop panning duration", 0.5, 4.0, 0.1, g_app.s.anim.workspaces, &g_app.adj_workspaces, [](double v) {
        g_app.s.anim.workspaces = v;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_dur));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 4: MONOLITH CONSTELLATION & WARM-RAM VAULT MANAGER
// ──────────────────────────────────────────────────────────────────────────────
static void update_taskgraph_node_flag(const std::string& node_id, const std::string& field, bool val, const std::string&) {
    std::string cfg_path = config_dir() + "/taskgraph/config.json";
    try {
        nlohmann::json j;
        {
            std::ifstream f(cfg_path);
            if (f.is_open()) f >> j;
        }
        if (j.contains("pinned") && j["pinned"].is_array()) {
            for (auto& n : j["pinned"]) {
                if (n.value("id", "") == node_id) {
                    n[field] = val;
                }
            }
        }
        std::ofstream out(cfg_path);
        if (out.is_open()) out << j.dump(2);

        if (field == "load_to_ram" && !node_id.empty()) {
            std::string act = val ? "load \"" : "unload \"";
            std::string full_cmd = "/usr/local/bin/monolith-ram-preloader " + act + node_id + "\" >/dev/null 2>&1 &";
            int r = system(full_cmd.c_str());
            (void)r;
        }
    } catch (...) {}
}

static GtkWidget* build_taskgraph_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_tg = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_tg, "Monolith Constellation Physics");
    adw_preferences_group_set_description(g_tg, "Interactive force-directed application constellation on the desktop layer (monolith-constellation).");

    make_slider_row(g_tg, "Spring Stiffness", "Hooke's spring constant between connected process nodes", 0.0004, 0.0050, 0.0002, g_app.s.taskgraph.spring_k, nullptr, [](double v) {
        g_app.s.taskgraph.spring_k = v;
    });
    make_slider_row(g_tg, "Rest Length", "Equilibrium distance between connected nodes", 100.0, 500.0, 10.0, g_app.s.taskgraph.rest_length, nullptr, [](double v) {
        g_app.s.taskgraph.rest_length = v;
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " px"; });

    make_slider_row(g_tg, "Coulomb Repulsion Force", "Repulsion magnitude between unlinked nodes", 200.0, 3000.0, 100.0, g_app.s.taskgraph.repulsion, nullptr, [](double v) {
        g_app.s.taskgraph.repulsion = v;
    });
    make_slider_row(g_tg, "Center Gravitation", "Attraction pull toward workspace center", 0.001, 0.050, 0.002, g_app.s.taskgraph.center_gravity, nullptr, [](double v) {
        g_app.s.taskgraph.center_gravity = v;
    });
    make_slider_row(g_tg, "Friction Damping", "Kinetic velocity decay per physics tick", 0.70, 0.98, 0.01, g_app.s.taskgraph.damping, nullptr, [](double v) {
        g_app.s.taskgraph.damping = v;
    });
    make_switch_row(g_tg, "Zero-Overhead Auto Freeze", "Suspends 60Hz timer completely (0.00% CPU) when windows cover desktop", g_app.s.taskgraph.auto_freeze, [](bool b) {
        g_app.s.taskgraph.auto_freeze = b;
    });

    std::vector<std::string> out_items = {"All Displays", "Primary Only"};
    std::vector<std::string> out_values = {"all", "primary"};

    GdkDisplay* gdisp = gdk_display_get_default();
    if (gdisp) {
        GListModel* mons = gdk_display_get_monitors(gdisp);
        if (mons) {
            guint nm = g_list_model_get_n_items(mons);
            for (guint i = 0; i < nm; ++i) {
                GdkMonitor* m = GDK_MONITOR(g_list_model_get_item(mons, i));
                if (m) {
                    const char* conn = gdk_monitor_get_connector(m);
                    if (conn) {
                        out_items.push_back(std::string("Only ") + conn);
                        out_values.push_back(conn);
                    }
                    g_object_unref(m);
                }
            }
        }
    }

    int cur_out_idx = 0;
    for (size_t i = 0; i < out_values.size(); ++i) {
        if (out_values[i] == g_app.s.taskgraph.display_output) {
            cur_out_idx = static_cast<int>(i);
            break;
        }
    }

    make_combo_row(g_tg, "Display Output", "Choose whether Constellation renders across all displays, primary only, or a specific screen", out_items, cur_out_idx, [out_values](int idx, const std::string&) {
        if (idx >= 0 && idx < static_cast<int>(out_values.size())) {
            g_app.s.taskgraph.display_output = out_values[idx];
            g_app.s.save();
        }
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_tg));

    // Group 2: Constellation Nodes & Warm-RAM Vault Manager
    AdwPreferencesGroup* g_nodes = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_nodes, "Constellation Nodes and Warm-RAM Vault");
    adw_preferences_group_set_description(g_nodes, "Manage constellation nodes and Instant-Launch Warm-RAM Vault residency (~1ms open).");

    try {
        std::ifstream f(config_dir() + "/taskgraph/config.json");
        if (f.is_open()) {
            nlohmann::json j;
            f >> j;
            if (j.contains("pinned") && j["pinned"].is_array()) {
                for (const auto& n : j["pinned"]) {
                    std::string nid = n.value("id", "");
                    std::string label = n.value("name", nid);
                    std::string cmd = n.value("exec", "");
                    bool in_ram = n.value("load_to_ram", false);
                    if (nid.empty()) continue;

                    AdwActionRow* nr = ADW_ACTION_ROW(adw_action_row_new());
                    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nr), label.c_str());
                    adw_action_row_set_subtitle(nr, ("Exec: " + cmd).c_str());

                    GtkWidget* suffix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                    gtk_widget_set_valign(suffix, GTK_ALIGN_CENTER);

                    GtkWidget* lbl_ram = gtk_label_new("Instant-Launch RAM Vault");
                    gtk_widget_add_css_class(lbl_ram, "dim-label");
                    GtkWidget* sw_ram = gtk_switch_new();
                    gtk_switch_set_active(GTK_SWITCH(sw_ram), in_ram);
                    gtk_widget_set_valign(sw_ram, GTK_ALIGN_CENTER);

                    struct NodeToggleCtx { std::string id; std::string cmd; };
                    auto* ctx_ram = new NodeToggleCtx{nid, cmd};
                    g_signal_connect_data(sw_ram, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer p) -> gboolean {
                        auto* c = static_cast<NodeToggleCtx*>(p);
                        update_taskgraph_node_flag(c->id, "load_to_ram", state, c->cmd);
                        show_toast(state ? ("Warm-spawning " + c->id + " into RAM Vault...") : ("Released " + c->id + " from RAM Vault."));
                        return FALSE;
                    }), ctx_ram, [](gpointer p, GClosure*) { delete static_cast<NodeToggleCtx*>(p); }, (GConnectFlags)0);

                    gtk_box_append(GTK_BOX(suffix), lbl_ram);
                    gtk_box_append(GTK_BOX(suffix), sw_ram);
                    adw_action_row_add_suffix(nr, suffix);
                    adw_preferences_group_add(g_nodes, GTK_WIDGET(nr));
                }
            }
        }
    } catch (...) {}

    AdwActionRow* preload_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(preload_row), "Synchronize RAM Pagecache Now");
    adw_action_row_set_subtitle(preload_row, "Streams all RAM-flagged binaries and shared libraries into physical memory");

    GtkWidget* preload_btn = gtk_button_new_with_label("Preload All Now");
    gtk_widget_add_css_class(preload_btn, "suggested-action");
    gtk_widget_set_valign(preload_btn, GTK_ALIGN_CENTER);
    g_signal_connect(preload_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        system("/usr/local/bin/monolith-ram-preloader preload-all > /dev/null 2>&1 &");
        show_toast("Parallel RAM preload initiated!");
    }), nullptr);
    adw_action_row_add_suffix(preload_row, preload_btn);
    adw_preferences_group_add(g_nodes, GTK_WIDGET(preload_row));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_nodes));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 5: WAYBAR & STATUS BAR
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_waybar_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_wb = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_wb, "Status Bar Architecture and Layout");
    adw_preferences_group_set_description(g_wb, "Configuration for Waybar position, heights, and drawer behavior.");

    std::vector<std::string> pos_opts = {"Top of Screen (Default)", "Bottom of Screen"};
    make_combo_row(g_wb, "Bar Screen Position", "Vertical anchor for the status bar", pos_opts, (g_app.s.waybar.position == "bottom" ? 1 : 0), [](int idx, const std::string&) {
        g_app.s.waybar.position = (idx == 1 ? "bottom" : "top");
    });

    make_slider_row(g_wb, "Bar Height", "Total vertical thickness in pixels", 24.0, 48.0, 1.0, g_app.s.waybar.height, nullptr, [](double v) {
        g_app.s.waybar.height = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " px"; });

    make_slider_row(g_wb, "Font Size", "Monospace font size for status indicators", 9.0, 16.0, 1.0, g_app.s.waybar.font_size, nullptr, [](double v) {
        g_app.s.waybar.font_size = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " px"; });

    // Top Bar Drawer Behavior
    std::vector<std::string> drawer_modes = {
        "Auto-Hide on Hover (Minimalist)",
        "Always Expanded and Pinned (Convenience)"
    };
    make_combo_row(g_wb, "Tray Drawer Behavior", "Choose between collapsible minimalism or permanently visible icons", drawer_modes, (g_app.s.waybar.drawer_enabled ? 0 : 1), [](int idx, const std::string&) {
        g_app.s.waybar.drawer_enabled = (idx == 0);
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_wb));

    // Module Toggles
    AdwPreferencesGroup* g_mods = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_mods, "Module Visibility");
    adw_preferences_group_set_description(g_mods, "Toggle active modules on the status bar.");

    make_switch_row(g_mods, "Audio and Volume", "WirePlumber audio controls", g_app.s.waybar.show_audio, [](bool b) { g_app.s.waybar.show_audio = b; });
    make_switch_row(g_mods, "Network and Wi-Fi", "NetworkManager connection status", g_app.s.waybar.show_network, [](bool b) { g_app.s.waybar.show_network = b; });
    make_switch_row(g_mods, "Bluetooth", "Bluetooth controller and device manager", g_app.s.waybar.show_bluetooth, [](bool b) { g_app.s.waybar.show_bluetooth = b; });
    make_switch_row(g_mods, "CPU Usage", "Live processor load percentage", g_app.s.waybar.show_cpu, [](bool b) { g_app.s.waybar.show_cpu = b; });
    make_switch_row(g_mods, "RAM Usage", "Physical memory consumption", g_app.s.waybar.show_memory, [](bool b) { g_app.s.waybar.show_memory = b; });
    make_switch_row(g_mods, "Backlight Brightness", "Display brightness indicator", g_app.s.waybar.show_backlight, [](bool b) { g_app.s.waybar.show_backlight = b; });
    make_switch_row(g_mods, "Battery Status", "Laptop battery percentage and charge state", g_app.s.waybar.show_battery, [](bool b) { g_app.s.waybar.show_battery = b; });
    make_switch_row(g_mods, "Night Light Filter", "Quick toggle for Hyprsunset blue light filter", g_app.s.waybar.show_nightlight, [](bool b) { g_app.s.waybar.show_nightlight = b; });
    make_switch_row(g_mods, "Notifications", "SwayNC notification center toggle", g_app.s.waybar.show_notifications, [](bool b) { g_app.s.waybar.show_notifications = b; });
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_mods));

    // Bar Colors & Visual Styling
    AdwPreferencesGroup* g_wb_colors = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_wb_colors, "Status Bar Palette and Theme");
    adw_preferences_group_set_description(g_wb_colors, "Full color control over bar background, borders, module icons, and active workspaces.");

    make_color_row(g_wb_colors, "Bar Background Color", "Surface color of the top bar container", g_app.s.waybar.bg_color, [](const std::string& s) {
        g_app.s.waybar.bg_color = s;
    });
    make_color_row(g_wb_colors, "Bar Border Color", "Bottom separator line color", g_app.s.waybar.border_color, [](const std::string& s) {
        g_app.s.waybar.border_color = s;
    });
    make_color_row(g_wb_colors, "Module Icons and Status Text Color", "Foreground tint for status indicators and icons", g_app.s.waybar.text_color, [](const std::string& s) {
        g_app.s.waybar.text_color = s;
    });
    make_color_row(g_wb_colors, "Active Workspace Button Background", "Pill highlight for the currently focused workspace", g_app.s.waybar.active_ws_bg, [](const std::string& s) {
        g_app.s.waybar.active_ws_bg = s;
    });
    make_color_row(g_wb_colors, "Active Workspace Text or Icon Color", "Text and icon color inside the active workspace pill", g_app.s.waybar.active_ws_text, [](const std::string& s) {
        g_app.s.waybar.active_ws_text = s;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_wb_colors));
    return box;
}

static int timeout_to_idx(int min) {
    if (min <= 0) return 0;
    if (min <= 2) return 1;
    if (min <= 5) return 2;
    if (min <= 10) return 3;
    if (min <= 15) return 4;
    return 5;
}

static int idx_to_timeout(int idx) {
    switch (idx) {
        case 0: return 0;
        case 1: return 2;
        case 2: return 5;
        case 3: return 10;
        case 4: return 15;
        case 5: return 30;
        default: return 10;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 6: INPUT & KEYBOARD
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_input_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    // Group 1: Touchpad Configuration
    AdwPreferencesGroup* g_inp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_inp, "Touchpad Gestures and Acceleration");
    adw_preferences_group_set_description(g_inp, "Direct libinput gestures, inertia, and palm rejection.");

    make_slider_row(g_inp, "Pointer Sensitivity", "Cursor acceleration offset (-1.0 to 1.0)", -1.0, 1.0, 0.05, g_app.s.inp.sensitivity, nullptr, [](double v) {
        g_app.s.inp.sensitivity = v;
    });
    make_switch_row(g_inp, "Natural Scrolling", "Inverts direction to match natural touchpad inertia", g_app.s.inp.natural_scroll, [](bool b) {
        g_app.s.inp.natural_scroll = b;
    });
    make_switch_row(g_inp, "Tap-to-Click", "Single tap acts as left mouse click", g_app.s.inp.tap_to_click, [](bool b) {
        g_app.s.inp.tap_to_click = b;
    });
    make_switch_row(g_inp, "Disable While Typing (DWT)", "Prevents accidental cursor jitter from palm contact", g_app.s.inp.disable_while_typing, [](bool b) {
        g_app.s.inp.disable_while_typing = b;
    });
    make_slider_row(g_inp, "Scroll Speed Multiplier", "Speed scaling factor for two-finger gestures", 0.2, 3.0, 0.1, g_app.s.inp.scroll_factor, nullptr, [](double v) {
        g_app.s.inp.scroll_factor = v;
    }, [](double v) { return std::to_string(v).substr(0, 3) + "x"; });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_inp));

    // Group 2: Keyboard Snappiness & Repeat (Vim / Coding Turbo)
    AdwPreferencesGroup* g_kb = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_kb, "Keyboard Snappiness and Repeat (Vim / Coding Turbo)");
    adw_preferences_group_set_description(g_kb, "Hyper-responsive repeat delays and frequency for effortless code navigation.");

    make_slider_row(g_kb, "Key Repeat Delay", "Delay before character repetition kicks in (Linux default is 600ms)", 150.0, 500.0, 25.0, g_app.s.inp.repeat_delay, nullptr, [](double v) {
        g_app.s.inp.repeat_delay = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " ms"; });

    make_slider_row(g_kb, "Key Repeat Rate", "Number of character repeats per second while holding a key", 20.0, 80.0, 5.0, g_app.s.inp.repeat_rate, nullptr, [](double v) {
        g_app.s.inp.repeat_rate = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " /sec"; });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_kb));

    // Group 3: Pointer & Mouse Ergonomics
    AdwPreferencesGroup* g_mouse = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_mouse, "Mouse Mechanics and Focus Policy");
    adw_preferences_group_set_description(g_mouse, "Precision sensor translation, middle-click behavior, and focus modes.");

    std::vector<std::string> accel_opts = {
        "adaptive - Smooth Desktop Acceleration Curve",
        "flat - Raw 1:1 Sensor Input (Zero Acceleration, Esports / FPS)"
    };
    int accel_idx = (g_app.s.inp.accel_profile == "flat") ? 1 : 0;
    make_combo_row(g_mouse, "Mouse Acceleration Profile", "Flat provides pure 1:1 hardware translation for esports gaming", accel_opts, accel_idx, [](int idx, const std::string&) {
        g_app.s.inp.accel_profile = (idx == 1) ? "flat" : "adaptive";
    });

    std::vector<std::string> focus_opts = {
        "1 - Hover Focus (Cursor movement immediately activates window)",
        "0 - Click to Focus (Prevents accidental typing in wrong window)",
        "2 - Hover Focus Detached (Does not raise floating windows)"
    };
    int focus_idx = (g_app.s.inp.follow_mouse == 0) ? 1 : ((g_app.s.inp.follow_mouse == 2) ? 2 : 0);
    make_combo_row(g_mouse, "Window Focus Follows Cursor", "Determines how cursor movement influences keyboard and window focus", focus_opts, focus_idx, [](int idx, const std::string&) {
        if (idx == 0) g_app.s.inp.follow_mouse = 1;
        else if (idx == 1) g_app.s.inp.follow_mouse = 0;
        else g_app.s.inp.follow_mouse = 2;
    });

    make_switch_row(g_mouse, "Middle-Click Primary Paste", "Pastes primary text buffer on middle-click (Disable to avoid code pollution)", g_app.s.inp.middle_click_paste, [](bool b) {
        g_app.s.inp.middle_click_paste = b;
    });

    make_switch_row(g_mouse, "Left-Handed Mouse Inversion", "Inverts primary left and right mouse buttons for southpaw ergonomics", g_app.s.inp.left_handed, [](bool b) {
        g_app.s.inp.left_handed = b;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_mouse));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 7: DISPLAY, GAMING & POWER
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_display_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    // Multi-Monitor Output and Calibration
    if (g_app.s.monitors_list.empty()) {
        g_app.s.detect_monitor();
    }

    for (size_t mi = 0; mi < g_app.s.monitors_list.size(); ++mi) {
        auto& mon = g_app.s.monitors_list[mi];
        AdwPreferencesGroup* g_dsp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        std::string grp_title = (mi == 0 ? "󰍹 Primary Display: " : "󰍺 External Display: ") + mon.name;
        adw_preferences_group_set_title(g_dsp, grp_title.c_str());
        std::string grp_desc = mon.description + " • Active Mode: " + mon.mode;
        adw_preferences_group_set_description(g_dsp, grp_desc.c_str());

        // Enable / Disable monitor
        make_switch_row(g_dsp, "Enable Display Output", "Powers on or disables this physical display output", mon.enabled, [&mon](bool b) {
            mon.enabled = b;
        });

        // Positioning
        std::vector<std::string> pos_labels = {
            "auto - Automatic Layout",
            "auto-right - Right of Primary Screen",
            "auto-left - Left of Primary Screen",
            "auto-up - Above Primary Screen",
            "auto-down - Below Primary Screen",
            "0x0 - Primary Origin",
            "mirror - Mirror Primary Output"
        };
        std::vector<std::string> pos_values = {
            "auto", "auto-right", "auto-left", "auto-up", "auto-down", "0x0", "mirror"
        };
        int current_pos_idx = 0;
        for (size_t pi = 0; pi < pos_values.size(); ++pi) {
            if (mon.position == pos_values[pi]) {
                current_pos_idx = static_cast<int>(pi);
                break;
            }
        }
        make_combo_row(g_dsp, "Screen Placement & Layout", "Spatial alignment relative to other displays in virtual desktop", pos_labels, current_pos_idx, [&mon, pos_values](int idx, const std::string&) {
            if (idx >= 0 && idx < static_cast<int>(pos_values.size())) {
                mon.position = pos_values[idx];
            }
        });

        // Resolution and Refresh Rate
        std::vector<std::string> mode_opts;
        if (!mon.mode.empty()) mode_opts.push_back(mon.mode);
        mode_opts.push_back("preferred - Highest Native Resolution & Refresh");
        mode_opts.push_back("highrr - Maximum Supported Refresh Rate");
        for (const auto& am : mon.available_modes) {
            if (std::find(mode_opts.begin(), mode_opts.end(), am) == mode_opts.end()) {
                mode_opts.push_back(am);
            }
        }
        make_combo_row(g_dsp, "Resolution & Refresh Rate", "Operating resolution and scan rate for this monitor", mode_opts, 0, [&mon, mode_opts](int idx, const std::string&) {
            if (idx >= 0 && idx < static_cast<int>(mode_opts.size())) {
                std::string chosen = mode_opts[idx];
                size_t dash = chosen.find(" - ");
                mon.mode = (dash != std::string::npos) ? chosen.substr(0, dash) : chosen;
            }
        });

        // Scale slider
        make_slider_row(g_dsp, "Display Fractional Scale", "Pixel scaling factor for high-DPI panels", 1.0, 2.5, 0.05, mon.scale, nullptr, [&mon](double v) {
            mon.scale = v;
        }, [](double v) { return std::to_string(v).substr(0, 4) + "x"; });

        // Adaptive Sync / VRR
        std::vector<std::string> vrr_opts = {"0 - Disabled (Fixed Refresh Rate)", "1 - Variable Refresh Rate (FreeSync/Adaptive)", "2 - VRR on Fullscreen Games Only"};
        make_combo_row(g_dsp, "Adaptive Sync / VRR", "Variable refresh rate for tear-free gaming on this display", vrr_opts, mon.vrr, [&mon](int idx, const std::string&) {
            mon.vrr = idx;
        });

        gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_dsp));
    }

    // Group 2: Gaming & Latency Optimization
    AdwPreferencesGroup* g_game = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_game, "Gaming and Latency Optimization");
    adw_preferences_group_set_description(g_game, "Hardware scanout and direct tearing bypasses compositor latency for competitive gaming.");

    make_switch_row(g_game, "Allow Screen Tearing", "Permits tearing in fullscreen mode (eliminates compositor queue lag in CS2, Dota 2, etc.)", g_app.s.disp.allow_tearing, [](bool b) {
        g_app.s.disp.allow_tearing = b;
    });

    make_switch_row(g_game, "Direct Scanout (Zero Copy)", "Bypasses compositor blending for fullscreen windows, sending frames straight to display", g_app.s.disp.direct_scanout, [](bool b) {
        g_app.s.disp.direct_scanout = b;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_game));

    // Group 3: Display Power & Inactivity (Hypridle)
    AdwPreferencesGroup* g_pwr = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_pwr, "Display Sleep and Power Saving (Hypridle)");
    adw_preferences_group_set_description(g_pwr, "Automated DPMS screen shutoff protects OLED panels and extends battery life.");

    std::vector<std::string> timeout_opts = {
        "0 - Disabled (Never sleep)",
        "2 - 2 minutes (Aggressive Battery Saver)",
        "5 - 5 minutes (Standard Laptop)",
        "10 - 10 minutes (Recommended)",
        "15 - 15 minutes",
        "30 - 30 minutes"
    };
    make_combo_row(g_pwr, "Display Inactivity Sleep", "Turns off display signal (DPMS off) after specified inactivity", timeout_opts, timeout_to_idx(g_app.s.disp.idle_timeout_min), [](int idx, const std::string&) {
        g_app.s.disp.idle_timeout_min = idx_to_timeout(idx);
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_pwr));

    // Group 4: Hardware Health & Battery Longevity
    AdwPreferencesGroup* g_bat = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_bat, "Hardware Health and Battery Longevity");
    adw_preferences_group_set_description(g_bat, "Hardware charge threshold preservation via ACPI sysfs controller.");

    make_switch_row(g_bat, "Battery 80% Health Extender", "Caps charge at 80% to dramatically preserve lithium battery chemistry while plugged into AC", g_app.s.disp.battery_limit_80, [](bool b) {
        g_app.s.disp.battery_limit_80 = b;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_bat));

    // Group 5: Night Light and Color Temperature
    AdwPreferencesGroup* g_nl = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_nl, "Night Light and Color Temperature (Hyprsunset)");
    adw_preferences_group_set_description(g_nl, "Gentle warm color tint reduces eye strain in dark environments.");

    make_switch_row(g_nl, "Enable Night Light Filter", "Applies warm color temperature in real-time", g_app.s.night.enabled, [](bool b) {
        g_app.s.night.enabled = b;
        if (b) {
            std::string cmd = "hyprsunset -t " + std::to_string(g_app.s.night.temp) + " &";
            system(cmd.c_str());
        } else {
            system("pkill -x hyprsunset");
        }
    });

    make_slider_row(g_nl, "Color Temperature", "Warmer Kelvin values filter out blue light", 2500.0, 6500.0, 100.0, g_app.s.night.temp, nullptr, [](double v) {
        g_app.s.night.temp = static_cast<int>(v);
        if (g_app.s.night.enabled) {
            std::string cmd = "pkill -x hyprsunset; hyprsunset -t " + std::to_string(g_app.s.night.temp) + " &";
            system(cmd.c_str());
        }
    }, [](double v) { return std::to_string(static_cast<int>(v)) + " K"; });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_nl));

    // Group 6: Privacy and Clipboard Vault
    AdwPreferencesGroup* g_priv = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_priv, "Privacy and Clipboard Vault");
    adw_preferences_group_set_description(g_priv, "Immediate sanitization of sensitive clipboard caches, tokens, and copied credentials.");

    AdwActionRow* r_wipe = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r_wipe), "Wipe Clipboard History Cache");
    adw_action_row_set_subtitle(r_wipe, "Immediately purges all stored text items, screenshots, and credentials from cliphist");
    GtkWidget* btn_wipe = gtk_button_new_with_label("Wipe Database");
    gtk_widget_set_valign(btn_wipe, GTK_ALIGN_CENTER);
    g_signal_connect(btn_wipe, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        system("cliphist wipe >/dev/null 2>&1");
        show_toast("Clipboard database wiped clean.");
    }), nullptr);
    adw_action_row_add_suffix(r_wipe, btn_wipe);
    adw_preferences_group_add(g_priv, GTK_WIDGET(r_wipe));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_priv));

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 8: SYSTEM UPDATES & AUTONOMOUS LIFECYCLE MANAGER
// ──────────────────────────────────────────────────────────────────────────────
struct UpdateCheckResult {
    std::string status = "unreachable";
    std::string current_version = MONOLITH_VERSION;
    std::string current_commit = "unknown";
    std::string latest_version = MONOLITH_VERSION;
    std::string tag_name = "v" MONOLITH_VERSION;
    std::string release_name = "";
    std::string release_date = "";
    std::string changelog = "";
    std::string url = "";
    std::string channel = "releases";
    bool update_available = false;
};

static void run_update_check(bool is_auto) {
    if (g_app.check_in_progress.exchange(true)) return;

    if (g_app.updates_status_badge) {
        gtk_label_set_text(GTK_LABEL(g_app.updates_status_badge), "● Checking...");
        gtk_widget_remove_css_class(g_app.updates_status_badge, "badge-uptodate");
        gtk_widget_remove_css_class(g_app.updates_status_badge, "badge-update-avail");
        gtk_widget_remove_css_class(g_app.updates_status_badge, "badge-warning");
        gtk_widget_add_css_class(g_app.updates_status_badge, "badge-checking");
    }
    if (g_app.updates_status_row) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.updates_status_row), "Querying GitHub API for latest releases...");
    }
    if (g_app.updates_check_btn) {
        gtk_widget_set_sensitive(g_app.updates_check_btn, FALSE);
    }

    std::string repo = g_app.s.updates.repo;
    std::string channel = g_app.s.updates.channel;

    std::thread([repo, channel, is_auto]() {
        std::string cmd = config_dir() + "/scripts/monolith-updater.sh --check " + repo + " " + channel;
        std::string json_raw = run_cmd(cmd);

        auto* res = new UpdateCheckResult();
        try {
            auto j = nlohmann::json::parse(json_raw);
            if (j.contains("status")) res->status = j["status"].get<std::string>();
            if (j.contains("current_version")) res->current_version = j["current_version"].get<std::string>();
            if (j.contains("current_commit")) res->current_commit = j["current_commit"].get<std::string>();
            if (j.contains("latest_version")) res->latest_version = j["latest_version"].get<std::string>();
            if (j.contains("tag_name")) res->tag_name = j["tag_name"].get<std::string>();
            if (j.contains("release_name")) res->release_name = j["release_name"].get<std::string>();
            if (j.contains("release_date")) res->release_date = j["release_date"].get<std::string>();
            if (j.contains("changelog")) res->changelog = j["changelog"].get<std::string>();
            if (j.contains("url")) res->url = j["url"].get<std::string>();
            if (j.contains("channel")) res->channel = j["channel"].get<std::string>();
            if (j.contains("update_available")) res->update_available = j["update_available"].get<bool>();
        } catch (...) {
            res->status = "error";
            res->changelog = "Failed to parse update server response.";
        }

        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)+[](gpointer data) -> gboolean {
            auto* r = static_cast<UpdateCheckResult*>(data);

            if (g_app.updates_status_badge && g_app.updates_status_row) {
                gtk_widget_remove_css_class(g_app.updates_status_badge, "badge-checking");
                gtk_widget_remove_css_class(g_app.updates_status_badge, "badge-uptodate");
                gtk_widget_remove_css_class(g_app.updates_status_badge, "badge-update-avail");
                gtk_widget_remove_css_class(g_app.updates_status_badge, "badge-warning");

                if (r->update_available) {
                    gtk_label_set_text(GTK_LABEL(g_app.updates_status_badge), ("● UPDATE: v" + r->latest_version).c_str());
                    gtk_widget_add_css_class(g_app.updates_status_badge, "badge-update-avail");
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.updates_status_row), ("New release available: " + r->release_name).c_str());
                    if (g_app.updates_upgrade_btn) {
                        gtk_button_set_label(GTK_BUTTON(g_app.updates_upgrade_btn), ("Upgrade to v" + r->latest_version + " Now").c_str());
                    }
                    if (!r->release_name.empty()) {
                        show_toast("New release detected: " + r->release_name);
                    }
                } else if (r->status == "unreachable") {
                    gtk_label_set_text(GTK_LABEL(g_app.updates_status_badge), "● REPO PENDING");
                    gtk_widget_add_css_class(g_app.updates_status_badge, "badge-warning");
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.updates_status_row), "Repository not yet published or private. System running local build.");
                    if (g_app.updates_upgrade_btn) {
                        gtk_button_set_label(GTK_BUTTON(g_app.updates_upgrade_btn), "Rebuild / Reinstall Monolith");
                    }
                } else {
                    gtk_label_set_text(GTK_LABEL(g_app.updates_status_badge), "● UP TO DATE");
                    gtk_widget_add_css_class(g_app.updates_status_badge, "badge-uptodate");
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.updates_status_row), "Your system is running the latest release build.");
                    if (g_app.updates_upgrade_btn) {
                        gtk_button_set_label(GTK_BUTTON(g_app.updates_upgrade_btn), "Rebuild / Reinstall Monolith");
                    }
                }
            }

            if (g_app.updates_changelog_buffer) {
                std::string header = "";
                if (!r->release_name.empty()) {
                    header += "Release: " + r->release_name + "\n";
                }
                if (!r->release_date.empty()) {
                    header += "Date:    " + r->release_date + "\n";
                }
                if (!r->url.empty()) {
                    header += "URL:     " + r->url + "\n";
                }
                if (!header.empty()) {
                    header += "────────────────────────────────────────────────────────────────\n\n";
                }
                std::string full_text = header + r->changelog;
                gtk_text_buffer_set_text(g_app.updates_changelog_buffer, full_text.c_str(), -1);
            }

            if (g_app.updates_check_btn) {
                gtk_widget_set_sensitive(g_app.updates_check_btn, TRUE);
            }
            if (g_app.updates_upgrade_btn) {
                gtk_widget_set_sensitive(g_app.updates_upgrade_btn, TRUE);
            }

            g_app.check_in_progress.store(false);
            return G_SOURCE_REMOVE;
        }, res, [](gpointer p) { delete static_cast<UpdateCheckResult*>(p); });
    }).detach();
}

struct LogChunk {
    std::string text;
    double progress = -1.0;
    std::string step_desc;
    bool is_done = false;
    bool success = false;
};

static void run_system_upgrade() {
    if (g_app.updates_in_progress.exchange(true)) return;

    if (g_app.updates_upgrade_btn) {
        gtk_widget_set_sensitive(g_app.updates_upgrade_btn, FALSE);
        gtk_button_set_label(GTK_BUTTON(g_app.updates_upgrade_btn), "Upgrading System...");
    }
    if (g_app.updates_check_btn) {
        gtk_widget_set_sensitive(g_app.updates_check_btn, FALSE);
    }
    if (g_app.updates_revealer) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(g_app.updates_revealer), TRUE);
    }
    if (g_app.updates_revealer_btn) {
        gtk_button_set_label(GTK_BUTTON(g_app.updates_revealer_btn), "Hide Live Build Log 󰅀");
    }
    if (g_app.updates_log_buffer) {
        gtk_text_buffer_set_text(g_app.updates_log_buffer, "", 0);
    }
    if (g_app.updates_progress_bar) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_app.updates_progress_bar), 0.05);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_app.updates_progress_bar), "Initiating upgrade...");
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(g_app.updates_progress_bar), TRUE);
    }

    std::string repo = g_app.s.updates.repo;

    std::thread([repo]() {
        std::string cmd = config_dir() + "/scripts/monolith-updater.sh --upgrade " + repo + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");

        if (!pipe) {
            auto* chunk = new LogChunk{"Failed to launch updater script.\n", 0.0, "Error launching script", true, false};
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)+[](gpointer data) -> gboolean {
                auto* c = static_cast<LogChunk*>(data);
                if (g_app.updates_log_buffer) gtk_text_buffer_insert_at_cursor(g_app.updates_log_buffer, c->text.c_str(), -1);
                if (g_app.updates_upgrade_btn) {
                    gtk_widget_set_sensitive(g_app.updates_upgrade_btn, TRUE);
                    gtk_button_set_label(GTK_BUTTON(g_app.updates_upgrade_btn), "Upgrade Failed (Retry)");
                }
                if (g_app.updates_check_btn) gtk_widget_set_sensitive(g_app.updates_check_btn, TRUE);
                g_app.updates_in_progress.store(false);
                return G_SOURCE_REMOVE;
            }, chunk, [](gpointer p) { delete static_cast<LogChunk*>(p); });
            return;
        }

        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            double prog = -1.0;
            std::string step_desc = "";

            if (line.find("[STEP 1/6]") != std::string::npos) {
                prog = 0.16;
                step_desc = "[1/6] Backing up configuration...";
            } else if (line.find("[STEP 2/6]") != std::string::npos) {
                prog = 0.32;
                step_desc = "[2/6] Synchronizing repository...";
            } else if (line.find("[STEP 3/6]") != std::string::npos) {
                prog = 0.50;
                step_desc = "[3/6] Compiling native suite...";
            } else if (line.find("[STEP 4/6]") != std::string::npos) {
                prog = 0.70;
                step_desc = "[4/6] Installing binaries...";
            } else if (line.find("[STEP 5/6]") != std::string::npos) {
                prog = 0.85;
                step_desc = "[5/6] Updating services...";
            } else if (line.find("[STEP 6/6]") != std::string::npos) {
                prog = 0.95;
                step_desc = "[6/6] Reloading desktop session...";
            } else if (line.find("[COMPLETE]") != std::string::npos) {
                prog = 1.0;
                step_desc = "Update completed successfully!";
            }

            auto* chunk = new LogChunk{line, prog, step_desc, false, false};
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)+[](gpointer data) -> gboolean {
                auto* c = static_cast<LogChunk*>(data);
                if (g_app.updates_log_buffer) {
                    GtkTextIter end;
                    gtk_text_buffer_get_end_iter(g_app.updates_log_buffer, &end);
                    gtk_text_buffer_insert(g_app.updates_log_buffer, &end, c->text.c_str(), -1);
                }
                if (g_app.updates_log_view) {
                    GtkTextIter end;
                    gtk_text_buffer_get_end_iter(g_app.updates_log_buffer, &end);
                    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(g_app.updates_log_view), &end, 0.0, FALSE, 0.0, 0.0);
                }
                if (c->progress >= 0.0 && g_app.updates_progress_bar) {
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_app.updates_progress_bar), c->progress);
                    if (!c->step_desc.empty()) {
                        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_app.updates_progress_bar), c->step_desc.c_str());
                    }
                }
                return G_SOURCE_REMOVE;
            }, chunk, [](gpointer p) { delete static_cast<LogChunk*>(p); });
        }

        int exit_code = pclose(pipe);
        bool ok = (exit_code == 0);

        auto* final_chunk = new LogChunk{"", ok ? 1.0 : 0.0, ok ? "Upgrade Complete!" : "Upgrade Failed", true, ok};
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)+[](gpointer data) -> gboolean {
            auto* c = static_cast<LogChunk*>(data);
            if (g_app.updates_progress_bar) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_app.updates_progress_bar), c->progress);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_app.updates_progress_bar), c->step_desc.c_str());
            }
            if (g_app.updates_upgrade_btn) {
                gtk_widget_set_sensitive(g_app.updates_upgrade_btn, TRUE);
                gtk_button_set_label(GTK_BUTTON(g_app.updates_upgrade_btn), c->success ? "Rebuild / Reinstall Monolith" : "Retry Upgrade");
            }
            if (g_app.updates_check_btn) {
                gtk_widget_set_sensitive(g_app.updates_check_btn, TRUE);
            }
            show_toast(c->success ? "Monolith upgraded and reloaded successfully." : "Upgrade failed. Check build logs.");
            g_app.updates_in_progress.store(false);

            if (c->success) {
                run_update_check(true);
            }
            return G_SOURCE_REMOVE;
        }, final_chunk, [](gpointer p) { delete static_cast<LogChunk*>(p); });
    }).detach();
}

static GtkWidget* build_updates_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 24);

    // Group 1: Release & Channel
    AdwPreferencesGroup* g_rel = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_rel, "System Release and Update Channel");
    adw_preferences_group_set_description(g_rel, "Automated release tracking and binary lifecycle manager.");

    // Row: Current Installed Version
    AdwActionRow* r_ver = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r_ver), "Installed Version");
    adw_action_row_set_subtitle(r_ver, ("Native Monolith C++20 Suite (Built " MONOLITH_BUILD_DATE ")"));
    GtkWidget* ver_badge = gtk_label_new("v" MONOLITH_VERSION);
    gtk_widget_add_css_class(ver_badge, "badge");
    adw_action_row_add_suffix(r_ver, ver_badge);
    adw_preferences_group_add(g_rel, GTK_WIDGET(r_ver));

    // Row: Upstream Status & Check Action
    g_app.updates_status_row = GTK_WIDGET(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(g_app.updates_status_row), "Upstream Status");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(g_app.updates_status_row), "Click Check Now to query GitHub releases.");

    g_app.updates_status_badge = gtk_label_new("● READY");
    gtk_widget_add_css_class(g_app.updates_status_badge, "badge");

    g_app.updates_check_btn = gtk_button_new_with_label("Check Now");
    gtk_widget_add_css_class(g_app.updates_check_btn, "suggested-action");
    g_signal_connect(g_app.updates_check_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        run_update_check(false);
    }), nullptr);

    adw_action_row_add_suffix(ADW_ACTION_ROW(g_app.updates_status_row), g_app.updates_status_badge);
    adw_action_row_add_suffix(ADW_ACTION_ROW(g_app.updates_status_row), g_app.updates_check_btn);
    adw_preferences_group_add(g_rel, g_app.updates_status_row);

    // Channel Combo
    std::vector<std::string> channels = {"Tagged Releases (Stable)", "Main Branch (Bleeding Edge)"};
    int chan_idx = (g_app.s.updates.channel == "commits") ? 1 : 0;
    make_combo_row(g_rel, "Release Channel", "Select upstream release tracking mechanism", channels, chan_idx, [](int idx, const std::string&) {
        g_app.s.updates.channel = (idx == 1) ? "commits" : "releases";
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_rel));

    // Group 2: Upstream Configuration
    AdwPreferencesGroup* g_cfg = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_cfg, "Upstream Repository Configuration");
    adw_preferences_group_set_description(g_cfg, "GitHub repository coordinates and safety preferences.");

    make_entry_row(g_cfg, "GitHub Repository (owner/repo)", g_app.s.updates.repo, [](const std::string& v) {
        g_app.s.updates.repo = v;
    });

    make_switch_row(g_cfg, "Pre-Update Backup", "Create a timestamped archive of configs in ~/.config/monolith/backups", g_app.s.updates.backup_before_update, [](bool a) {
        g_app.s.updates.backup_before_update = a;
    });

    make_switch_row(g_cfg, "Check on Startup", "Automatically check for newer releases when Settings opens", g_app.s.updates.auto_check_on_startup, [](bool a) {
        g_app.s.updates.auto_check_on_startup = a;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_cfg));

    // Group 3: Release Notes & Changelog
    AdwPreferencesGroup* g_notes = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_notes, "Release Notes and Changelog");
    adw_preferences_group_set_description(g_notes, "Upstream release notes, commits, and upgrade summary.");

    GtkWidget* notes_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(notes_card, "boxed-list");

    GtkWidget* notes_scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(notes_scrolled, -1, 140);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(notes_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    g_app.updates_changelog_buffer = gtk_text_buffer_new(nullptr);
    gtk_text_buffer_set_text(g_app.updates_changelog_buffer, "Upstream release notes will appear here after checking.\nWhen you publish a new release tag (e.g. v1.3.0) on GitHub, its full release description and changelog will be displayed in this view.", -1);

    g_app.updates_changelog_view = gtk_text_view_new_with_buffer(g_app.updates_changelog_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_app.updates_changelog_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g_app.updates_changelog_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_app.updates_changelog_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(g_app.updates_changelog_view, "log-box");

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(notes_scrolled), g_app.updates_changelog_view);
    gtk_box_append(GTK_BOX(notes_card), notes_scrolled);
    adw_preferences_group_add(g_notes, notes_card);

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_notes));

    // Group 4: Automated System Upgrade Action
    AdwPreferencesGroup* g_act = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_act, "Autonomous System Upgrade");
    adw_preferences_group_set_description(g_act, "Executes git synchronization, native C++ parallel build, binary installation, and shell reload.");

    GtkWidget* act_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(act_box, "boxed-list");
    gtk_widget_set_margin_top(act_box, 4);

    // Progress Bar
    g_app.updates_progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_app.updates_progress_bar), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(g_app.updates_progress_bar), "Idle");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(g_app.updates_progress_bar), TRUE);
    gtk_widget_set_margin_start(g_app.updates_progress_bar, 14);
    gtk_widget_set_margin_end(g_app.updates_progress_bar, 14);
    gtk_widget_set_margin_top(g_app.updates_progress_bar, 12);
    gtk_box_append(GTK_BOX(act_box), g_app.updates_progress_bar);

    // Button Row
    GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(btn_row, 14);
    gtk_widget_set_margin_end(btn_row, 14);
    gtk_widget_set_margin_bottom(btn_row, 12);

    g_app.updates_revealer_btn = gtk_button_new_with_label("Show Live Build Log 󰅂");
    gtk_widget_add_css_class(g_app.updates_revealer_btn, "suggested-action");

    g_app.updates_upgrade_btn = gtk_button_new_with_label("Upgrade Monolith Now");
    gtk_widget_add_css_class(g_app.updates_upgrade_btn, "upgrade-btn");
    gtk_widget_set_hexpand(g_app.updates_upgrade_btn, TRUE);
    g_signal_connect(g_app.updates_upgrade_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        run_system_upgrade();
    }), nullptr);

    gtk_box_append(GTK_BOX(btn_row), g_app.updates_revealer_btn);
    gtk_box_append(GTK_BOX(btn_row), g_app.updates_upgrade_btn);
    gtk_box_append(GTK_BOX(act_box), btn_row);

    // Revealer for Live Log Output
    g_app.updates_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(g_app.updates_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(g_app.updates_revealer), FALSE);

    g_signal_connect(g_app.updates_revealer_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        gboolean cur = gtk_revealer_get_reveal_child(GTK_REVEALER(g_app.updates_revealer));
        gtk_revealer_set_reveal_child(GTK_REVEALER(g_app.updates_revealer), !cur);
        gtk_button_set_label(btn, !cur ? "Hide Live Build Log 󰅀" : "Show Live Build Log 󰅂");
    }), nullptr);

    GtkWidget* log_scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(log_scrolled, -1, 180);
    gtk_widget_set_margin_start(log_scrolled, 14);
    gtk_widget_set_margin_end(log_scrolled, 14);
    gtk_widget_set_margin_bottom(log_scrolled, 14);

    g_app.updates_log_buffer = gtk_text_buffer_new(nullptr);
    g_app.updates_log_view = gtk_text_view_new_with_buffer(g_app.updates_log_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_app.updates_log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g_app.updates_log_view), FALSE);
    gtk_widget_add_css_class(g_app.updates_log_view, "log-box");

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scrolled), g_app.updates_log_view);
    gtk_revealer_set_child(GTK_REVEALER(g_app.updates_revealer), log_scrolled);
    gtk_box_append(GTK_BOX(act_box), g_app.updates_revealer);

    adw_preferences_group_add(g_act, act_box);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_act));

    // If auto-check enabled, trigger on start
    if (g_app.s.updates.auto_check_on_startup) {
        g_idle_add((GSourceFunc)+[](gpointer) -> gboolean {
            run_update_check(true);
            return G_SOURCE_REMOVE;
        }, nullptr);
    }

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 8B: SHORTCUTS & 2D SPATIAL NAVIGATION REFERENCE
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_shortcuts_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    auto make_shortcut_group = [&](const std::string& title, const std::string& desc, const std::vector<std::pair<std::string, std::string>>& items) {
        AdwPreferencesGroup* grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        adw_preferences_group_set_title(grp, title.c_str());
        adw_preferences_group_set_description(grp, desc.c_str());
        for (const auto& kv : items) {
            AdwActionRow* r = ADW_ACTION_ROW(adw_action_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), kv.first.c_str());
            GtkWidget* kbd = gtk_label_new(kv.second.c_str());
            gtk_widget_add_css_class(kbd, "badge");
            adw_action_row_add_suffix(r, kbd);
            adw_preferences_group_add(grp, GTK_WIDGET(r));
        }
        gtk_box_append(GTK_BOX(box), GTK_WIDGET(grp));
    };

    make_shortcut_group("2D Spatial Navigation &amp; Gestures",
        "Horizontal workspaces (X-axis) combined with vertical special layers (Y-axis).", {
        {"Horizontal Workspace Pan (1..9)", "3-Finger Swipe Left / Right  or  Super + 1..9"},
        {"Top Media Layer (Spotify Special Tab)", "3-Finger Swipe Up  or  Super + M"},
        {"Bottom Extra Shelf (Window Stash Tab)", "3-Finger Swipe Down  or  Super + S"},
        {"Stash Active Window into Extra Shelf", "Super + Scroll Down  or  Super + Shift + S"},
        {"Restore Stashed Window to Current Workspace", "Super + Scroll Up  or  Middle-Click Node"},
        {"Waybar Spatial Tab Switch / Stash", "Left/Right/Middle/Scroll on [S] / [E] Badges"}
    });

    make_shortcut_group("Core Shell &amp; Native Daemons",
        "Instant zero-bloat tools and launchers.", {
        {"Terminal (Foot Server Client)", "Super + Return"},
        {"Application Launcher (Fuzzel + Orbital Graph)", "Super + Space  or  Super + D"},
        {"Monolith Control Center (Settings GUI)", "Super + I  or  Super + ,"},
        {"Edge Toolbox (Timer / Calc / Canvas / Notes)", "Super + G  or  Left/Right Screen Edge"},
        {"Notification Center (SwayNC)", "Super + N"},
        {"Clipboard History Vault (Cliphist)", "Super + V"},
        {"Lock Screen (Hyprlock)", "Super + Escape"},
        {"Power Session Menu", "Super + BackSpace"}
    });

    make_shortcut_group("Window Management &amp; Screenshots",
        "Dwindle layout controls and region capture.", {
        {"Close Active Window", "Super + Q"},
        {"Toggle Floating / Fullscreen", "Super + F  /  Super + Shift + F"},
        {"Move Focus (Vim or Arrow Keys)", "Super + H / J / K / L"},
        {"Swap / Move Window", "Super + Shift + H / J / K / L"},
        {"Region Screenshot to Clipboard", "Print  or  Super + Shift + P"},
        {"Region Screenshot to ~/Pictures", "Shift + Print"}
    });

    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 9: ABOUT MONOLITH
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_about_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_abt = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_abt, "Monolith Architecture");
    adw_preferences_group_set_description(g_abt, "Minimalist high-performance native C++20 shell environment for Hyprland.");

    auto add_info = [&](const std::string& title, const std::string& val) {
        AdwActionRow* r = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title.c_str());
        GtkWidget* lbl = gtk_label_new(val.c_str());
        gtk_widget_add_css_class(lbl, "dim-label");
        adw_action_row_add_suffix(r, lbl);
        adw_preferences_group_add(g_abt, GTK_WIDGET(r));
    };

    add_info("Version", "v" MONOLITH_VERSION " (" MONOLITH_BUILD_DATE ")");
    add_info("Compositor", "Hyprland (Wayland Direct Scanout + UNIX Socket IPC)");
    add_info("Native Tool Suite", "C++20 (GTK4 + gtk4-layer-shell + Libadwaita + Cairo)");
    add_info("Memory Preloader", "Linux Pagecache posix_fadvise(POSIX_FADV_WILLNEED) (0ms cold start)");
    add_info("Mathematical Engine", "Box-Muller Normal, Smoothstep Mesh & Sub-LSB Dither");
    add_info("License", "MIT Open Source");

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_abt));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// APPLY SETTINGS ACTION
// ──────────────────────────────────────────────────────────────────────────────
static void on_apply_clicked(GtkButton*, gpointer) {
    g_app.s.save();
    show_toast("All settings and visual parameters applied successfully.");
}

// ──────────────────────────────────────────────────────────────────────────────
// MAIN APPLICATION INITIALIZATION
// ──────────────────────────────────────────────────────────────────────────────
static void on_app_activate(GApplication* app, gpointer) {
    g_app.s.detect_monitor();
    g_app.s.detect_battery_limit();
    g_app.s.load();

    // Create main window
    g_app.window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(g_app.window), "Monolith Settings & Control Center");
    gtk_window_set_default_size(GTK_WINDOW(g_app.window), 980, 720);

    // Apply custom styling
    const char* css = R"(
        window {
            background-color: #000000;
            color: #ffffff;
            font-family: "JetBrains Mono", monospace;
        }
        headerbar {
            background-color: #000000;
            border-bottom: 1px solid #141416;
            min-height: 48px;
            padding: 0 12px;
        }
        headerbar .heading {
            font-family: "JetBrains Mono", monospace;
            font-size: 13px;
            font-weight: 700;
            letter-spacing: 1px;
            color: #ffffff;
        }
        search-bar, entry {
            background-color: #09090b;
            color: #ffffff;
            border: 1px solid #1f1f23;
            border-radius: 6px;
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            padding: 6px 10px;
        }
        entry:focus {
            border-color: #52525b;
            box-shadow: none;
        }
        list.sidebar-list {
            background-color: #040406;
            border-right: 1px solid #141416;
            padding: 10px 8px;
        }
        list.sidebar-list > row {
            background: transparent;
            padding: 0;
            margin: 3px 0;
            border-radius: 6px;
            border: 1px solid transparent;
            outline: none;
            box-shadow: none;
            transition: all 120ms ease;
        }
        list.sidebar-list > row:hover {
            background-color: #101014;
            border-color: #1a1a20;
        }
        list.sidebar-list > row:selected {
            background-color: #18181c;
            border: 1px solid #27272e;
            border-left: 3px solid #ffffff;
            outline: none;
            box-shadow: none;
        }
        list.sidebar-list > row:focus {
            outline: none;
            box-shadow: none;
        }
        .sidebar-item {
            padding: 10px 14px;
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            font-weight: 500;
            color: #71717a;
            transition: color 120ms ease;
        }
        list.sidebar-list > row:hover .sidebar-item {
            color: #e4e4e7;
        }
        list.sidebar-list > row:selected .sidebar-item {
            color: #ffffff;
            font-weight: 700;
        }
        preferencesgroup > box {
            background-color: transparent;
        }
        list.boxed-list {
            background-color: #09090c;
            border: 1px solid #18181c;
            border-radius: 8px;
            box-shadow: none;
        }
        list.boxed-list > row {
            border-bottom: 1px solid #141416;
            padding: 10px 14px;
            background: transparent;
        }
        list.boxed-list > row:last-child {
            border-bottom: none;
        }
        list.boxed-list > row:hover {
            background-color: #0d0d10;
        }
        list.boxed-list > row:focus, list.boxed-list > row:selected {
            background-color: transparent;
            outline: none;
            box-shadow: none;
        }
        preferencesgroup .title {
            font-family: "JetBrains Mono", monospace;
            font-size: 13px;
            font-weight: 700;
            color: #ffffff;
            letter-spacing: 0.5px;
            margin-bottom: 4px;
        }
        preferencesgroup .subtitle {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            color: #71717a;
            margin-bottom: 8px;
        }
        row .title {
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            font-weight: 600;
            color: #ffffff;
        }
        row .subtitle {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            color: #71717a;
        }
        scale trough {
            background-color: #18181b;
            border-radius: 3px;
            min-height: 4px;
        }
        scale highlight {
            background-color: #ffffff;
            border-radius: 3px;
        }
        scale slider {
            background-color: #ffffff;
            border-radius: 50%;
            min-width: 16px;
            min-height: 16px;
            margin: -6px;
            box-shadow: 0 1px 4px rgba(0,0,0,0.6);
            border: none;
        }
        scale slider:hover {
            background-color: #e4e4e7;
        }
        switch {
            background-color: #18181b;
            border: 1px solid #27272a;
            border-radius: 12px;
        }
        switch:checked {
            background-color: #ffffff;
            border-color: #ffffff;
        }
        switch slider {
            background-color: #71717a;
            border-radius: 10px;
            border: none;
            min-width: 14px;
            min-height: 14px;
        }
        switch:checked slider {
            background-color: #000000;
        }
        button.swatch-trigger {
            background: #09090b;
            border: 1px solid #27272a;
            border-radius: 6px;
            padding: 3px 6px;
        }
        button.swatch-trigger:hover {
            border-color: #52525b;
            background: #141416;
        }
        popover {
            background-color: #09090c;
            border: 1px solid #27272e;
            border-radius: 8px;
            box-shadow: 0 8px 24px rgba(0,0,0,0.8);
        }
        .popover-heading {
            font-family: "JetBrains Mono", monospace;
            font-size: 10px;
            font-weight: 700;
            color: #71717a;
            letter-spacing: 0.8px;
        }
        .dim-label {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            color: #71717a;
        }
        .apply-btn {
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            font-weight: 700;
            padding: 8px 20px;
            background-color: #ffffff;
            color: #000000;
            border-radius: 6px;
            border: none;
            box-shadow: 0 2px 8px rgba(255,255,255,0.1);
        }
        .apply-btn:hover {
            background-color: #e4e4e7;
        }
        button.suggested-action {
            background-color: #18181c;
            border: 1px solid #27272e;
            color: #ffffff;
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            font-weight: 600;
            padding: 6px 14px;
            border-radius: 6px;
        }
        button.suggested-action:hover {
            background-color: #27272e;
            border-color: #3f3f46;
        }
        scrollbar {
            background-color: transparent;
        }
        scrollbar slider {
            background-color: #27272a;
            border-radius: 4px;
            min-width: 4px;
        }
        scrollbar slider:hover {
            background-color: #52525b;
        }
        .badge {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            font-weight: 700;
            padding: 3px 8px;
            border-radius: 4px;
            background-color: #121215;
            border: 1px solid #27272a;
            color: #a1a1aa;
        }
        .badge-uptodate {
            color: #4ade80;
            border-color: #166534;
            background-color: #052e16;
        }
        .badge-update-avail {
            color: #ffffff;
            border-color: #22c55e;
            background-color: #15803d;
        }
        .badge-checking {
            color: #38bdf8;
            border-color: #0369a1;
            background-color: #082f49;
        }
        .badge-warning {
            color: #fbbf24;
            border-color: #854d0e;
            background-color: #451a03;
        }
        .upgrade-btn {
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            font-weight: 700;
            padding: 8px 18px;
            background-color: #ffffff;
            color: #000000;
            border-radius: 6px;
            border: none;
            transition: all 120ms ease;
        }
        .upgrade-btn:hover {
            background-color: #e4e4e7;
        }
        .upgrade-btn:disabled {
            background-color: #27272a;
            color: #71717a;
        }
        .log-box {
            font-family: "JetBrains Mono", monospace;
            font-size: 11px;
            background-color: #040406;
            color: #a1a1aa;
            border: 1px solid #18181c;
            border-radius: 6px;
            padding: 8px;
        }
        progressbar trough {
            background-color: #141416;
            border: 1px solid #27272a;
            border-radius: 4px;
            min-height: 8px;
        }
        progressbar progress {
            background-color: #ffffff;
            border-radius: 3px;
        }
        .hero-card {
            background-color: #060608;
            border: 1px solid #1c1c24;
            border-radius: 8px;
            padding: 18px 22px;
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

    // Main layout box (Vertical: HeaderBar, Center, FooterBar)
    GtkWidget* root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // 1. Header Bar
    AdwHeaderBar* header = ADW_HEADER_BAR(adw_header_bar_new());

    // Title widget
    GtkWidget* title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* title_lbl = gtk_label_new("MONOLITH CONTROL CENTER");
    gtk_widget_add_css_class(title_lbl, "heading");
    gtk_box_append(GTK_BOX(title_box), title_lbl);
    adw_header_bar_set_title_widget(header, title_box);

    // Search Entry on Header Bar
    g_app.search_entry = gtk_search_entry_new();
    gtk_widget_set_size_request(g_app.search_entry, 280, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(g_app.search_entry), "Search settings, colors, physics...");
    adw_header_bar_pack_end(header, g_app.search_entry);

    gtk_box_append(GTK_BOX(root_box), GTK_WIDGET(header));

    // Toast overlay
    g_app.toast_overlay = adw_toast_overlay_new();
    gtk_widget_set_vexpand(g_app.toast_overlay, TRUE);

    // Center layout: Horizontal Paned or Box (Left Sidebar + Right Stack)
    GtkWidget* center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    // Left Sidebar Navigation
    GtkWidget* sidebar_scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sidebar_scrolled, 240, -1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    g_app.sidebar_list = gtk_list_box_new();
    gtk_widget_add_css_class(g_app.sidebar_list, "sidebar-list");
    gtk_list_box_set_show_separators(GTK_LIST_BOX(g_app.sidebar_list), FALSE);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_app.sidebar_list), GTK_SELECTION_SINGLE);

    std::vector<std::pair<std::string, std::string>> categories = {
        {"Overview & Specs", "overview"},
        {"Appearance & Borders", "appearance"},
        {"Wallpaper & Grain", "grain"},
        {"Motion & Physics", "motion"},
        {"Constellation & RAM", "taskgraph"},
        {"Status Bar (Waybar)", "waybar"},
        {"Input & Keyboard", "input"},
        {"Display, Gaming & Power", "display"},
        {"Shortcuts & Spatial 2D", "shortcuts"},
        {"System Updates", "updates"},
        {"Architecture Info", "about"}
    };

    for (const auto& cat : categories) {
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* lbl = gtk_label_new(cat.first.c_str());
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(lbl, "sidebar-item");
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_append(GTK_LIST_BOX(g_app.sidebar_list), row);
    }
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scrolled), g_app.sidebar_list);
    gtk_box_append(GTK_BOX(center_box), sidebar_scrolled);

    // Right Content Area (Stack)
    g_app.stack = gtk_stack_new();
    gtk_widget_set_hexpand(g_app.stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(g_app.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(g_app.stack), 150);

    auto add_page = [&](GtkWidget* content, const std::string& name) {
        GtkWidget* sc = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), content);
        gtk_stack_add_named(GTK_STACK(g_app.stack), sc, name.c_str());
    };

    add_page(build_overview_page(), "overview");
    add_page(build_appearance_page(), "appearance");
    add_page(build_grain_page(), "grain");
    add_page(build_motion_page(), "motion");
    add_page(build_taskgraph_page(), "taskgraph");
    add_page(build_waybar_page(), "waybar");
    add_page(build_input_page(), "input");
    add_page(build_display_page(), "display");
    add_page(build_shortcuts_page(), "shortcuts");
    add_page(build_updates_page(), "updates");
    add_page(build_about_page(), "about");

    // Connect sidebar selection to stack
    g_signal_connect(g_app.sidebar_list, "row-selected", G_CALLBACK((+[](GtkListBox*, GtkListBoxRow* row, gpointer) {
        if (!row) return;
        int idx = gtk_list_box_row_get_index(row);
        static const char* names[] = {"overview", "appearance", "grain", "motion", "taskgraph", "waybar", "input", "display", "shortcuts", "updates", "about"};
        if (idx >= 0 && idx < 11) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), names[idx]);
        }
    })), nullptr);

    // Select first row (Overview & Specs) by default
    GtkListBoxRow* first_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 0);
    if (first_row) gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), first_row);

    gtk_box_append(GTK_BOX(center_box), g_app.stack);
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(g_app.toast_overlay), center_box);
    gtk_box_append(GTK_BOX(root_box), g_app.toast_overlay);

    // Search Entry Filtering logic
    g_signal_connect(g_app.search_entry, "search-changed", G_CALLBACK((+[](GtkSearchEntry* entry, gpointer) {
        std::string query = gtk_editable_get_text(GTK_EDITABLE(entry));
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);
        if (query.empty()) return;

        if (query.find("overview") != std::string::npos || query.find("spec") != std::string::npos || query.find("system") != std::string::npos || query.find("cpu") != std::string::npos || query.find("ram") != std::string::npos || query.find("kernel") != std::string::npos || query.find("host") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "overview");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 0));
        } else if (query.find("color") != std::string::npos || query.find("border") != std::string::npos || query.find("gap") != std::string::npos || query.find("opacity") != std::string::npos || query.find("smart") != std::string::npos || query.find("dim") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "appearance");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 1));
        } else if (query.find("grain") != std::string::npos || query.find("gradient") != std::string::npos || query.find("wallpaper") != std::string::npos || query.find("noise") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "grain");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 2));
        } else if (query.find("bezier") != std::string::npos || query.find("anim") != std::string::npos || query.find("speed") != std::string::npos || query.find("curve") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "motion");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 3));
        } else if (query.find("graph") != std::string::npos || query.find("task") != std::string::npos || query.find("physics") != std::string::npos || query.find("spring") != std::string::npos || query.find("ram") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "taskgraph");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 4));
        } else if (query.find("waybar") != std::string::npos || query.find("bar") != std::string::npos || query.find("drawer") != std::string::npos || query.find("tray") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "waybar");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 5));
        } else if (query.find("touchpad") != std::string::npos || query.find("mouse") != std::string::npos || query.find("scroll") != std::string::npos || query.find("input") != std::string::npos || query.find("keyboard") != std::string::npos || query.find("repeat") != std::string::npos || query.find("delay") != std::string::npos || query.find("vim") != std::string::npos || query.find("paste") != std::string::npos || query.find("accel") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "input");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 6));
        } else if (query.find("display") != std::string::npos || query.find("night") != std::string::npos || query.find("scale") != std::string::npos || query.find("vrr") != std::string::npos || query.find("tearing") != std::string::npos || query.find("gaming") != std::string::npos || query.find("scanout") != std::string::npos || query.find("sleep") != std::string::npos || query.find("idle") != std::string::npos || query.find("battery") != std::string::npos || query.find("clip") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "display");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 7));
        } else if (query.find("key") != std::string::npos || query.find("shortcut") != std::string::npos || query.find("spatial") != std::string::npos || query.find("swipe") != std::string::npos || query.find("shelf") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "shortcuts");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 8));
        } else if (query.find("update") != std::string::npos || query.find("upgrade") != std::string::npos || query.find("version") != std::string::npos || query.find("release") != std::string::npos || query.find("git") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "updates");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 9));
        } else if (query.find("about") != std::string::npos || query.find("info") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "about");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 10));
        }
    })), nullptr);

    // 3. Bottom Footer Bar (Info on Left, "Apply Settings" on Right)
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(footer, 18);
    gtk_widget_set_margin_end(footer, 18);
    gtk_widget_set_margin_top(footer, 10);
    gtk_widget_set_margin_bottom(footer, 12);

    GtkWidget* foot_info = gtk_label_new("Monolith Shell — Native C++20 / Hyprland Architecture");
    gtk_widget_add_css_class(foot_info, "dim-label");
    gtk_widget_set_hexpand(foot_info, TRUE);
    gtk_widget_set_halign(foot_info, GTK_ALIGN_START);

    // Prominent "Apply Settings" button in the bottom right corner
    GtkWidget* btn_apply = gtk_button_new_with_label("Apply Settings");
    gtk_widget_add_css_class(btn_apply, "apply-btn");
    gtk_widget_set_halign(btn_apply, GTK_ALIGN_END);
    g_signal_connect(btn_apply, "clicked", G_CALLBACK(on_apply_clicked), nullptr);

    gtk_box_append(GTK_BOX(footer), foot_info);
    gtk_box_append(GTK_BOX(footer), btn_apply);
    gtk_box_append(GTK_BOX(root_box), footer);

    gtk_window_set_child(GTK_WINDOW(g_app.window), root_box);

    if (g_show_welcome) {
        // Welcome Dialog
        GtkWidget* win_w = adw_window_new();
        gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(win_w));
        gtk_window_set_title(GTK_WINDOW(win_w), "Welcome to Monolith");
        gtk_window_set_default_size(GTK_WINDOW(win_w), 540, 520);
        gtk_window_set_resizable(GTK_WINDOW(win_w), FALSE);

        GtkWidget* root_w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
        gtk_widget_set_margin_start(root_w, 28);
        gtk_widget_set_margin_end(root_w, 28);
        gtk_widget_set_margin_top(root_w, 32);
        gtk_widget_set_margin_bottom(root_w, 28);

        GtkWidget* title_w = gtk_label_new("Welcome to Monolith");
        gtk_widget_add_css_class(title_w, "title-1");
        gtk_widget_set_halign(title_w, GTK_ALIGN_CENTER);

        GtkWidget* sub_w = gtk_label_new("Ultra-Minimalist • Zero-Bloat • High Performance C++20 Environment");
        gtk_widget_add_css_class(sub_w, "dim-label");
        gtk_widget_set_halign(sub_w, GTK_ALIGN_CENTER);

        gtk_box_append(GTK_BOX(root_w), title_w);
        gtk_box_append(GTK_BOX(root_w), sub_w);

        AdwPreferencesGroup* g_feat = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        gtk_widget_set_margin_top(GTK_WIDGET(g_feat), 8);

        auto add_feat = [&](const char* heading, const char* desc) {
            AdwActionRow* r = ADW_ACTION_ROW(adw_action_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), heading);
            adw_action_row_set_subtitle(r, desc);
            adw_preferences_group_add(g_feat, GTK_WIDGET(r));
        };

        add_feat("OLED Brutalist Interface", "Pure #000000 black, hardware Bezier motion, and algorithmic grain.");
        add_feat("Spatial 2D Tabs & Shelf", "3-finger swipe up for Spotify, swipe down for the Extra Window Shelf.");
        add_feat("Native C++20 Suite", "Control Center (Super+I), Constellation Desktop, Edge Drawer (Super+Z).");

        gtk_box_append(GTK_BOX(root_w), GTK_WIDGET(g_feat));

        GtkWidget* btn_box_w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_top(btn_box_w, 12);

        GtkWidget* btn_go = gtk_button_new_with_label("Get Started");
        gtk_widget_add_css_class(btn_go, "upgrade-btn");
        gtk_widget_set_size_request(btn_go, -1, 44);

        g_signal_connect(btn_go, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            GtkWidget* w = GTK_WIDGET(data);
            std::string flag = config_dir() + "/.first_run_done";
            std::ofstream f(flag);
            if (f.is_open()) f << "first_run_completed=true\n";
            gtk_window_destroy(GTK_WINDOW(w));
            if (g_app.window) {
                gtk_window_present(GTK_WINDOW(g_app.window));
            }
        }), win_w);

        GtkWidget* hint = gtk_label_new("Click Get Started to explore Monolith Control Center");
        gtk_widget_add_css_class(hint, "dim-label");
        gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);

        gtk_box_append(GTK_BOX(btn_box_w), btn_go);
        gtk_box_append(GTK_BOX(btn_box_w), hint);
        gtk_box_append(GTK_BOX(root_w), btn_box_w);

        adw_window_set_content(ADW_WINDOW(win_w), root_w);
        gtk_window_present(GTK_WINDOW(win_w));
    } else {
        gtk_window_present(GTK_WINDOW(g_app.window));
    }
}

int main(int argc, char* argv[]) {
    adw_init();

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--welcome") {
            g_show_welcome = true;
        }
    }
    std::string first_run_file = config_dir() + "/.first_run_done";
    if (!g_show_welcome && !std::filesystem::exists(first_run_file)) {
        g_show_welcome = true;
    }

    GtkApplication* app = gtk_application_new("org.monolith.settings", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
