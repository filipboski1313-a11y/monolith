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
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

// ══════════════════════════════════════════════════════════════════════════════
// Monolith Native Toolbox
// Ultra-lightweight native C++20 tool suite:
//   - Full-edge reactive hover trigger with dynamic cursor-following indicator line
//   - Left & Right edge support
//   - Compact brutalist square window (OLED #000000, 1px #27272a)
//   - Tabs:
//       1. 󰔛 Stopwatch (Millisecond stopwatch + lap recording)
//       2. 󰃬 Calculator (Standard & keyboard-aware math engine)
//       3. 󰽉 Canvas (Smooth Cairo drawing canvas + clipboard copy)
//       4. 󰈙 Notes (Persistent auto-saving scratchpad)
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
        bool hovered = false;
        double cursor_y = 200.0;
        GtkWidget* da = nullptr;
    };
    TriggerState left_trig;
    TriggerState right_trig;

    GtkWidget* popup_win = nullptr;
    GtkWidget* stack = nullptr;
    bool popup_visible = false;
    std::string current_edge = "left";

    // Stopwatch state
    bool sw_running = false;
    std::chrono::steady_clock::time_point sw_start_time;
    std::chrono::milliseconds sw_elapsed{0};
    guint sw_timer_id = 0;
    GtkWidget* sw_label = nullptr;
    GtkWidget* sw_start_btn = nullptr;
    GtkWidget* sw_lap_box = nullptr;
    int sw_lap_count = 0;

    // Calculator state
    std::string calc_expr = "";
    std::string calc_display = "0";
    GtkWidget* calc_expr_label = nullptr;
    GtkWidget* calc_res_label = nullptr;

    // Drawing state
    GtkWidget* draw_area = nullptr;
    std::vector<Stroke> strokes;
    Stroke current_stroke;
    bool is_drawing = false;
    double cur_r = 1.0, cur_g = 1.0, cur_b = 1.0;
    double cur_brush_width = 3.0;

    // Notes state
    GtkTextBuffer* notes_buffer = nullptr;
    std::string notes_path;
} g_state;

static void toggle_popup(const std::string& edge);
static void hide_popup();

// ──────────────────────────────────────────────────────────────────────────────
// STOPWATCH
// ──────────────────────────────────────────────────────────────────────────────
static std::string format_time(long long ms) {
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

static gboolean sw_tick(gpointer) {
    if (!g_state.sw_running) return G_SOURCE_REMOVE;
    auto now = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_state.sw_start_time);
    auto total = g_state.sw_elapsed + diff;
    gtk_label_set_text(GTK_LABEL(g_state.sw_label), format_time(total.count()).c_str());
    return G_SOURCE_CONTINUE;
}

static void sw_toggle_start(GtkButton* btn, gpointer) {
    if (!g_state.sw_running) {
        g_state.sw_running = true;
        g_state.sw_start_time = std::chrono::steady_clock::now();
        gtk_button_set_label(btn, "PAUSE");
        gtk_widget_add_css_class(GTK_WIDGET(btn), "btn-pause");
        gtk_widget_remove_css_class(GTK_WIDGET(btn), "btn-start");
        g_state.sw_timer_id = g_timeout_add(16, sw_tick, nullptr);
    } else {
        g_state.sw_running = false;
        auto now = std::chrono::steady_clock::now();
        g_state.sw_elapsed += std::chrono::duration_cast<std::chrono::milliseconds>(now - g_state.sw_start_time);
        gtk_button_set_label(btn, "START");
        gtk_widget_add_css_class(GTK_WIDGET(btn), "btn-start");
        gtk_widget_remove_css_class(GTK_WIDGET(btn), "btn-pause");
        if (g_state.sw_timer_id > 0) {
            g_source_remove(g_state.sw_timer_id);
            g_state.sw_timer_id = 0;
        }
    }
}

