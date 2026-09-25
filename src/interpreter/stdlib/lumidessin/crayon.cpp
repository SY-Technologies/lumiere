// Crayon -- the turtle-style cursor (docs/stdlib-lumidessin.md, "Crayon
// API"). A Crayon holds its own position, heading, pen state, and drawing
// style, and draws onto a canvas it strongly owns via Ref<CanvasState> (see
// CrayonState in state.hpp for the acyclic-ownership contract). Movement and
// drawing methods share one coordinate conversion and one line-drawing
// helper below, so canvas_x/canvas_y are derived exactly once per move.

#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "state.hpp"

#include <algorithm>
#include <cmath>

namespace lumiere
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

double normalize_degrees(double degrees)
{
    double result = std::fmod(degrees, 360.0);
    if (result < 0.0)
    {
        result += 360.0;
    }
    return result;
}

// docs/stdlib-lumidessin.md, "Crayon API": canvas_x = width/2 + crayon_x,
// canvas_y = height/2 - crayon_y. The one place this mapping is applied.
std::pair<double, double> to_canvas_point(const CanvasState &canvas, double x, double y)
{
    return {static_cast<double>(canvas.width) / 2.0 + x, static_cast<double>(canvas.height) / 2.0 - y};
}

// Draws the crayon's trail from its current position to (new_x, new_y), if
// the pen is down, then moves it there -- the shared tail of every movement
// method (avancer/reculer, aller_à, recentrer). A zero thickness is a no-op
// draw, not an error, matching every other stroke primitive in this module.
void move_crayon_to(CrayonState &crayon, double new_x, double new_y)
{
    if (crayon.pen_down && crayon.thickness > 0.0)
    {
        CanvasState &canvas = *crayon.canvas;
        const auto [from_cx, from_cy] = to_canvas_point(canvas, crayon.x, crayon.y);
        const auto [to_cx, to_cy] = to_canvas_point(canvas, new_x, new_y);
        ColorState color;
        color.r = crayon.color_r;
        color.g = crayon.color_g;
        color.b = crayon.color_b;
        color.a = crayon.color_a;
        raster_capsule(canvas, from_cx, from_cy, to_cx, to_cy, crayon.thickness / 2.0, color);
    }
    crayon.x = new_x;
    crayon.y = new_y;
}

void advance_crayon(CrayonState &crayon, double distance)
{
    const double heading_rad = crayon.heading_deg * kPi / 180.0;
    move_crayon_to(crayon, crayon.x + distance * std::cos(heading_rad), crayon.y + distance * std::sin(heading_rad));
}

} // namespace

void bind_canevas_crayon_methods(const Ref<LumiereObject> &object, CanvasState *state,
                                 const NativeFunctionFactory &make_native_function)
{
    object->fields["crayon"] = Value::fonction(make_native_function(
        [state, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.crayon", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.crayon", native_args.site);
            Value crayon = make_crayon_value(Ref<CanvasState>(state), make_native_function);
            runtime.annotate_value(crayon, "LumiDessin.Crayon", native_args.site);
            return crayon;
        }));
}

