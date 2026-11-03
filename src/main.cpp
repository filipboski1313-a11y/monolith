#include <adwaita.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>
#include <algorithm>

#include "settings.hpp"
#include "bezier_widget.hpp"
#include "grain_generator.hpp"

using namespace Monolith;

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
    GtkAdjustment* adj_grain_intensity = nullptr;
    GtkAdjustment* adj_grain_scale = nullptr;
    GtkWidget* entry_grain_c1 = nullptr;
    GtkWidget* entry_grain_c2 = nullptr;
    GtkWidget* entry_grain_c3 = nullptr;
    GtkWidget* entry_grain_tint = nullptr;
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

static AdwEntryRow* make_entry_row(
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
    adw_preferences_group_set_title(g_borders, "Window Borders & Architecture");
    adw_preferences_group_set_description(g_borders, "Crisp geometry, dual-color gradient borders, and gaps.");

    make_entry_row(g_borders, "Active Border Color 1 (Hex)", g_app.s.geom.active_border_1, [](const std::string& s) {
        g_app.s.geom.active_border_1 = s;
    });
    make_entry_row(g_borders, "Active Border Color 2 (Hex)", g_app.s.geom.active_border_2, [](const std::string& s) {
        g_app.s.geom.active_border_2 = s;
    });
    make_slider_row(g_borders, "Border Gradient Angle", "Rotation angle of the dual-color gradient", 0.0, 360.0, 15.0, g_app.s.geom.border_angle, nullptr, [](double v) {
        g_app.s.geom.border_angle = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + "°"; });

    make_entry_row(g_borders, "Inactive Border Color (Hex)", g_app.s.geom.inactive_border, [](const std::string& s) {
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
    adw_preferences_group_set_title(g_shd, "Opacity & GPU Compositing");
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
    adw_preferences_group_set_title(g_wb_col, "Status Bar Colors (Waybar)");
    adw_preferences_group_set_description(g_wb_col, "Individual color overrides for the top bar and widgets.");

    make_entry_row(g_wb_col, "Bar Background Color (Hex)", g_app.s.waybar.bg_color, [](const std::string& s) {
        g_app.s.waybar.bg_color = s;
    });
    make_entry_row(g_wb_col, "Bar Border Color (Hex)", g_app.s.waybar.border_color, [](const std::string& s) {
        g_app.s.waybar.border_color = s;
    });
    make_entry_row(g_wb_col, "Bar Text & Icon Color (Hex)", g_app.s.waybar.text_color, [](const std::string& s) {
        g_app.s.waybar.text_color = s;
    });
    make_entry_row(g_wb_col, "Active Workspace Accent (Hex)", g_app.s.waybar.active_ws_bg, [](const std::string& s) {
        g_app.s.waybar.active_ws_bg = s;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_wb_col));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 2: GRADIENT GRAIN BACKGROUND
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_grain_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_grad = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_grad, "Mathematical Gradient & Grain Architecture");
    adw_preferences_group_set_description(g_grad, "Zero static wallpapers. Seamless mathematical gradients with tactile film grain woven directly into the field.");

    // Gradient Presets
    std::vector<std::string> presets = {
        "OLED Monolith (Pure Black & Charcoal)",
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

    make_combo_row(g_grad, "Gradient & Grain Preset", "Hand-crafted brutalist and chromatic templates", presets, initial_preset_idx, [](int idx, const std::string& name) {
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
                g_app.s.grain.color1 = "#030712"; // Bottom
                g_app.s.grain.color2 = "#064e3b"; // Top-left
                g_app.s.grain.color3 = "#312e81"; // Top-right lower
                g_app.s.grain.tint = "#a7f3d0";
                break;
            case 5: // Solar Dusk (3-Color)
                g_app.s.grain.gradient_type = "mesh_3point";
                g_app.s.grain.color1 = "#050505"; // Bottom
                g_app.s.grain.color2 = "#450a0a"; // Top-left
                g_app.s.grain.color3 = "#78350f"; // Top-right lower
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
        if (g_app.entry_grain_c1) gtk_editable_set_text(GTK_EDITABLE(g_app.entry_grain_c1), g_app.s.grain.color1.c_str());
        if (g_app.entry_grain_c2) gtk_editable_set_text(GTK_EDITABLE(g_app.entry_grain_c2), g_app.s.grain.color2.c_str());
        if (g_app.entry_grain_c3) gtk_editable_set_text(GTK_EDITABLE(g_app.entry_grain_c3), g_app.s.grain.color3.c_str());
        if (g_app.entry_grain_tint) gtk_editable_set_text(GTK_EDITABLE(g_app.entry_grain_tint), g_app.s.grain.tint.c_str());
    });

    // Gradient Direction / Geometry
    std::vector<std::string> grad_types = {
        "Solid OLED Field (No Gradient)",
        "2-Color Vertical (Top-to-Bottom)",
        "2-Color Horizontal (Left-to-Right)",
        "2-Color Diagonal Down (Top-Left to Bottom-Right)",
        "2-Color Diagonal Up (Bottom-Left to Top-Right)",
        "3-Color Mesh Gradient (Bottom Center, Top-Left, Top-Right Lower)"
    };
    int cur_gt = 0;
    if (g_app.s.grain.gradient_type == "vertical") cur_gt = 1;
    else if (g_app.s.grain.gradient_type == "horizontal") cur_gt = 2;
    else if (g_app.s.grain.gradient_type == "diagonal_down") cur_gt = 3;
    else if (g_app.s.grain.gradient_type == "diagonal_up") cur_gt = 4;
    else if (g_app.s.grain.gradient_type == "mesh_3point") cur_gt = 5;

    make_combo_row(g_grad, "Gradient Geometry", "Direction and coordinate model for color transitions", grad_types, cur_gt, [](int idx, const std::string&) {
        const char* keys[] = {"none", "vertical", "horizontal", "diagonal_down", "diagonal_up", "mesh_3point"};
        g_app.s.grain.gradient_type = keys[std::clamp(idx, 0, 5)];
    });

    // Color Pickers
    g_app.entry_grain_c1 = GTK_WIDGET(make_entry_row(g_grad, "Color 1: Base / Bottom (Hex)", g_app.s.grain.color1, [](const std::string& s) {
        g_app.s.grain.color1 = s;
    }));

    g_app.entry_grain_c2 = GTK_WIDGET(make_entry_row(g_grad, "Color 2: Top / Left (Hex)", g_app.s.grain.color2, [](const std::string& s) {
        g_app.s.grain.color2 = s;
    }));

    g_app.entry_grain_c3 = GTK_WIDGET(make_entry_row(g_grad, "Color 3: Top-Right Lower (Hex)", g_app.s.grain.color3, [](const std::string& s) {
        g_app.s.grain.color3 = s;
    }));

    g_app.entry_grain_tint = GTK_WIDGET(make_entry_row(g_grad, "Grain Noise Particle Tint (Hex)", g_app.s.grain.tint, [](const std::string& s) {
        g_app.s.grain.tint = s;
    }));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_grad));

    // Group: Mathematical Grain Physics
    AdwPreferencesGroup* g_math = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_math, "Grain Physics & Math Algorithm");
    adw_preferences_group_set_description(g_math, "Mathematical equation governing pixel scattering across the canvas.");

    std::vector<std::string> algos = {
        "Gaussian Box-Muller (Tactile Film Grain)",
        "Harmonic Wave Modulation (Sine & Cosine Fields)",
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
    });

    make_slider_row(g_math, "Grain Noise Intensity", "Standard deviation of the random distribution", 0.0, 40.0, 1.0, g_app.s.grain.intensity, &g_app.adj_grain_intensity, [](double v) {
        g_app.s.grain.intensity = v;
    });

    make_slider_row(g_math, "Pixel Grain Scale", "Pixel block multiplier for analog aesthetic", 1.0, 4.0, 1.0, g_app.s.grain.scale, &g_app.adj_grain_scale, [](double v) {
        g_app.s.grain.scale = static_cast<int>(v);
    }, [](double v) { return std::to_string(static_cast<int>(v)) + "x"; });

    make_switch_row(g_math, "Architectural Center Crosshair", "Subtle coordinate alignment marker at screen center", g_app.s.grain.crosshair, [](bool b) {
        g_app.s.grain.crosshair = b;
    });

    // Render Action Button
    AdwActionRow* render_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(render_row), "Render Wallpaper Now");
    adw_action_row_set_subtitle(render_row, "Computes full 1920x1200 gradient grain and updates Hyprpaper in RAM");

    GtkWidget* render_btn = gtk_button_new_with_label("Render In RAM");
    gtk_widget_add_css_class(render_btn, "suggested-action");
    gtk_widget_set_valign(render_btn, GTK_ALIGN_CENTER);
    g_signal_connect(render_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        g_app.s.render_wallpaper();
        show_toast("Gradient grain wallpaper rendered and applied!");
    }), nullptr);

    adw_action_row_add_suffix(render_row, render_btn);
    adw_preferences_group_add(g_math, GTK_WIDGET(render_row));

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_math));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 3: WINDOW MOTION & BÉZIER CURVES
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_motion_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_curv = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_curv, "Interactive Cubic Bézier Curve Engine");
    adw_preferences_group_set_description(g_curv, "Custom Cairo widget modeling micro-acceleration physics in real time.");

    // Bézier widget
    g_app.bezier_data.p1x = g_app.s.anim.p1x;
    g_app.bezier_data.p1y = g_app.s.anim.p1y;
    g_app.bezier_data.p2x = g_app.s.anim.p2x;
    g_app.bezier_data.p2y = g_app.s.anim.p2y;
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
        if (idx == 0) { // Micro Snappy
            g_app.s.anim.p1x = 0.05; g_app.s.anim.p1y = 0.90; g_app.s.anim.p2x = 0.18; g_app.s.anim.p2y = 1.00;
        } else if (idx == 1) { // Instant Minimal
            g_app.s.anim.p1x = 0.16; g_app.s.anim.p1y = 1.00; g_app.s.anim.p2x = 0.30; g_app.s.anim.p2y = 1.00;
        } else if (idx == 2) { // Gentle Arc
            g_app.s.anim.p1x = 0.25; g_app.s.anim.p1y = 0.10; g_app.s.anim.p2x = 0.25; g_app.s.anim.p2y = 1.00;
        } else if (idx == 3) { // Dynamic Smooth
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
    adw_preferences_group_set_title(g_dur, "Animation Speeds & Transitions");
    adw_preferences_group_set_description(g_dur, "Durations in tenths of a second (1.4 = 140ms).");

    make_slider_row(g_dur, "Window Spawn Speed", "Opening speed when new process launches", 0.5, 4.0, 0.1, g_app.s.anim.windows_in, &g_app.adj_win_in, [](double v) {
        g_app.s.anim.windows_in = v;
    });
    make_slider_row(g_dur, "Window Close Speed", "Dismissal speed when window is terminated", 0.5, 4.0, 0.1, g_app.s.anim.windows_out, &g_app.adj_win_out, [](double v) {
        g_app.s.anim.windows_out = v;
    });
    make_slider_row(g_dur, "Window Move & Resize Speed", "Reflow speed during layout recalculations", 0.5, 4.0, 0.1, g_app.s.anim.windows_move, &g_app.adj_win_move, [](double v) {
        g_app.s.anim.windows_move = v;
    });
    make_slider_row(g_dur, "Workspace Switch Speed", "Horizontal desktop panning duration", 0.5, 4.0, 0.1, g_app.s.anim.workspaces, &g_app.adj_workspaces, [](double v) {
        g_app.s.anim.workspaces = v;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_dur));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 4: TASK GRAPH & OBSIDIAN PHYSICS
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_taskgraph_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_tg = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_tg, "Desktop Task Graph Physics");
    adw_preferences_group_set_description(g_tg, "Interactive force-directed graph engine on the background layer.");

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
    make_switch_row(g_tg, "Zero-Overhead Auto Freeze", "Freezes rendering and physics entirely when windows cover desktop", g_app.s.taskgraph.auto_freeze, [](bool b) {
        g_app.s.taskgraph.auto_freeze = b;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_tg));
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
    adw_preferences_group_set_title(g_wb, "Status Bar Architecture & Layout");
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
        "Always Expanded & Pinned (Convenience)"
    };
    make_combo_row(g_wb, "Tray Drawer Behavior", "Choose between collapsible minimalism or permanently visible icons", drawer_modes, (g_app.s.waybar.drawer_enabled ? 0 : 1), [](int idx, const std::string&) {
        g_app.s.waybar.drawer_enabled = (idx == 0);
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_wb));

    // Module Toggles
    AdwPreferencesGroup* g_mods = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_mods, "Module Visibility");
    adw_preferences_group_set_description(g_mods, "Toggle active modules on the status bar.");

    make_switch_row(g_mods, "󰕾 Audio & Volume", "WirePlumber audio controls", g_app.s.waybar.show_audio, [](bool b) { g_app.s.waybar.show_audio = b; });
    make_switch_row(g_mods, "󰖩 Network & Wi-Fi", "NetworkManager connection status", g_app.s.waybar.show_network, [](bool b) { g_app.s.waybar.show_network = b; });
    make_switch_row(g_mods, "󰂯 Bluetooth", "Bluetooth controller and device manager", g_app.s.waybar.show_bluetooth, [](bool b) { g_app.s.waybar.show_bluetooth = b; });
    make_switch_row(g_mods, "󰍛 CPU Usage", "Live processor load percentage", g_app.s.waybar.show_cpu, [](bool b) { g_app.s.waybar.show_cpu = b; });
    make_switch_row(g_mods, "󰘚 RAM Usage", "Physical memory consumption", g_app.s.waybar.show_memory, [](bool b) { g_app.s.waybar.show_memory = b; });
    make_switch_row(g_mods, "󰃞 Backlight Brightness", "Display brightness indicator", g_app.s.waybar.show_backlight, [](bool b) { g_app.s.waybar.show_backlight = b; });
    make_switch_row(g_mods, "󰂄 Battery Status", "Laptop battery percentage and charge state", g_app.s.waybar.show_battery, [](bool b) { g_app.s.waybar.show_battery = b; });
    make_switch_row(g_mods, "󰃝 Night Light Filter", "Quick toggle for Hyprsunset blue light filter", g_app.s.waybar.show_nightlight, [](bool b) { g_app.s.waybar.show_nightlight = b; });
    make_switch_row(g_mods, "󰂚 Notifications", "SwayNC notification center toggle", g_app.s.waybar.show_notifications, [](bool b) { g_app.s.waybar.show_notifications = b; });
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_mods));

    // Bar Colors & Visual Styling
    AdwPreferencesGroup* g_wb_colors = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_wb_colors, "Status Bar Colors & Visual Palette");
    adw_preferences_group_set_description(g_wb_colors, "Full color control over bar background, borders, module icons, and active workspaces.");

    make_entry_row(g_wb_colors, "Bar Background Color (Hex)", g_app.s.waybar.bg_color, [](const std::string& s) {
        g_app.s.waybar.bg_color = s;
    });
    make_entry_row(g_wb_colors, "Bar Border Color (Hex)", g_app.s.waybar.border_color, [](const std::string& s) {
        g_app.s.waybar.border_color = s;
    });
    make_entry_row(g_wb_colors, "Module Icons & Status Text Color (Hex)", g_app.s.waybar.text_color, [](const std::string& s) {
        g_app.s.waybar.text_color = s;
    });
    make_entry_row(g_wb_colors, "Active Workspace Button Background (Hex)", g_app.s.waybar.active_ws_bg, [](const std::string& s) {
        g_app.s.waybar.active_ws_bg = s;
    });
    make_entry_row(g_wb_colors, "Active Workspace Text / Icon Color (Hex)", g_app.s.waybar.active_ws_text, [](const std::string& s) {
        g_app.s.waybar.active_ws_text = s;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_wb_colors));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 6: INPUT & TOUCHPAD
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_input_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_inp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_inp, "Touchpad & Pointer Configuration");
    adw_preferences_group_set_description(g_inp, "Direct libinput gestures and acceleration profiles.");

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
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 7: DISPLAY & NIGHT LIGHT
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_display_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_dsp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_dsp, "Display Output & Calibration");
    adw_preferences_group_set_description(g_dsp, ("Output: " + g_app.s.disp.monitor_name + " • Mode: " + g_app.s.disp.mode).c_str());

    make_slider_row(g_dsp, "Display Fractional Scale", "Pixel scaling factor for high-DPI panels", 1.0, 2.5, 0.05, g_app.s.disp.scale, nullptr, [](double v) {
        g_app.s.disp.scale = v;
    }, [](double v) { return std::to_string(v).substr(0, 4) + "x"; });

    std::vector<std::string> vrr_opts = {"0 - Disabled (Fixed Refresh Rate)", "1 - Variable Refresh Rate (FreeSync/Adaptive)", "2 - VRR on Fullscreen Games Only"};
    make_combo_row(g_dsp, "Adaptive Sync / VRR", "Variable refresh rate for tear-free gaming", vrr_opts, g_app.s.disp.vrr, [](int idx, const std::string&) {
        g_app.s.disp.vrr = idx;
    });

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_dsp));

    AdwPreferencesGroup* g_nl = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_nl, "Night Light & Color Temperature (Hyprsunset)");
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
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// PAGE 8: ABOUT MONOLITH
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_about_page() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);

    AdwPreferencesGroup* g_abt = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g_abt, "Monolith Architecture");
    adw_preferences_group_set_description(g_abt, "Ultra-Minimalist • Zero-Bloat • High Performance Native C++20 Shell for Hyprland.");

    auto add_info = [&](const std::string& title, const std::string& val) {
        AdwActionRow* r = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(r), title.c_str());
        GtkWidget* lbl = gtk_label_new(val.c_str());
        gtk_widget_add_css_class(lbl, "dim-label");
        adw_action_row_add_suffix(r, lbl);
        adw_preferences_group_add(g_abt, GTK_WIDGET(r));
    };

    add_info("Compositor", "Hyprland (Wayland)");
    add_info("Native Tool Suite", "C++20 (GTK4 + gtk4-layer-shell + Cairo)");
    add_info("Memory Preloader", "Linux Pagecache Posix Fadvise (0ms cold start)");
    add_info("Mathematical Engine", "Box-Muller Normal & Mesh Gradients");
    add_info("License", "MIT Open Source");

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(g_abt));
    return box;
}

