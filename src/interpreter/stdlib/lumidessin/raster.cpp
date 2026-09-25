#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace lumiere
{

namespace
{

// ---------------------------------------------------------------------------
// Point-list validation (expect_finite/expect_non_negative_finite are
// shared with lumidessin.cpp's point() constructor and declared in
// state.hpp)
// ---------------------------------------------------------------------------

bool is_point_object(const Value &value)
{
    return value.is_objet() && value.as_objet() != nullptr && value.as_objet()->klass != nullptr &&
           value.as_objet()->klass->name == "LumiDessin.Point";
}

std::pair<double, double> expect_point_xy(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    if (!is_point_object(value))
    {
        runtime.raise_runtime_error(site, context + " attend une Liste[Point]");
    }
    const auto &fields = value.as_objet()->fields;
    const double x = fields.at("x").as_decimal();
    const double y = fields.at("y").as_decimal();
    if (!std::isfinite(x) || !std::isfinite(y))
    {
        runtime.raise_runtime_error(site, context + " attend des points aux coordonnées finies");
    }
    return {x, y};
}

std::vector<std::pair<double, double>> expect_point_list(
    IRuntime &runtime, const Value &value, std::size_t minimum, const std::string &context, const RuntimeSite &site)
{
    if (!value.is_liste())
    {
        runtime.raise_runtime_error(site, context + " attend une Liste[Point]");
    }
    const auto &elements = value.as_liste()->elements;
    if (elements.size() < minimum)
    {
        runtime.raise_runtime_error(
            site, context + " attend au moins " + std::to_string(minimum) + " points");
    }
    std::vector<std::pair<double, double>> points;
    points.reserve(elements.size());
    for (const Value &element : elements)
    {
        points.push_back(expect_point_xy(runtime, element, context, site));
    }
    return points;
}

// ---------------------------------------------------------------------------
// Pixel compositing -- source-over, straight alpha, sRGB component space,
// round-half-away-from-zero. docs/stdlib-lumidessin.md, "Compositing".
// ---------------------------------------------------------------------------

uint8_t round_channel(double value)
{
    const double rounded = value >= 0.0 ? std::floor(value + 0.5) : std::ceil(value - 0.5);
    return static_cast<uint8_t>(std::clamp(rounded, 0.0, 255.0));
}

} // namespace

// Shared with text.cpp (glyph coverage compositing uses the same
// source-over path as every SDF primitive below), so this one function --
// alone among this file's blending/rasterization internals -- has external
// linkage and a declaration in state.hpp instead of living in the
// anonymous namespace above.
void blend_pixel(CanvasState &canvas, int x, int y, const ColorState &color, double coverage)
{
    if (x < 0 || x >= canvas.width || y < 0 || y >= canvas.height || coverage <= 0.0)
    {
        return;
    }
    const double src_a = (color.a / 255.0) * std::min(coverage, 1.0);
    if (src_a <= 0.0)
    {
        return;
    }
    uint8_t *const pixel = &canvas.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.width) +
                                           static_cast<std::size_t>(x)) *
                                          4];
    const double dst_a = pixel[3] / 255.0;
    const double out_a = src_a + dst_a * (1.0 - src_a);
    const auto blend_channel = [&](uint8_t src_channel, uint8_t dst_channel) -> uint8_t {
        if (out_a <= 0.0)
        {
            return 0;
        }
        const double s = src_channel / 255.0;
        const double d = dst_channel / 255.0;
        const double out_c = (s * src_a + d * dst_a * (1.0 - src_a)) / out_a;
        return round_channel(out_c * 255.0);
    };
    pixel[0] = blend_channel(color.r, pixel[0]);
    pixel[1] = blend_channel(color.g, pixel[1]);
    pixel[2] = blend_channel(color.b, pixel[2]);
    pixel[3] = round_channel(out_a * 255.0);
}