static void sw_reset(GtkButton*, gpointer) {
    g_state.sw_running = false;
    g_state.sw_elapsed = std::chrono::milliseconds{0};
    if (g_state.sw_timer_id > 0) {
        g_source_remove(g_state.sw_timer_id);
        g_state.sw_timer_id = 0;
    }
    gtk_label_set_text(GTK_LABEL(g_state.sw_label), "00:00.00");
    gtk_button_set_label(GTK_BUTTON(g_state.sw_start_btn), "START");
    gtk_widget_add_css_class(GTK_WIDGET(g_state.sw_start_btn), "btn-start");
    gtk_widget_remove_css_class(GTK_WIDGET(g_state.sw_start_btn), "btn-pause");

    GtkWidget* child = gtk_widget_get_first_child(g_state.sw_lap_box);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(g_state.sw_lap_box), child);
        child = next;
    }
    g_state.sw_lap_count = 0;
}

static void sw_lap(GtkButton*, gpointer) {
    long long total_ms = g_state.sw_elapsed.count();
    if (g_state.sw_running) {
        auto now = std::chrono::steady_clock::now();
        total_ms += std::chrono::duration_cast<std::chrono::milliseconds>(now - g_state.sw_start_time).count();
    }
    if (total_ms == 0) return;

    g_state.sw_lap_count++;
    std::string lap_str = "Lap " + std::to_string(g_state.sw_lap_count) + "   " + format_time(total_ms);
    GtkWidget* lbl = gtk_label_new(lap_str.c_str());
    gtk_widget_add_css_class(lbl, "lap-item");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_prepend(GTK_BOX(g_state.sw_lap_box), lbl);
}

// ──────────────────────────────────────────────────────────────────────────────
// CALCULATOR
// ──────────────────────────────────────────────────────────────────────────────
static double eval_simple(const std::string& expr, bool& ok) {
    ok = true;
    try {
        std::vector<double> numbers;
        std::vector<char> ops;
        std::string num_str = "";

        for (size_t i = 0; i < expr.length(); ++i) {
            char c = expr[i];
            if (isdigit(c) || c == '.') {
                num_str += c;
            } else if (c == '+' || c == '-' || c == '*' || c == '/') {
                if (!num_str.empty()) {
                    numbers.push_back(std::stod(num_str));
                    num_str = "";
                } else if (c == '-' && (i == 0 || expr[i-1] == '+' || expr[i-1] == '-' || expr[i-1] == '*' || expr[i-1] == '/')) {
                    num_str += c;
                    continue;
                }
                ops.push_back(c);
            }
        }
        if (!num_str.empty()) numbers.push_back(std::stod(num_str));

        if (numbers.empty()) { ok = false; return 0.0; }

        for (size_t i = 0; i < ops.size();) {
            if (ops[i] == '*' || ops[i] == '/') {
                double a = numbers[i];
                double b = numbers[i+1];
                double res = (ops[i] == '*') ? (a * b) : ((b != 0) ? (a / b) : 0);
                if (ops[i] == '/' && b == 0) { ok = false; return 0.0; }
                numbers[i] = res;
                numbers.erase(numbers.begin() + i + 1);
                ops.erase(ops.begin() + i);
            } else {
                i++;
            }
        }

        double total = numbers[0];
        for (size_t i = 0; i < ops.size(); ++i) {
            if (ops[i] == '+') total += numbers[i+1];
            else if (ops[i] == '-') total -= numbers[i+1];
        }
        return total;
    } catch (...) {
        ok = false;
        return 0.0;
    }
}

static void calc_press(const std::string& key) {
    if (key == "C") {
        g_state.calc_expr = "";
        g_state.calc_display = "0";
    } else if (key == "DEL") {
        if (!g_state.calc_expr.empty()) g_state.calc_expr.pop_back();
        g_state.calc_display = g_state.calc_expr.empty() ? "0" : g_state.calc_expr;
    } else if (key == "=") {
        if (!g_state.calc_expr.empty()) {
            bool ok = false;
            double res = eval_simple(g_state.calc_expr, ok);
            if (ok) {
                std::ostringstream ss;
                if (std::abs(res - std::round(res)) < 1e-9) {
                    ss << static_cast<long long>(res);
                } else {
                    ss << std::fixed << std::setprecision(4) << res;
                    std::string s = ss.str();
                    while (s.back() == '0') s.pop_back();
                    if (s.back() == '.') s.pop_back();
                    ss.str(s);
                }
                g_state.calc_display = ss.str();
                g_state.calc_expr = ss.str();
            } else {
                g_state.calc_display = "Error";
            }
        }
    } else if (key == "+/-") {
        if (!g_state.calc_display.empty() && g_state.calc_display != "0") {
            if (g_state.calc_display[0] == '-') g_state.calc_display = g_state.calc_display.substr(1);
            else g_state.calc_display = "-" + g_state.calc_display;
            g_state.calc_expr = g_state.calc_display;
        }
    } else {
        if (g_state.calc_display == "0" && key != "." && key != "+" && key != "-" && key != "*" && key != "/") {
            g_state.calc_expr = key;
        } else {
            g_state.calc_expr += key;
        }
        g_state.calc_display = g_state.calc_expr;
    }

    gtk_label_set_text(GTK_LABEL(g_state.calc_expr_label), g_state.calc_expr.c_str());
    gtk_label_set_text(GTK_LABEL(g_state.calc_res_label), g_state.calc_display.c_str());
}