// ──────────────────────────────────────────────────────────────────────────────
// APPLY SETTINGS ACTION
// ──────────────────────────────────────────────────────────────────────────────
static void on_apply_clicked(GtkButton*, gpointer) {
    g_app.s.save();
    show_toast("✓ All settings and visual parameters applied successfully!");
}

// ──────────────────────────────────────────────────────────────────────────────
// MAIN APPLICATION INITIALIZATION
// ──────────────────────────────────────────────────────────────────────────────
static void on_app_activate(GApplication* app, gpointer) {
    g_app.s.detect_monitor();
    g_app.s.load();

    // Create main window
    g_app.window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(g_app.window), "Monolith Settings & Control Center");
    gtk_window_set_default_size(GTK_WINDOW(g_app.window), 980, 720);

    // Apply custom styling
    const char* css = R"(
        window {
            background-color: #000000;
        }
        .sidebar-list {
            background-color: #09090b;
            border-right: 1px solid #18181b;
        }
        .sidebar-item {
            padding: 10px 14px;
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            font-weight: 600;
            color: #a1a1aa;
            border-radius: 4px;
            margin: 2px 6px;
        }
        .sidebar-item:hover {
            background-color: #141416;
            color: #ffffff;
        }
        .sidebar-item:selected {
            background-color: #18181b;
            color: #ffffff;
            border-left: 2px solid #ffffff;
        }
        .apply-btn {
            font-family: "JetBrains Mono", monospace;
            font-size: 12px;
            font-weight: 700;
            padding: 8px 18px;
            background-color: #ffffff;
            color: #000000;
            border-radius: 4px;
        }
        .apply-btn:hover {
            background-color: #e4e4e7;
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
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_app.sidebar_list), GTK_SELECTION_SINGLE);

    std::vector<std::pair<std::string, std::string>> categories = {
        {"🎨 Appearance & Colors", "appearance"},
        {"🌌 Gradient & Grain", "grain"},
        {"⚡ Motion & Bézier", "motion"},
        {"📊 Desktop Task Graph", "taskgraph"},
        {"🎛️ Waybar & Modules", "waybar"},
        {"⌨️ Input & Touchpad", "input"},
        {"🌙 Display & Night Light", "display"},
        {"ℹ️ About Monolith", "about"}
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

    add_page(build_appearance_page(), "appearance");
    add_page(build_grain_page(), "grain");
    add_page(build_motion_page(), "motion");
    add_page(build_taskgraph_page(), "taskgraph");
    add_page(build_waybar_page(), "waybar");
    add_page(build_input_page(), "input");
    add_page(build_display_page(), "display");
    add_page(build_about_page(), "about");

    // Connect sidebar selection to stack
    g_signal_connect(g_app.sidebar_list, "row-selected", G_CALLBACK((+[](GtkListBox*, GtkListBoxRow* row, gpointer) {
        if (!row) return;
        int idx = gtk_list_box_row_get_index(row);
        static const char* names[] = {"appearance", "grain", "motion", "taskgraph", "waybar", "input", "display", "about"};
        if (idx >= 0 && idx < 8) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), names[idx]);
        }
    })), nullptr);

    // Select first row by default
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

        if (query.find("color") != std::string::npos || query.find("border") != std::string::npos || query.find("gap") != std::string::npos || query.find("opacity") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "appearance");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 0));
        } else if (query.find("grain") != std::string::npos || query.find("gradient") != std::string::npos || query.find("wallpaper") != std::string::npos || query.find("noise") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "grain");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 1));
        } else if (query.find("bezier") != std::string::npos || query.find("anim") != std::string::npos || query.find("speed") != std::string::npos || query.find("curve") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "motion");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 2));
        } else if (query.find("graph") != std::string::npos || query.find("task") != std::string::npos || query.find("physics") != std::string::npos || query.find("spring") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "taskgraph");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 3));
        } else if (query.find("waybar") != std::string::npos || query.find("bar") != std::string::npos || query.find("drawer") != std::string::npos || query.find("tray") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "waybar");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 4));
        } else if (query.find("touchpad") != std::string::npos || query.find("mouse") != std::string::npos || query.find("scroll") != std::string::npos || query.find("input") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "input");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 5));
        } else if (query.find("display") != std::string::npos || query.find("night") != std::string::npos || query.find("scale") != std::string::npos || query.find("vrr") != std::string::npos) {
            gtk_stack_set_visible_child_name(GTK_STACK(g_app.stack), "display");
            gtk_list_box_select_row(GTK_LIST_BOX(g_app.sidebar_list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_app.sidebar_list), 6));
        }
    })), nullptr);

    // 3. Bottom Footer Bar (Info on Left, "Apply Settings" on Right)
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(footer, 18);
    gtk_widget_set_margin_end(footer, 18);
    gtk_widget_set_margin_top(footer, 10);
    gtk_widget_set_margin_bottom(footer, 12);

    GtkWidget* foot_info = gtk_label_new("⚡ Monolith Native Engine • Mathematical C++20 Environment");
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
    gtk_window_present(GTK_WINDOW(g_app.window));
}

int main(int argc, char* argv[]) {
    adw_init();
    GtkApplication* app = gtk_application_new("org.monolith.settings", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