Value make_crayon_value(Ref<CanvasState> canvas, const NativeFunctionFactory &make_native_function)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "LumiDessin.Crayon";
    klass->type_identity = native_nominal_type_identity("LumiDessin", "Crayon");
    object->klass = std::move(klass);
    auto state = make_ref<CrayonState>(std::move(canvas));
    CrayonState *const raw_state = state.get();
    object->native_state = std::move(state);

    // Every closure below reaches its own object's state through a raw
    // CrayonState* captured at construction time, exactly like every other
    // Canevas/Image/Couleur method in this module: the closure lives inside
    // this same object's `fields` map, so it cannot outlive the state it
    // points to, and CrayonState's own Ref<CanvasState> field keeps the
    // canvas it draws on alive independently. Neither needs a
    // native_captures entry.

    object->fields["avancer"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Crayon.avancer", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.avancer", native_args.site);
            const double distance = expect_finite(runtime, args[0].value, "Crayon.avancer", native_args.site);
            advance_crayon(*raw_state, distance);
            return Value::rien();
        }));

    object->fields["reculer"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Crayon.reculer", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.reculer", native_args.site);
            const double distance = expect_finite(runtime, args[0].value, "Crayon.reculer", native_args.site);
            // "reculer(d) is exactly avancer(-d)" (docs/stdlib-lumidessin.md).
            advance_crayon(*raw_state, -distance);
            return Value::rien();
        }));

    object->fields["tourner_gauche"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Crayon.tourner_gauche", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.tourner_gauche", native_args.site);
            const double angle = expect_finite(runtime, args[0].value, "Crayon.tourner_gauche", native_args.site);
            // Positive headings are counterclockwise, so turning left adds.
            raw_state->heading_deg = normalize_degrees(raw_state->heading_deg + angle);
            return Value::rien();
        }));

    object->fields["tourner_droite"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Crayon.tourner_droite", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.tourner_droite", native_args.site);
            const double angle = expect_finite(runtime, args[0].value, "Crayon.tourner_droite", native_args.site);
            raw_state->heading_deg = normalize_degrees(raw_state->heading_deg - angle);
            return Value::rien();
        }));

    object->fields["aller_à"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Crayon.aller_à", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.aller_à", native_args.site);
            const double x = expect_finite(runtime, args[0].value, "Crayon.aller_à", native_args.site);
            const double y = expect_finite(runtime, args[1].value, "Crayon.aller_à", native_args.site);
            move_crayon_to(*raw_state, x, y);
            return Value::rien();
        }));

    object->fields["recentrer"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.recentrer", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.recentrer", native_args.site);
            move_crayon_to(*raw_state, 0.0, 0.0);
            raw_state->heading_deg = 0.0;
            return Value::rien();
        }));

    object->fields["lever"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.lever", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.lever", native_args.site);
            raw_state->pen_down = false;
            return Value::rien();
        }));

    object->fields["baisser"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.baisser", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.baisser", native_args.site);
            raw_state->pen_down = true;
            return Value::rien();
        }));

    object->fields["est_baissé"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.est_baissé", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.est_baissé", native_args.site);
            return Value::logique(raw_state->pen_down);
        }));

    object->fields["régler_couleur"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Crayon.régler_couleur", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.régler_couleur", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[0].value, "Crayon.régler_couleur", native_args.site);
            raw_state->color_r = color.r;
            raw_state->color_g = color.g;
            raw_state->color_b = color.b;
            raw_state->color_a = color.a;
            return Value::rien();
        }));

    object->fields["régler_épaisseur"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Crayon.régler_épaisseur", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.régler_épaisseur", native_args.site);
            raw_state->thickness =
                expect_non_negative_finite(runtime, args[0].value, "Crayon.régler_épaisseur", native_args.site);
            return Value::rien();
        }));

    object->fields["régler_cap"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Crayon.régler_cap", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.régler_cap", native_args.site);
            const double angle = expect_finite(runtime, args[0].value, "Crayon.régler_cap", native_args.site);
            raw_state->heading_deg = normalize_degrees(angle);
            return Value::rien();
        }));

    object->fields["position"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.position", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.position", native_args.site);
            Value point = make_point_value(raw_state->x, raw_state->y);
            runtime.annotate_value(point, "LumiDessin.Point", native_args.site);
            return point;
        }));

    object->fields["cap"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.cap", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.cap", native_args.site);
            return Value::decimal(raw_state->heading_deg);
        }));

    object->fields["montrer"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.montrer", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.montrer", native_args.site);
            raw_state->visible = true;
            return Value::rien();
        }));

    object->fields["cacher"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.cacher", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.cacher", native_args.site);
            raw_state->visible = false;
            return Value::rien();
        }));

    object->fields["est_visible"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Crayon.est_visible", native_args.site);
            expect_canevas_open(runtime, *raw_state->canvas, "Crayon.est_visible", native_args.site);
            return Value::logique(raw_state->visible);
        }));

    return Value::objet(std::move(object));
}

} // namespace lumiere