// ──────────────────────────────────────────────────────────────────────────────
// DRAWING CANVAS
// ──────────────────────────────────────────────────────────────────────────────
static void draw_area_draw(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer) {
    cairo_set_source_rgb(cr, 0.04, 0.04, 0.04);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, 0.18, 0.18, 0.20, 0.4);
    for (int x = 20; x < width; x += 20) {
        for (int y = 20; y < height; y += 20) {
            cairo_arc(cr, x, y, 1.0, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    for (const auto& s : g_state.strokes) {
        if (s.points.size() < 2) continue;
        cairo_set_source_rgb(cr, s.r, s.g, s.b);
        cairo_set_line_width(cr, s.width);
        cairo_move_to(cr, s.points[0].x, s.points[0].y);
        for (size_t i = 1; i < s.points.size(); ++i) {
            cairo_line_to(cr, s.points[i].x, s.points[i].y);
        }
        cairo_stroke(cr);
    }

    if (g_state.is_drawing && g_state.current_stroke.points.size() >= 2) {
        cairo_set_source_rgb(cr, g_state.cur_r, g_state.cur_g, g_state.cur_b);
        cairo_set_line_width(cr, g_state.cur_brush_width);
        cairo_move_to(cr, g_state.current_stroke.points[0].x, g_state.current_stroke.points[0].y);
        for (size_t i = 1; i < g_state.current_stroke.points.size(); ++i) {
            cairo_line_to(cr, g_state.current_stroke.points[i].x, g_state.current_stroke.points[i].y);
        }
        cairo_stroke(cr);
    }
}

static void on_drag_begin(GtkGestureDrag*, double x, double y, gpointer) {
    g_state.is_drawing = true;
    g_state.current_stroke = Stroke{};
    g_state.current_stroke.r = g_state.cur_r;
    g_state.current_stroke.g = g_state.cur_g;
    g_state.current_stroke.b = g_state.cur_b;
    g_state.current_stroke.width = g_state.cur_brush_width;
    g_state.current_stroke.points.push_back({x, y});
    gtk_widget_queue_draw(g_state.draw_area);
}

static void on_drag_update(GtkGestureDrag*, double offset_x, double offset_y, gpointer data) {
    if (!g_state.is_drawing) return;
    auto* gesture = GTK_GESTURE_DRAG(data);
    double start_x = 0, start_y = 0;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
    g_state.current_stroke.points.push_back({start_x + offset_x, start_y + offset_y});
    gtk_widget_queue_draw(g_state.draw_area);
}

static void on_drag_end(GtkGestureDrag*, double, double, gpointer) {
    if (g_state.is_drawing) {
        g_state.strokes.push_back(g_state.current_stroke);
        g_state.is_drawing = false;
        gtk_widget_queue_draw(g_state.draw_area);
    }
}

static void draw_clear(GtkButton*, gpointer) {
    g_state.strokes.clear();
    gtk_widget_queue_draw(g_state.draw_area);
}

static void draw_copy_clip(GtkButton*, gpointer) {
    int w = gtk_widget_get_width(g_state.draw_area);
    int h = gtk_widget_get_height(g_state.draw_area);
    if (w <= 0 || h <= 0) return;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    draw_area_draw(nullptr, cr, w, h, nullptr);
    cairo_destroy(cr);

    std::string tmp_path = "/tmp/monolith_sketch.png";
    cairo_surface_write_to_png(surface, tmp_path.c_str());
    cairo_surface_destroy(surface);

    std::string cmd = "wl-copy -t image/png < " + tmp_path + " 2>/dev/null && notify-send -a Monolith -i emblem-shared -t 1500 'Canvas' 'Drawing copied to clipboard!' &";
    system(cmd.c_str());
}

// ──────────────────────────────────────────────────────────────────────────────
// NOTES SCRATCHPAD
// ──────────────────────────────────────────────────────────────────────────────
static void notes_save() {
    if (!g_state.notes_buffer) return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(g_state.notes_buffer, &start, &end);
    char* text = gtk_text_buffer_get_text(g_state.notes_buffer, &start, &end, FALSE);
    if (text) {
        std::ofstream f(g_state.notes_path);
        if (f.is_open()) f << text;
        g_free(text);
    }
}

static void notes_load() {
    if (!g_state.notes_buffer) return;
    if (fs::exists(g_state.notes_path)) {
        std::ifstream f(g_state.notes_path);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        gtk_text_buffer_set_text(g_state.notes_buffer, content.c_str(), -1);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// POPUP WINDOW
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* build_stopwatch_tab() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 14);
    gtk_widget_set_margin_end(box, 14);

    g_state.sw_label = gtk_label_new("00:00.00");
    gtk_widget_add_css_class(g_state.sw_label, "timer-display");
    gtk_box_append(GTK_BOX(box), g_state.sw_label);

    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);

    g_state.sw_start_btn = gtk_button_new_with_label("START");
    gtk_widget_add_css_class(g_state.sw_start_btn, "btn-start");
    g_signal_connect(g_state.sw_start_btn, "clicked", G_CALLBACK(sw_toggle_start), nullptr);
    gtk_box_append(GTK_BOX(btn_box), g_state.sw_start_btn);

    GtkWidget* lap_btn = gtk_button_new_with_label("LAP");
    gtk_widget_add_css_class(lap_btn, "btn-lap");
    g_signal_connect(lap_btn, "clicked", G_CALLBACK(sw_lap), nullptr);
    gtk_box_append(GTK_BOX(btn_box), lap_btn);

    GtkWidget* reset_btn = gtk_button_new_with_label("RESET");
    gtk_widget_add_css_class(reset_btn, "btn-reset");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(sw_reset), nullptr);
    gtk_box_append(GTK_BOX(btn_box), reset_btn);

    gtk_box_append(GTK_BOX(box), btn_box);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_add_css_class(scroll, "lap-scroll");

    g_state.sw_lap_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), g_state.sw_lap_box);
    gtk_box_append(GTK_BOX(box), scroll);

    return box;
}

