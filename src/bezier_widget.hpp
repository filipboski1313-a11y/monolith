#pragma once

#include <gtk/gtk.h>
#include <cairo.h>
#include <iomanip>
#include <sstream>

namespace Monolith {

struct BezierData {
    double p1x = 0.05;
    double p1y = 0.90;
    double p2x = 0.10;
    double p2y = 1.00;
};

inline void draw_bezier_func(
    GtkDrawingArea* drawing_area,
    cairo_t* cr,
    int width,
    int height,
    gpointer user_data
) {
    BezierData* d = static_cast<BezierData*>(user_data);
    double p1x = d ? d->p1x : 0.05;
    double p1y = d ? d->p1y : 0.90;
    double p2x = d ? d->p2x : 0.10;
    double p2y = d ? d->p2y : 1.00;

    // Background: pure brutalist black
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    // Padding & Coordinate box
    double pad_x = 40.0;
    double pad_top = 25.0;
    double pad_bot = 30.0;
    double w = width - 2.0 * pad_x;
    double h = height - pad_top - pad_bot;

    // Grid coordinates: x in [0, 1], y in [0, 1.5] for overshoot
    auto to_screen_x = [&](double x) { return pad_x + x * w; };
    auto to_screen_y = [&](double y) { return (height - pad_bot) - (y / 1.5) * h; };

    // Draw coordinate box
    cairo_set_source_rgba(cr, 0.15, 0.15, 0.18, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, to_screen_x(0.0), to_screen_y(1.5), w, h);
    cairo_stroke(cr);

    // Draw unit box (1.0 x 1.0)
    cairo_set_source_rgba(cr, 0.22, 0.22, 0.25, 0.5);
    double dashes[] = {3.0, 3.0};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_rectangle(cr, to_screen_x(0.0), to_screen_y(1.0), w, (1.0 / 1.5) * h);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // Reference diagonal (0,0) -> (1,1)
    cairo_set_source_rgba(cr, 0.25, 0.25, 0.28, 0.6);
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(0.0));
    cairo_line_to(cr, to_screen_x(1.0), to_screen_y(1.0));
    cairo_stroke(cr);

    // Control handle lines
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.55, 0.5);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(0.0));
    cairo_line_to(cr, to_screen_x(p1x), to_screen_y(p1y));
    cairo_stroke(cr);

    cairo_move_to(cr, to_screen_x(1.0), to_screen_y(1.0));
    cairo_line_to(cr, to_screen_x(p2x), to_screen_y(p2y));
    cairo_stroke(cr);

    // Cubic Bezier curve
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // Solid crisp white
    cairo_set_line_width(cr, 2.5);
    cairo_move_to(cr, to_screen_x(0.0), to_screen_y(0.0));
    cairo_curve_to(
        cr,
        to_screen_x(p1x), to_screen_y(p1y),
        to_screen_x(p2x), to_screen_y(p2y),
        to_screen_x(1.0), to_screen_y(1.0)
    );
    cairo_stroke(cr);

    // Control point circles P1 (Accent white/cyan)
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.95);
    cairo_arc(cr, to_screen_x(p1x), to_screen_y(p1y), 4.5, 0, 6.2831853);
    cairo_fill(cr);

    // Control point circle P2 (Muted gray)
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.65);
    cairo_arc(cr, to_screen_x(p2x), to_screen_y(p2y), 4.5, 0, 6.2831853);
    cairo_fill(cr);

    // Labels
    cairo_select_font_face(cr, "JetBrains Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9.5);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.55);

    cairo_move_to(cr, to_screen_x(0.0) - 18, to_screen_y(0.0) + 4);
    cairo_show_text(cr, "0.0");

    cairo_move_to(cr, to_screen_x(0.0) - 18, to_screen_y(1.0) + 4);
    cairo_show_text(cr, "1.0");

    cairo_move_to(cr, to_screen_x(1.0) - 8, to_screen_y(0.0) + 16);
    cairo_show_text(cr, "1.0");

    // Coordinates legend
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "P1: (" << p1x << ", " << p1y << ")   P2: (" << p2x << ", " << p2y << ")";
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.85);
    cairo_move_to(cr, pad_x, 15);
    cairo_show_text(cr, ss.str().c_str());
}

inline GtkWidget* create_bezier_widget(BezierData* data) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 340);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 170);
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(area),
        draw_bezier_func,
        data,
        nullptr
    );
    return area;
}

} // namespace Monolith