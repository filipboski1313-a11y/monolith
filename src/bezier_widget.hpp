#pragma once

#include <gtk/gtk.h>
#include <cairo.h>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <functional>

namespace Monolith {

struct BezierData {
    double p1x = 0.05;
    double p1y = 0.90;
    double p2x = 0.18;
    double p2y = 1.00;
    int dragging_point = 0; // 0 = none, 1 = P1, 2 = P2
    double anim_t = 0.0;
    bool anim_forward = true;
    GtkWidget* area = nullptr;
    std::function<void(double, double, double, double)> on_drag_changed;
};

// Evaluate cubic Bezier at parameter t in [0, 1]
inline double eval_bezier_coord(double t, double p1, double p2) {
    double u = 1.0 - t;
    return 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t;
}

// Solve y(x) for cubic Bezier curve given x in [0, 1]
inline double solve_bezier_y_for_x(double x, double p1x, double p1y, double p2x, double p2y) {
    double t = std::clamp(x, 0.0, 1.0);
    for (int i = 0; i < 6; ++i) {
        double bx = eval_bezier_coord(t, p1x, p2x) - x;
        if (std::abs(bx) < 1e-5) break;
        double u = 1.0 - t;
        double dbx = 3.0 * u * u * p1x + 6.0 * u * t * (p2x - p1x) + 3.0 * t * t * (1.0 - p2x);
        if (std::abs(dbx) < 1e-6) break;
        t = std::clamp(t - bx / dbx, 0.0, 1.0);
    }
    return eval_bezier_coord(t, p1y, p2y);
}

inline void draw_bezier_func(
    GtkDrawingArea*,
    cairo_t* cr,
    int width,
    int height,
    gpointer user_data
) {
    BezierData* d = static_cast<BezierData*>(user_data);
    double p1x = d ? d->p1x : 0.05;
    double p1y = d ? d->p1y : 0.90;
    double p2x = d ? d->p2x : 0.18;
    double p2y = d ? d->p2y : 1.00;

    // Background: crisp architectural dark card (#040406) with 1px border
    double r = 8.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, width - r, r, r, -M_PI / 2.0, 0);
    cairo_arc(cr, width - r, height - r, r, 0, M_PI / 2.0);
    cairo_arc(cr, r, height - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, r, r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0.015, 0.015, 0.022);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.18, 0.18, 0.22, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    double pad_x = 44.0;
    double pad_top = 28.0;
    double pad_bot = 36.0;
    double w = width - 2.0 * pad_x;
    double h = height - pad_top - pad_bot;

    auto to_screen_x = [&](double x) { return pad_x + x * w; };
    auto to_screen_y = [&](double y) { return (height - pad_bot) - (y / 1.45) * h; };