static GtkWidget* build_calc_tab() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    GtkWidget* disp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(disp_box, "calc-display-box");

    g_state.calc_expr_label = gtk_label_new("");
    gtk_widget_add_css_class(g_state.calc_expr_label, "calc-expr-label");
    gtk_widget_set_halign(g_state.calc_expr_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(disp_box), g_state.calc_expr_label);

    g_state.calc_res_label = gtk_label_new("0");
    gtk_widget_add_css_class(g_state.calc_res_label, "calc-res-label");
    gtk_widget_set_halign(g_state.calc_res_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(disp_box), g_state.calc_res_label);

    gtk_box_append(GTK_BOX(box), disp_box);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_widget_set_vexpand(grid, TRUE);

    const char* keys[5][4] = {
        {"C", "+/-", "DEL", "/"},
        {"7", "8", "9", "*"},
        {"4", "5", "6", "-"},
        {"1", "2", "3", "+"},
        {"0", ".", "", "="}
    };

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 4; ++c) {
            std::string k = keys[r][c];
            if (k.empty()) continue;
            GtkWidget* b = gtk_button_new_with_label(k.c_str());
            gtk_widget_add_css_class(b, "calc-btn");
            if (k == "/" || k == "*" || k == "-" || k == "+" || k == "=") {
                gtk_widget_add_css_class(b, "calc-op-btn");
            } else if (k == "C" || k == "DEL") {
                gtk_widget_add_css_class(b, "calc-clear-btn");
            }
            if (k == "0") {
                gtk_grid_attach(GTK_GRID(grid), b, c, r, 2, 1);
            } else {
                gtk_grid_attach(GTK_GRID(grid), b, c, r, 1, 1);
            }
            g_signal_connect_swapped(b, "clicked", G_CALLBACK(+[](const char* str) {
                calc_press(str);
            }), (gpointer)keys[r][c]);
        }
    }

    gtk_box_append(GTK_BOX(box), grid);
    return box;
}