namespace
{


// ---------------------------------------------------------------------------
// Signed-distance-field rasterization. Every primitive below reduces to a
// function of a pixel-center point returning a signed distance (negative
// inside the shape, in pixel units); coverage = clamp(0.5 - distance, 0, 1)
// gives a ~1px antialiased edge, and the shared loop clips to the canvas by
// only ever visiting pixels inside the (already-clipped) bounding box.
// remplir_polygone is the one exception, using even-odd supersampling
// instead, since an analytic signed distance to an arbitrary concave or
// self-intersecting path is not this simple.
// ---------------------------------------------------------------------------

template <typename SignedDistance>
void raster_sdf(CanvasState &canvas, double min_x, double min_y, double max_x, double max_y,
                const ColorState &color, SignedDistance &&sdf)
{
    const int x0 = std::max(0, static_cast<int>(std::floor(min_x - 1.0)));
    const int y0 = std::max(0, static_cast<int>(std::floor(min_y - 1.0)));
    const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(max_x + 1.0)));
    const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(max_y + 1.0)));
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const double coverage = std::clamp(0.5 - sdf(x + 0.5, y + 0.5), 0.0, 1.0);
            if (coverage > 0.0)
            {
                blend_pixel(canvas, x, y, color, coverage);
            }
        }
    }
}

double point_segment_distance(double px, double py, double ax, double ay, double bx, double by)
{
    const double abx = bx - ax;
    const double aby = by - ay;
    const double length_squared = abx * abx + aby * aby;
    double t = length_squared > 0.0 ? ((px - ax) * abx + (py - ay) * aby) / length_squared : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double cx = ax + t * abx;
    const double cy = ay + t * aby;
    return std::hypot(px - cx, py - cy);
}

double capsule_sdf(double px, double py, double ax, double ay, double bx, double by, double half_thickness)
{
    return point_segment_distance(px, py, ax, ay, bx, by) - half_thickness;
}

void raster_capsule(CanvasState &canvas, double ax, double ay, double bx, double by, double half_thickness,
                    const ColorState &color)
{
    raster_sdf(
        canvas,
        std::min(ax, bx) - half_thickness,
        std::min(ay, by) - half_thickness,
        std::max(ax, bx) + half_thickness,
        std::max(ay, by) + half_thickness,
        color,
        [=](double px, double py) { return capsule_sdf(px, py, ax, ay, bx, by, half_thickness); });
}

// A closed or open polyline's stroke is the union of a capsule per segment:
// each capsule's own round caps meet exactly at shared vertices, which is
// what gives round joins with no separate join logic.
void raster_polyline_stroke(CanvasState &canvas, const std::vector<std::pair<double, double>> &points, bool closed,
                            double half_thickness, const ColorState &color)
{
    const std::size_t segment_count = points.size() - 1 + (closed ? 1 : 0);
    for (std::size_t i = 0; i < segment_count; ++i)
    {
        const auto &[ax, ay] = points[i];
        const auto &[bx, by] = points[(i + 1) % points.size()];
        raster_capsule(canvas, ax, ay, bx, by, half_thickness, color);
    }
}

double rectangle_fill_sdf(double px, double py, double x0, double y0, double x1, double y1)
{
    const double dx = std::max(x0 - px, px - x1);
    const double dy = std::max(y0 - py, py - y1);
    const double outside = std::hypot(std::max(dx, 0.0), std::max(dy, 0.0));
    const double inside = std::min(std::max(dx, dy), 0.0);
    return outside + inside;
}

// rx/ry are clamped away from exactly zero solely to keep the normalized
// radial distance below finite -- an ellipse with one axis at zero is a
// legitimate degenerate shape (a very thin sliver), not an error.
double ellipse_signed_distance(double px, double py, double cx, double cy, double rx, double ry)
{
    const double safe_rx = std::max(rx, 1e-6);
    const double safe_ry = std::max(ry, 1e-6);
    const double nx = (px - cx) / safe_rx;
    const double ny = (py - cy) / safe_ry;
    const double normalized_radius = std::hypot(nx, ny);
    return (normalized_radius - 1.0) * std::min(safe_rx, safe_ry);
}

void raster_ellipse_fill(CanvasState &canvas, double cx, double cy, double rx, double ry, const ColorState &color)
{
    raster_sdf(canvas, cx - rx, cy - ry, cx + rx, cy + ry, color,
              [=](double px, double py) { return ellipse_signed_distance(px, py, cx, cy, rx, ry); });
}

void raster_ellipse_stroke(CanvasState &canvas, double cx, double cy, double rx, double ry, double half_thickness,
                           const ColorState &color)
{
    raster_sdf(
        canvas, cx - rx - half_thickness, cy - ry - half_thickness, cx + rx + half_thickness, cy + ry + half_thickness,
        color,
        [=](double px, double py) {
            return std::fabs(ellipse_signed_distance(px, py, cx, cy, rx, ry)) - half_thickness;
        });
}

constexpr double kPi = 3.14159265358979323846;