    // Subtle internal grid lines
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.15, 0.7);
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i <= 3; ++i) {
        double gx = to_screen_x(i * 0.25);
        cairo_move_to(cr, gx, to_screen_y(1.45));
        cairo_line_to(cr, gx, to_screen_y(0.0));
        cairo_stroke(cr);
    }

    // Outer coordinate frame
    cairo_set_source_rgba(cr, 0.16, 0.16, 0.20, 0.9);
    cairo_rectangle(cr, to_screen_x(0.0), to_screen_y(1.45), w, h);
    cairo_stroke(cr);

    // 1.0 target ceiling line (dashed)
    cairo_set_source_rgba(cr, 0.28, 0.28, 0.34, 0.65);
    double dashes[] = {4.0, 4.0};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(1.0));
    cairo_line_to(cr, to_screen_x(1.0), to_screen_y(1.0));
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // Reference linear diagonal (0,0) -> (1,1)
    cairo_set_source_rgba(cr, 0.22, 0.22, 0.26, 0.6);
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(0.0));
    cairo_line_to(cr, to_screen_x(1.0), to_screen_y(1.0));
    cairo_stroke(cr);

    // Subtle shaded area under curve
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(0.0));
    cairo_curve_to(
        cr,
        to_screen_x(p1x), to_screen_y(p1y),
        to_screen_x(p2x), to_screen_y(p2y),
        to_screen_x(1.0), to_screen_y(1.0)
    );
    cairo_line_to(cr, to_screen_x(1.0), to_screen_y(0.0));
    cairo_close_path(cr);
    cairo_pattern_t* grad = cairo_pattern_create_linear(0, to_screen_y(1.2), 0, to_screen_y(0.0));
    cairo_pattern_add_color_stop_rgba(grad, 0.0, 1.0, 1.0, 1.0, 0.12);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, 1.0, 1.0, 1.0, 0.01);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);

    // Tangent control handles
    cairo_set_source_rgba(cr, 0.45, 0.75, 0.95, 0.65);
    cairo_set_line_width(cr, 1.3);
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(0.0));
    cairo_line_to(cr, to_screen_x(p1x), to_screen_y(p1y));
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0.70, 0.70, 0.78, 0.65);
    cairo_move_to(cr, to_screen_x(1.0), to_screen_y(1.0));
    cairo_line_to(cr, to_screen_x(p2x), to_screen_y(p2y));
    cairo_stroke(cr);

    // Cubic Bezier Curve (Crisp white)
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(0.0));
    cairo_curve_to(
        cr,
        to_screen_x(p1x), to_screen_y(p1y),
        to_screen_x(p2x), to_screen_y(p2y),
        to_screen_x(1.0), to_screen_y(1.0)
    );
    cairo_stroke(cr);

    // Interactive Control Point P1 (Cyan accent)
    double p1_sx = to_screen_x(p1x);
    double p1_sy = to_screen_y(p1y);
    cairo_arc(cr, p1_sx, p1_sy, (d && d->dragging_point == 1) ? 7.5 : 5.5, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.07);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    // Interactive Control Point P2 (Silver/White accent)
    double p2_sx = to_screen_x(p2x);
    double p2_sy = to_screen_y(p2y);
    cairo_arc(cr, p2_sx, p2_sy, (d && d->dragging_point == 2) ? 7.5 : 5.5, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.07);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.90, 0.90, 0.95);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    // Live Motion Preview Track at bottom of widget
    if (d) {
        double track_y = height - 14.0;
        cairo_set_source_rgba(cr, 0.18, 0.18, 0.22, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_move_to(cr, to_screen_x(0.0), track_y);
        cairo_line_to(cr, to_screen_x(1.0), track_y);
        cairo_stroke(cr);

        double prog_y = solve_bezier_y_for_x(d->anim_t, p1x, p1y, p2x, p2y);
        double puck_x = to_screen_x(std::clamp(prog_y, -0.1, 1.25));
        cairo_arc(cr, puck_x, track_y, 4.0, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_fill(cr);

        // Also draw tracer dot on the curve itself
        double curve_x = to_screen_x(d->anim_t);
        double curve_y = to_screen_y(prog_y);
        cairo_arc(cr, curve_x, curve_y, 3.5, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
        cairo_fill(cr);
    }

    // Typography & Telemetry Legend
    cairo_select_font_face(cr, "JetBrains Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9.5);
    cairo_set_source_rgb(cr, 0.45, 0.45, 0.50);

    cairo_move_to(cr, to_screen_x(0.0) - 22, to_screen_y(0.0) + 3);
    cairo_show_text(cr, "0.0");
    cairo_move_to(cr, to_screen_x(0.0) - 22, to_screen_y(1.0) + 3);
    cairo_show_text(cr, "1.0");

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "P1(" << p1x << ", " << p1y << ")   P2(" << p2x << ", " << p2y << ")   [Drag points to tune]";
    cairo_set_source_rgb(cr, 0.78, 0.78, 0.84);
    cairo_move_to(cr, pad_x, 18);
    cairo_show_text(cr, ss.str().c_str());
}

inline GtkWidget* create_bezier_widget(BezierData* data) {
    GtkWidget* area = gtk_drawing_area_new();
    data->area = area;
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 440);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 210);
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(area),
        draw_bezier_func,
        data,
        nullptr
    );

    // Interactive Drag Controller so user can grab P1 or P2 directly on the canvas
    GtkGesture* drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK((+[](GtkGestureDrag*, double sx, double sy, gpointer user_data) {
        BezierData* d = static_cast<BezierData*>(user_data);
        int width = gtk_widget_get_width(d->area);
        int height = gtk_widget_get_height(d->area);
        double pad_x = 44.0, pad_top = 28.0, pad_bot = 36.0;
        double w = width - 2.0 * pad_x;
        double h = height - pad_top - pad_bot;
        auto to_sx = [&](double x) { return pad_x + x * w; };
        auto to_sy = [&](double y) { return (height - pad_bot) - (y / 1.45) * h; };

        double d1 = std::hypot(sx - to_sx(d->p1x), sy - to_sy(d->p1y));
        double d2 = std::hypot(sx - to_sx(d->p2x), sy - to_sy(d->p2y));
        if (d1 < 22.0 && d1 <= d2) d->dragging_point = 1;
        else if (d2 < 22.0) d->dragging_point = 2;
        else d->dragging_point = 0;
        gtk_widget_queue_draw(d->area);
    })), data);

    g_signal_connect(drag, "drag-update", G_CALLBACK((+[](GtkGestureDrag* gesture, double ox, double oy, gpointer user_data) {
        BezierData* d = static_cast<BezierData*>(user_data);
        if (d->dragging_point == 0) return;
        double start_x = 0, start_y = 0;
        gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
        double cx = start_x + ox;
        double cy = start_y + oy;

        int width = gtk_widget_get_width(d->area);
        int height = gtk_widget_get_height(d->area);
        double pad_x = 44.0, pad_top = 28.0, pad_bot = 36.0;
        double w = std::max(1.0, width - 2.0 * pad_x);
        double h = std::max(1.0, height - pad_top - pad_bot);

        double nx = std::clamp((cx - pad_x) / w, 0.0, 1.0);
        double ny = std::clamp(((height - pad_bot) - cy) / h * 1.45, 0.0, 1.45);

        if (d->dragging_point == 1) {
            d->p1x = nx;
            d->p1y = ny;
        } else if (d->dragging_point == 2) {
            d->p2x = nx;
            d->p2y = ny;
        }
        if (d->on_drag_changed) {
            d->on_drag_changed(d->p1x, d->p1y, d->p2x, d->p2y);
        }
        gtk_widget_queue_draw(d->area);
    })), data);

    g_signal_connect(drag, "drag-end", G_CALLBACK((+[](GtkGestureDrag*, double, double, gpointer user_data) {
        BezierData* d = static_cast<BezierData*>(user_data);
        d->dragging_point = 0;
        gtk_widget_queue_draw(d->area);
    })), data);

    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

    // Periodic animation tick for the live motion preview puck (only while mapped)
    g_timeout_add(24, +[](gpointer user_data) -> gboolean {
        BezierData* d = static_cast<BezierData*>(user_data);
        if (!d || !d->area || !GTK_IS_WIDGET(d->area)) return G_SOURCE_REMOVE;
        if (!gtk_widget_get_mapped(d->area)) return G_SOURCE_CONTINUE;

        if (d->anim_forward) {
            d->anim_t += 0.028;
            if (d->anim_t >= 1.25) {
                d->anim_t = 1.0;
                d->anim_forward = false;
            }
        } else {
            d->anim_t -= 0.028;
            if (d->anim_t <= -0.25) {
                d->anim_t = 0.0;
                d->anim_forward = true;
            }
        }
        gtk_widget_queue_draw(d->area);
        return G_SOURCE_CONTINUE;
    }, data);

    return area;
}

} // namespace Monolith