static GtkWidget* build_draw_tab() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(toolbar, GTK_ALIGN_CENTER);

    struct ColData { double r, g, b; };
    auto make_col_btn = [&](double r, double g, double b, const char* css_cls) {
        GtkWidget* btn = gtk_button_new();
        gtk_widget_add_css_class(btn, css_cls);
        gtk_widget_set_size_request(btn, 24, 24);
        auto* data = new ColData{r, g, b};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            auto* p = static_cast<ColData*>(user_data);
            g_state.cur_r = p->r;
            g_state.cur_g = p->g;
            g_state.cur_b = p->b;
        }), data, +[](gpointer d, GClosure*) { delete static_cast<ColData*>(d); }, G_CONNECT_DEFAULT);
        gtk_box_append(GTK_BOX(toolbar), btn);
    };

    make_col_btn(0.95, 0.95, 0.95, "col-white");
    make_col_btn(0.22, 0.74, 0.97, "col-cyan");
    make_col_btn(0.96, 0.62, 0.04, "col-amber");
    make_col_btn(0.94, 0.27, 0.27, "col-red");
    make_col_btn(0.04, 0.04, 0.04, "col-eraser");

    GtkWidget* clear_btn = gtk_button_new_with_label("󰃢 Clear");
    gtk_widget_add_css_class(clear_btn, "draw-act-btn");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(draw_clear), nullptr);
    gtk_box_append(GTK_BOX(toolbar), clear_btn);

    GtkWidget* copy_btn = gtk_button_new_with_label("󰅍 Copy");
    gtk_widget_add_css_class(copy_btn, "draw-act-btn");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(draw_copy_clip), nullptr);
    gtk_box_append(GTK_BOX(toolbar), copy_btn);

    gtk_box_append(GTK_BOX(box), toolbar);

    GtkWidget* frame = gtk_frame_new(nullptr);
    gtk_widget_add_css_class(frame, "draw-frame");
    gtk_widget_set_vexpand(frame, TRUE);

    g_state.draw_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_state.draw_area), draw_area_draw, nullptr, nullptr);

    GtkGesture* drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), nullptr);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), drag);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), nullptr);
    gtk_widget_add_controller(g_state.draw_area, GTK_EVENT_CONTROLLER(drag));

    gtk_frame_set_child(GTK_FRAME(frame), g_state.draw_area);
    gtk_box_append(GTK_BOX(box), frame);

    return box;
}

static GtkWidget* build_notes_tab() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget* text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(text_view, "notes-view");
    g_state.notes_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    g_signal_connect(g_state.notes_buffer, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer) {
        notes_save();
    }), nullptr);

    notes_load();

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text_view);
    gtk_box_append(GTK_BOX(box), scroll);

    return box;
}