// theta_deg is the angle of a point relative to the arc's center, using the
// same convention as the arc's own angle_début/amplitude: 0 points right,
// positive is clockwise. atan2(dy, dx) already reads that way because
// screen y increases downward, so no sign flip is needed here.
bool angle_within_sweep(double theta_deg, double start_deg, double amplitude_deg)
{
    double start = std::fmod(start_deg, 360.0);
    if (start < 0.0)
        start += 360.0;
    double theta = std::fmod(theta_deg, 360.0);
    if (theta < 0.0)
        theta += 360.0;
    if (amplitude_deg >= 0.0)
    {
        double swept = std::fmod(theta - start, 360.0);
        if (swept < 0.0)
            swept += 360.0;
        return swept <= amplitude_deg;
    }
    double swept = std::fmod(start - theta, 360.0);
    if (swept < 0.0)
        swept += 360.0;
    return swept <= -amplitude_deg;
}

void raster_arc_stroke(CanvasState &canvas, double cx, double cy, double radius, double start_deg, double amplitude_deg,
                       double half_thickness, const ColorState &color)
{
    const double amplitude = std::clamp(amplitude_deg, -360.0, 360.0);
    const double start_rad = start_deg * kPi / 180.0;
    const double end_rad = (start_deg + amplitude) * kPi / 180.0;
    const double p1x = cx + radius * std::cos(start_rad);
    const double p1y = cy + radius * std::sin(start_rad);
    const double p2x = cx + radius * std::cos(end_rad);
    const double p2y = cy + radius * std::sin(end_rad);

    raster_sdf(
        canvas, cx - radius - half_thickness, cy - radius - half_thickness, cx + radius + half_thickness,
        cy + radius + half_thickness, color,
        [=](double px, double py) {
            const double theta = std::atan2(py - cy, px - cx) * 180.0 / kPi;
            const double ring = std::fabs(std::hypot(px - cx, py - cy) - radius) - half_thickness;
            const double within_ring = angle_within_sweep(theta, start_deg, amplitude) ? ring : HUGE_VAL;
            const double cap1 = std::hypot(px - p1x, py - p1y) - half_thickness;
            const double cap2 = std::hypot(px - p2x, py - p2y) - half_thickness;
            return std::min({within_ring, cap1, cap2});
        });
}

bool point_in_polygon_even_odd(double px, double py, const std::vector<std::pair<double, double>> &points)
{
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++)
    {
        const auto &[xi, yi] = points[i];
        const auto &[xj, yj] = points[j];
        const bool crosses = (yi > py) != (yj > py);
        if (crosses && px < (xj - xi) * (py - yi) / (yj - yi) + xi)
        {
            inside = !inside;
        }
    }
    return inside;
}

// Even-odd fill via 4x4 supersampling rather than an analytic edge
// function: docs/stdlib-lumidessin.md allows any antialiasing that agrees
// between engines, and a closed-form exact-coverage formula for an
// arbitrary concave or self-intersecting polygon is not worth the
// complexity here.
void raster_polygon_fill(CanvasState &canvas, const std::vector<std::pair<double, double>> &points, const ColorState &color)
{
    double min_x = points[0].first, max_x = points[0].first;
    double min_y = points[0].second, max_y = points[0].second;
    for (const auto &[x, y] : points)
    {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }

    const int x0 = std::max(0, static_cast<int>(std::floor(min_x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(min_y)));
    const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(max_x)));
    const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(max_y)));
    constexpr int kSamplesPerAxis = 4;
    constexpr int kTotalSamples = kSamplesPerAxis * kSamplesPerAxis;

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            int inside_count = 0;
            for (int sy = 0; sy < kSamplesPerAxis; ++sy)
            {
                for (int sx = 0; sx < kSamplesPerAxis; ++sx)
                {
                    const double sample_x = x + (sx + 0.5) / kSamplesPerAxis;
                    const double sample_y = y + (sy + 0.5) / kSamplesPerAxis;
                    if (point_in_polygon_even_odd(sample_x, sample_y, points))
                    {
                        ++inside_count;
                    }
                }
            }
            if (inside_count > 0)
            {
                blend_pixel(canvas, x, y, color, static_cast<double>(inside_count) / kTotalSamples);
            }
        }
    }
}

} // namespace