static void create_popup_window() {
    g_state.popup_win = gtk_window_new();
    gtk_layer_init_for_window(GTK_WINDOW(g_state.popup_win));
    gtk_layer_set_layer(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(g_state.popup_win), 0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_namespace(GTK_WINDOW(g_state.popup_win), "monolith-toolbox-popup");

    gtk_window_set_default_size(GTK_WINDOW(g_state.popup_win), 330, 390);
    gtk_widget_add_css_class(g_state.popup_win, "toolbox-popup");

    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(header, "toolbox-header");
    gtk_widget_set_margin_start(header, 8);
    gtk_widget_set_margin_end(header, 8);
    gtk_widget_set_margin_top(header, 6);
    gtk_widget_set_margin_bottom(header, 2);

    GtkWidget* stack = gtk_stack_new();
    g_state.stack = stack;
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 140);

    GtkWidget* sw_tab = build_stopwatch_tab();
    GtkWidget* calc_tab = build_calc_tab();
    GtkWidget* draw_tab = build_draw_tab();
    GtkWidget* notes_tab = build_notes_tab();

    gtk_stack_add_titled(GTK_STACK(stack), sw_tab, "timer", "TIMER");
    gtk_stack_add_titled(GTK_STACK(stack), calc_tab, "calc", "CALCULATOR");
    gtk_stack_add_titled(GTK_STACK(stack), draw_tab, "draw", "CANVAS");
    gtk_stack_add_titled(GTK_STACK(stack), notes_tab, "notes", "NOTES");

    GtkWidget* switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_widget_set_hexpand(switcher, TRUE);
    gtk_box_append(GTK_BOX(header), switcher);

    GtkWidget* close_btn = gtk_button_new_with_label("✕");
    gtk_widget_add_css_class(close_btn, "toolbox-close-btn");
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(hide_popup), nullptr);
    gtk_box_append(GTK_BOX(header), close_btn);

    gtk_box_append(GTK_BOX(main_box), header);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_box_append(GTK_BOX(main_box), stack);

    gtk_window_set_child(GTK_WINDOW(g_state.popup_win), main_box);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer) -> gboolean {
        if (keyval == GDK_KEY_Escape) {
            hide_popup();
            return TRUE;
        }

        // Tab shortcuts: Alt+1 to Alt+4 or F1 to F4
        if ((state & GDK_ALT_MASK) || (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F4)) {
            int tab_idx = -1;
            if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F4) tab_idx = keyval - GDK_KEY_F1;
            else if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_4) tab_idx = keyval - GDK_KEY_1;
            if (tab_idx == 0) gtk_stack_set_visible_child_name(GTK_STACK(g_state.stack), "timer");
            else if (tab_idx == 1) gtk_stack_set_visible_child_name(GTK_STACK(g_state.stack), "calc");
            else if (tab_idx == 2) gtk_stack_set_visible_child_name(GTK_STACK(g_state.stack), "draw");
            else if (tab_idx == 3) gtk_stack_set_visible_child_name(GTK_STACK(g_state.stack), "notes");
            return TRUE;
        }

        const char* cur_tab = gtk_stack_get_visible_child_name(GTK_STACK(g_state.stack));
        if (cur_tab && std::string(cur_tab) == "calc") {
            if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
                calc_press(std::string(1, '0' + (keyval - GDK_KEY_0)));
                return TRUE;
            }
            if (keyval >= GDK_KEY_KP_0 && keyval <= GDK_KEY_KP_9) {
                calc_press(std::string(1, '0' + (keyval - GDK_KEY_KP_0)));
                return TRUE;
            }
            if (keyval == GDK_KEY_plus || keyval == GDK_KEY_KP_Add) {
                calc_press("+"); return TRUE;
            }
            if (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract) {
                calc_press("-"); return TRUE;
            }
            if (keyval == GDK_KEY_asterisk || keyval == GDK_KEY_KP_Multiply) {
                calc_press("*"); return TRUE;
            }
            if (keyval == GDK_KEY_slash || keyval == GDK_KEY_KP_Divide) {
                calc_press("/"); return TRUE;
            }
            if (keyval == GDK_KEY_period || keyval == GDK_KEY_comma || keyval == GDK_KEY_KP_Decimal) {
                calc_press("."); return TRUE;
            }
            if (keyval == GDK_KEY_equal || keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                calc_press("="); return TRUE;
            }
            if (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete) {
                calc_press("DEL"); return TRUE;
            }
            if (keyval == GDK_KEY_c || keyval == GDK_KEY_C) {
                calc_press("C"); return TRUE;
            }
        } else if (cur_tab && std::string(cur_tab) == "timer") {
            if (keyval == GDK_KEY_space) {
                sw_toggle_start(nullptr, nullptr);
                return TRUE;
            }
            if (keyval == GDK_KEY_l || keyval == GDK_KEY_L) {
                sw_lap(nullptr, nullptr);
                return TRUE;
            }
            if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
                sw_reset(nullptr, nullptr);
                return TRUE;
            }
        }

        return FALSE;
    }), nullptr);
    gtk_widget_add_controller(g_state.popup_win, key_ctrl);
}

static void toggle_popup(const std::string& edge) {
    if (!g_state.popup_win) create_popup_window();

    if (g_state.popup_visible) {
        hide_popup();
    } else {
        g_state.current_edge = edge;
        g_state.popup_visible = true;

        gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_TOP, FALSE);
        gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);

        if (edge == "left") {
            gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
            gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, 14);
            gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
        } else {
            gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
            gtk_layer_set_anchor(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
            gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_RIGHT, 14);
            gtk_layer_set_margin(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_EDGE_LEFT, 0);
        }

        gtk_layer_set_keyboard_mode(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
        gtk_widget_set_visible(g_state.popup_win, TRUE);
    }
}

static void hide_popup() {
    if (g_state.popup_win && g_state.popup_visible) {
        g_state.popup_visible = false;
        notes_save();
        gtk_layer_set_keyboard_mode(GTK_WINDOW(g_state.popup_win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_widget_set_visible(g_state.popup_win, FALSE);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// FULL-EDGE REACTIVE TRIGGER
// ──────────────────────────────────────────────────────────────────────────────
static GtkWidget* create_edge_trigger(const std::string& edge) {
    GtkWidget* win = gtk_window_new();
    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), 0);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_namespace(GTK_WINDOW(win), "monolith-toolbox-trigger");

    // Anchor to top, bottom, and target edge (full vertical coverage)
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
        double pill_h = 44.0;
        double pill_w = 3.0;
        double pill_x = 0.0;
        double pill_y = ts->cursor_y - pill_h / 2.0;
        if (pill_y < 8) pill_y = 8;
        if (pill_y + pill_h > height - 8) pill_y = height - 8 - pill_h;

        double radius = 1.5;
        cairo_new_sub_path(cr);
        cairo_arc(cr, pill_x + pill_w - radius, pill_y + radius, radius, -M_PI/2, 0);
        cairo_arc(cr, pill_x + pill_w - radius, pill_y + pill_h - radius, radius, 0, M_PI/2);
        cairo_arc(cr, pill_x + radius, pill_y + pill_h - radius, radius, M_PI/2, M_PI);
        cairo_arc(cr, pill_x + radius, pill_y + radius, radius, M_PI, 3*M_PI/2);
        cairo_close_path(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
        cairo_fill(cr);
    }, &trig_state, nullptr);

    gtk_window_set_child(GTK_WINDOW(win), da);

    // Follow cursor motion dynamically
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

    // Click to toggle
    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect_swapped(click, "pressed", G_CALLBACK(+[](const char* e) {
        toggle_popup(e);
    }), (gpointer)(edge == "left" ? "left" : "right"));
    gtk_widget_add_controller(da, GTK_EVENT_CONTROLLER(click));

    return win;
}

static void apply_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = R"CSS(* {
    font-family: 'JetBrains Mono', monospace;
}

window.edge-trigger-window, .edge-trigger-window {
    background: transparent;
    background-color: transparent;
    border: none;
    box-shadow: none;
}

.toolbox-popup {
    background: #000000;
    border: 1px solid #27272a;
    border-radius: 4px;
    box-shadow: 0 10px 40px rgba(0, 0, 0, 0.95);
}

.toolbox-header {
    border-bottom: 1px solid #18181b;
    padding: 2px 4px;
}

.toolbox-header stackswitcher {
    border: none;
    background: transparent;
}

.toolbox-header stackswitcher button {
    background: transparent;
    border: none;
    border-radius: 2px;
    color: #52525b;
    font-size: 10px;
    font-weight: 700;
    padding: 5px 10px;
    margin: 0 1px;
    transition: color 100ms ease, background 100ms ease;
}

.toolbox-header stackswitcher button:hover {
    color: #a1a1aa;
    background: #0c0c0e;
}

.toolbox-header stackswitcher button:checked {
    color: #ffffff;
    background: #18181b;
}

.toolbox-close-btn {
    background: transparent;
    border: none;
    color: #52525b;
    padding: 2px 6px;
    font-size: 12px;
    border-radius: 2px;
    transition: color 100ms ease, background 100ms ease;
}

.toolbox-close-btn:hover {
    background: #27272a;
    color: #f87171;
}

/* STOPER */
.timer-display {
    font-size: 34px;
    font-weight: 700;
    color: #ffffff;
    margin: 8px 0;
    font-variant-numeric: tabular-nums;
    letter-spacing: -1px;
}

.btn-start, .btn-pause, .btn-lap, .btn-reset {
    background: #09090b;
    border: 1px solid #27272a;
    color: #e4e4e7;
    font-size: 11px;
    font-weight: 700;
    padding: 6px 14px;
    border-radius: 2px;
    transition: background 100ms ease, border-color 100ms ease, color 100ms ease;
}