void bind_canevas_drawing_methods(const Ref<LumiereObject> &object, CanvasState *state,
                                  const NativeFunctionFactory &make_native_function)
{
    object->fields["dessiner_pixel"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 3, "Canevas.dessiner_pixel", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.dessiner_pixel", native_args.site);
            const int64_t x = stdlib_expect_integer(runtime, args[0].value, "Canevas.dessiner_pixel", native_args.site);
            const int64_t y = stdlib_expect_integer(runtime, args[1].value, "Canevas.dessiner_pixel", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[2].value, "Canevas.dessiner_pixel", native_args.site);
            blend_pixel(*state, static_cast<int>(x), static_cast<int>(y), color, 1.0);
            return Value::rien();
        }));

    object->fields["tracer_ligne"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 6, "Canevas.tracer_ligne", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.tracer_ligne", native_args.site);
            const double x1 = expect_finite(runtime, args[0].value, "Canevas.tracer_ligne", native_args.site);
            const double y1 = expect_finite(runtime, args[1].value, "Canevas.tracer_ligne", native_args.site);
            const double x2 = expect_finite(runtime, args[2].value, "Canevas.tracer_ligne", native_args.site);
            const double y2 = expect_finite(runtime, args[3].value, "Canevas.tracer_ligne", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[4].value, "Canevas.tracer_ligne", native_args.site);
            const double thickness =
                expect_non_negative_finite(runtime, args[5].value, "Canevas.tracer_ligne", native_args.site);
            if (thickness > 0.0)
            {
                raster_capsule(*state, x1, y1, x2, y2, thickness / 2.0, color);
            }
            return Value::rien();
        }));

    object->fields["tracer_rectangle"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 6, "Canevas.tracer_rectangle", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.tracer_rectangle", native_args.site);
            const double x = expect_finite(runtime, args[0].value, "Canevas.tracer_rectangle", native_args.site);
            const double y = expect_finite(runtime, args[1].value, "Canevas.tracer_rectangle", native_args.site);
            const double width =
                expect_non_negative_finite(runtime, args[2].value, "Canevas.tracer_rectangle", native_args.site);
            const double height =
                expect_non_negative_finite(runtime, args[3].value, "Canevas.tracer_rectangle", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[4].value, "Canevas.tracer_rectangle", native_args.site);
            const double thickness =
                expect_non_negative_finite(runtime, args[5].value, "Canevas.tracer_rectangle", native_args.site);
            if (thickness > 0.0)
            {
                const std::vector<std::pair<double, double>> corners{
                    {x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}};
                raster_polyline_stroke(*state, corners, true, thickness / 2.0, color);
            }
            return Value::rien();
        }));

    object->fields["remplir_rectangle"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 5, "Canevas.remplir_rectangle", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.remplir_rectangle", native_args.site);
            const double x = expect_finite(runtime, args[0].value, "Canevas.remplir_rectangle", native_args.site);
            const double y = expect_finite(runtime, args[1].value, "Canevas.remplir_rectangle", native_args.site);
            const double width =
                expect_non_negative_finite(runtime, args[2].value, "Canevas.remplir_rectangle", native_args.site);
            const double height =
                expect_non_negative_finite(runtime, args[3].value, "Canevas.remplir_rectangle", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[4].value, "Canevas.remplir_rectangle", native_args.site);
            if (width > 0.0 && height > 0.0)
            {
                raster_sdf(*state, x, y, x + width, y + height, color, [=](double px, double py) {
                    return rectangle_fill_sdf(px, py, x, y, x + width, y + height);
                });
            }
            return Value::rien();
        }));

    object->fields["tracer_cercle"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 5, "Canevas.tracer_cercle", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.tracer_cercle", native_args.site);
            const double cx = expect_finite(runtime, args[0].value, "Canevas.tracer_cercle", native_args.site);
            const double cy = expect_finite(runtime, args[1].value, "Canevas.tracer_cercle", native_args.site);
            const double radius =
                expect_non_negative_finite(runtime, args[2].value, "Canevas.tracer_cercle", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[3].value, "Canevas.tracer_cercle", native_args.site);
            const double thickness =
                expect_non_negative_finite(runtime, args[4].value, "Canevas.tracer_cercle", native_args.site);
            if (thickness > 0.0)
            {
                raster_ellipse_stroke(*state, cx, cy, radius, radius, thickness / 2.0, color);
            }
            return Value::rien();
        }));

    object->fields["remplir_cercle"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 4, "Canevas.remplir_cercle", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.remplir_cercle", native_args.site);
            const double cx = expect_finite(runtime, args[0].value, "Canevas.remplir_cercle", native_args.site);
            const double cy = expect_finite(runtime, args[1].value, "Canevas.remplir_cercle", native_args.site);
            const double radius =
                expect_non_negative_finite(runtime, args[2].value, "Canevas.remplir_cercle", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[3].value, "Canevas.remplir_cercle", native_args.site);
            if (radius > 0.0)
            {
                raster_ellipse_fill(*state, cx, cy, radius, radius, color);
            }
            return Value::rien();
        }));

    object->fields["tracer_ellipse"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 6, "Canevas.tracer_ellipse", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.tracer_ellipse", native_args.site);
            const double cx = expect_finite(runtime, args[0].value, "Canevas.tracer_ellipse", native_args.site);
            const double cy = expect_finite(runtime, args[1].value, "Canevas.tracer_ellipse", native_args.site);
            const double rx =
                expect_non_negative_finite(runtime, args[2].value, "Canevas.tracer_ellipse", native_args.site);
            const double ry =
                expect_non_negative_finite(runtime, args[3].value, "Canevas.tracer_ellipse", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[4].value, "Canevas.tracer_ellipse", native_args.site);
            const double thickness =
                expect_non_negative_finite(runtime, args[5].value, "Canevas.tracer_ellipse", native_args.site);
            if (thickness > 0.0)
            {
                raster_ellipse_stroke(*state, cx, cy, rx, ry, thickness / 2.0, color);
            }
            return Value::rien();
        }));

    object->fields["remplir_ellipse"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 5, "Canevas.remplir_ellipse", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.remplir_ellipse", native_args.site);
            const double cx = expect_finite(runtime, args[0].value, "Canevas.remplir_ellipse", native_args.site);
            const double cy = expect_finite(runtime, args[1].value, "Canevas.remplir_ellipse", native_args.site);
            const double rx =
                expect_non_negative_finite(runtime, args[2].value, "Canevas.remplir_ellipse", native_args.site);
            const double ry =
                expect_non_negative_finite(runtime, args[3].value, "Canevas.remplir_ellipse", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[4].value, "Canevas.remplir_ellipse", native_args.site);
            if (rx > 0.0 && ry > 0.0)
            {
                raster_ellipse_fill(*state, cx, cy, rx, ry, color);
            }
            return Value::rien();
        }));

    object->fields["tracer_arc"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 7, "Canevas.tracer_arc", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.tracer_arc", native_args.site);
            const double cx = expect_finite(runtime, args[0].value, "Canevas.tracer_arc", native_args.site);
            const double cy = expect_finite(runtime, args[1].value, "Canevas.tracer_arc", native_args.site);
            const double radius = expect_non_negative_finite(runtime, args[2].value, "Canevas.tracer_arc", native_args.site);
            const double start = expect_finite(runtime, args[3].value, "Canevas.tracer_arc", native_args.site);
            const double amplitude = expect_finite(runtime, args[4].value, "Canevas.tracer_arc", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[5].value, "Canevas.tracer_arc", native_args.site);
            const double thickness =
                expect_non_negative_finite(runtime, args[6].value, "Canevas.tracer_arc", native_args.site);
            if (thickness > 0.0)
            {
                raster_arc_stroke(*state, cx, cy, radius, start, amplitude, thickness / 2.0, color);
            }
            return Value::rien();
        }));

    object->fields["tracer_polyligne"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 4, "Canevas.tracer_polyligne", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.tracer_polyligne", native_args.site);
            const auto points = expect_point_list(runtime, args[0].value, 2, "Canevas.tracer_polyligne", native_args.site);
            if (!args[1].value.is_logique())
            {
                runtime.raise_runtime_error(native_args.site, "Canevas.tracer_polyligne attend une Logique");
            }
            const bool closed = args[1].value.as_logique();
            const ColorState &color = expect_couleur(runtime, args[2].value, "Canevas.tracer_polyligne", native_args.site);
            const double thickness =
                expect_non_negative_finite(runtime, args[3].value, "Canevas.tracer_polyligne", native_args.site);
            if (thickness > 0.0)
            {
                raster_polyline_stroke(*state, points, closed, thickness / 2.0, color);
            }
            return Value::rien();
        }));

    object->fields["remplir_polygone"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Canevas.remplir_polygone", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.remplir_polygone", native_args.site);
            const auto points = expect_point_list(runtime, args[0].value, 3, "Canevas.remplir_polygone", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[1].value, "Canevas.remplir_polygone", native_args.site);
            raster_polygon_fill(*state, points, color);
            return Value::rien();
        }));
}

} // namespace lumiere