.btn-start:hover {
    background: #18181b;
    border-color: #ffffff;
    color: #ffffff;
}

.btn-pause {
    border-color: #f59e0b;
    color: #fef08a;
}

.btn-lap:hover, .btn-reset:hover {
    background: #18181b;
    border-color: #3f3f46;
    color: #ffffff;
}

.lap-scroll {
    background: #050505;
    border: 1px solid #18181b;
    border-radius: 2px;
    padding: 4px;
    min-height: 90px;
}

.lap-item {
    font-size: 11px;
    color: #71717a;
    font-variant-numeric: tabular-nums;
    padding: 2px 4px;
    border-bottom: 1px solid #0f0f12;
}

/* KALKULATOR */
.calc-display-box {
    background: #050505;
    border: 1px solid #18181b;
    border-radius: 2px;
    padding: 6px 10px;
    margin-bottom: 2px;
}

.calc-expr-label {
    font-size: 11px;
    color: #52525b;
    min-height: 14px;
    font-variant-numeric: tabular-nums;
}

.calc-res-label {
    font-size: 24px;
    font-weight: 700;
    color: #ffffff;
    font-variant-numeric: tabular-nums;
    letter-spacing: -0.5px;
}

.calc-btn {
    background: #09090b;
    border: 1px solid #18181b;
    color: #e4e4e7;
    font-size: 13px;
    font-weight: 600;
    border-radius: 2px;
    padding: 6px;
    transition: background 80ms ease, border-color 80ms ease;
}

.calc-btn:hover {
    background: #18181b;
    border-color: #27272a;
    color: #ffffff;
}

.calc-op-btn {
    background: #0c0c10;
    border: 1px solid #27272a;
    color: #38bdf8;
    font-weight: 700;
}

.calc-op-btn:hover {
    background: #1e1e24;
    border-color: #38bdf8;
    color: #ffffff;
}

.calc-clear-btn {
    background: #0c0c10;
    border: 1px solid #18181b;
    color: #f87171;
    font-weight: 700;
}

.calc-clear-btn:hover {
    background: #1e1e24;
    border-color: #f87171;
    color: #ffffff;
}

/* CANVAS */
.draw-frame {
    border: 1px solid #18181b;
    background: #050505;
    border-radius: 2px;
}

.col-white  { background: #ffffff; border-radius: 2px; }
.col-cyan   { background: #38bdf8; border-radius: 2px; }
.col-amber  { background: #f59e0b; border-radius: 2px; }
.col-red    { background: #ef4444; border-radius: 2px; }
.col-eraser { background: #000000; border: 1px solid #3f3f46; border-radius: 2px; }

.draw-act-btn {
    background: #09090b;
    border: 1px solid #18181b;
    color: #a1a1aa;
    font-size: 10px;
    font-weight: 600;
    padding: 3px 8px;
    border-radius: 2px;
}
.draw-act-btn:hover {
    background: #18181b;
    border-color: #27272a;
    color: #ffffff;
}

/* NOTATKI */
.notes-view {
    background: #050505;
    color: #e4e4e7;
    font-size: 12px;
    border: 1px solid #18181b;
    border-radius: 2px;
    padding: 8px;
})CSS";
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}

static void on_action_toggle(GSimpleAction*, GVariant*, gpointer) {
    toggle_popup(g_state.current_edge);
}

static void on_activate(GApplication* app, gpointer) {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    g_application_hold(app);
    apply_css();

    g_state.notes_path = std::string(getenv("HOME") ? getenv("HOME") : "") + "/.config/monolith/scratchpad.txt";

    // Add toggle action for CLI IPC
    GSimpleAction* act_toggle = g_simple_action_new("toggle", nullptr);
    g_signal_connect(act_toggle, "activate", G_CALLBACK(on_action_toggle), nullptr);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(act_toggle));

    // Full-edge triggers on Left and Right
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
        int c = 0;
        char** v = g_application_command_line_get_arguments(cmd, &c);
        if (c > 1 && std::string(v[1]) == "--toggle") {
            toggle_popup("left");
        } else {
            g_application_activate(app);
        }
        return 0;
    }), nullptr);

    int status = g_application_run(G_APPLICATION(g_state.app), argc, argv);
    g_object_unref(g_state.app);
    return status;
}
