#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "state.hpp"

#include <cmath>

namespace lumiere
{

double expect_finite(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    const double v = stdlib_expect_decimal(runtime, value, context, site);
    if (!std::isfinite(v))
    {
        runtime.raise_runtime_error(site, context + " attend une valeur numérique finie");
    }
    return v;
}

double expect_non_negative_finite(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    const double v = expect_finite(runtime, value, context, site);
    if (v < 0.0)
    {
        runtime.raise_runtime_error(site, context + " attend une valeur non négative");
    }
    return v;
}

void validate_canvas_dimensions(IRuntime &runtime,
                                int64_t width,
                                int64_t height,
                                const std::string &context,
                                const RuntimeSite &site)
{
    if (width < 1 || width > kMaxCanvasDimension || height < 1 || height > kMaxCanvasDimension)
    {
        runtime.raise_runtime_error(
            site, context + " attend des dimensions entre 1 et " + std::to_string(kMaxCanvasDimension));
    }
    if (width * height > kMaxCanvasPixels)
    {
        runtime.raise_runtime_error(
            site, context + " attend un canevas d'au plus " + std::to_string(kMaxCanvasPixels) + " pixels");
    }
}

// ---------------------------------------------------------------------------
// Couleur
// ---------------------------------------------------------------------------

Value make_couleur_value(uint8_t r, uint8_t g, uint8_t b, uint8_t a, const NativeFunctionFactory &make_native_function)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "LumiDessin.Couleur";
    klass->type_identity = native_nominal_type_identity("LumiDessin", "Couleur");
    object->klass = std::move(klass);
    auto state = make_ref<ColorState>();
    state->r = r;
    state->g = g;
    state->b = b;
    state->a = a;
    object->native_state = std::move(state);

    // Immutable components: each accessor closure captures its byte by
    // value, so no reference back to `state` -- and no native_captures
    // entry -- is needed for these four.
    object->fields["rouge"] = Value::fonction(make_native_function(
        [r](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Couleur.rouge", native_args.site);
            return Value::entier(r);
        }));
    object->fields["vert"] = Value::fonction(make_native_function(
        [g](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Couleur.vert", native_args.site);
            return Value::entier(g);
        }));
    object->fields["bleu"] = Value::fonction(make_native_function(
        [b](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Couleur.bleu", native_args.site);
            return Value::entier(b);
        }));
    object->fields["alpha"] = Value::fonction(make_native_function(
        [a](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Couleur.alpha", native_args.site);
            return Value::entier(a);
        }));

    return Value::objet(std::move(object));
}

bool is_couleur_object(const Value &value)
{
    return value.is_objet() && value.as_objet() != nullptr && value.as_objet()->klass != nullptr &&
           value.as_objet()->klass->name == "LumiDessin.Couleur";
}

const ColorState &expect_couleur(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    if (!is_couleur_object(value))
    {
        runtime.raise_runtime_error(site, context + " attend une valeur de type Couleur");
    }
    auto *state = dynamic_cast<ColorState *>(value.as_objet()->native_state.get());
    if (state == nullptr)
    {
        runtime.raise_runtime_error(site, context + " attend une valeur Couleur valide");
    }
    return *state;
}

int64_t expect_color_component(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    const int64_t component = stdlib_expect_integer(runtime, value, context, site);
    if (component < 0 || component > 255)
    {
        runtime.raise_runtime_error(site, context + " attend une composante de couleur entre 0 et 255");
    }
    return component;
}

// LumiDessin.ErreurCouleur carries opération/valeur/cause rather than the
// shared opération/cause/chemin shape stdlib_error_value() builds -- "chemin"
// does not name what a color parse failed on. Mirrors that helper's
// construction, the same choice JSON's ligne/colonne error made.
Value make_erreur_couleur(const std::string &operation, const std::string &value_text, const std::string &cause)
{
    auto klass = make_ref<LumiereClass>();
    klass->name = "LumiDessin.ErreurCouleur";
    klass->type_identity = native_nominal_type_identity(klass->name);
    auto error_interface = make_ref<LumiereInterface>();
    error_interface->name = "Erreur";
    error_interface->type_identity = "Erreur";
    klass->interfaces.emplace("Erreur", std::move(error_interface));

    auto object = make_ref<LumiereObject>();
    object->klass = std::move(klass);
    object->fields.emplace("opération", Value::texte(operation));
    object->fields.emplace("valeur", Value::texte(value_text));
    object->fields.emplace("cause", Value::texte(cause));
    return Value::objet(std::move(object));
}

std::optional<uint8_t> parse_hex_byte(char high, char low)
{
    const auto nibble = [](char c) -> std::optional<int> {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F')
            return 10 + (c - 'A');
        return std::nullopt;
    };
    const auto h = nibble(high);
    const auto l = nibble(low);
    if (!h || !l)
        return std::nullopt;
    return static_cast<uint8_t>((*h << 4) | *l);
}

// ---------------------------------------------------------------------------
// Point -- immutable value type with plain readable fields (not methods),
// per docs/stdlib-lumidessin.md's "Public types" section.
// ---------------------------------------------------------------------------

Value make_point_value(double x, double y)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "LumiDessin.Point";
    klass->type_identity = native_nominal_type_identity("LumiDessin", "Point");
    object->klass = std::move(klass);
    object->fields["x"] = Value::decimal(x);
    object->fields["y"] = Value::decimal(y);
    return Value::objet(std::move(object));
}

// ---------------------------------------------------------------------------
// Dimensions -- immutable value type with plain readable fields (not
// methods), per docs/stdlib-lumidessin.md's "Public types" section.
// ---------------------------------------------------------------------------

Value make_dimensions_value(int32_t width, int32_t height)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "LumiDessin.Dimensions";
    klass->type_identity = native_nominal_type_identity("LumiDessin", "Dimensions");
    object->klass = std::move(klass);
    object->fields["largeur"] = Value::entier(width);
    object->fields["hauteur"] = Value::entier(height);
    return Value::objet(std::move(object));
}

// ---------------------------------------------------------------------------
// Image -- immutable decoded/captured RGBA8 pixels
// ---------------------------------------------------------------------------

Value make_image_value(int32_t width,
                       int32_t height,
                       std::vector<uint8_t> pixels,
                       const NativeFunctionFactory &make_native_function)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "LumiDessin.Image";
    klass->type_identity = native_nominal_type_identity("LumiDessin", "Image");
    object->klass = std::move(klass);
    auto state = make_ref<ImageState>();
    state->width = width;
    state->height = height;
    state->pixels = std::move(pixels);
    object->native_state = std::move(state);

    object->fields["largeur"] = Value::fonction(make_native_function(
        [width](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Image.largeur", native_args.site);
            return Value::entier(width);
        }));
    object->fields["hauteur"] = Value::fonction(make_native_function(
        [height](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Image.hauteur", native_args.site);
            return Value::entier(height);
        }));

    return Value::objet(std::move(object));
}

bool is_image_object(const Value &value)
{
    return value.is_objet() && value.as_objet() != nullptr && value.as_objet()->klass != nullptr &&
           value.as_objet()->klass->name == "LumiDessin.Image";
}

const ImageState &expect_image(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    if (!is_image_object(value))
    {
        runtime.raise_runtime_error(site, context + " attend une valeur de type Image");
    }
    auto *state = dynamic_cast<ImageState *>(value.as_objet()->native_state.get());
    if (state == nullptr)
    {
        runtime.raise_runtime_error(site, context + " attend une valeur Image valide");
    }
    return *state;
}

// LumiDessin.ErreurImage fits stdlib_error_value's opération/chemin/cause
// shape exactly (docs/stdlib-lumidessin.md, "Public types"), unlike
// ErreurCouleur -- so it uses that shared helper directly rather than a
// bespoke constructor.
Value make_erreur_image(const std::string &operation, const std::string &path, const std::string &cause)
{
    return stdlib_error_value("LumiDessin.ErreurImage", operation, cause, path);
}

// ---------------------------------------------------------------------------
// Canevas -- mutable RGBA8 framebuffer. Off-screen lifetime, clear, pixel
// read/write, and capture live here; drawing-primitive methods are added by
// bind_canevas_drawing_methods() (raster.cpp) before this function returns.
// ---------------------------------------------------------------------------

void expect_canevas_open(IRuntime &runtime, const CanvasState &state, const std::string &context, const RuntimeSite &site)
{
    if (!state.open)
    {
        runtime.raise_runtime_error(site, context + " ne peut pas utiliser un canevas fermé");
    }
}

Value make_canevas_value(int32_t width, int32_t height, bool visible, const NativeFunctionFactory &make_native_function)
{
    auto object = make_ref<LumiereObject>();
    auto klass = make_ref<LumiereClass>();
    klass->name = "LumiDessin.Canevas";
    klass->type_identity = native_nominal_type_identity("LumiDessin", "Canevas");
    object->klass = std::move(klass);
    auto state = make_ref<CanvasState>();
    state->width = width;
    state->height = height;
    // A new canvas is initialized to opaque white.
    state->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0xFF);
    state->visible = visible;
    CanvasState *const raw_state = state.get();
    object->native_state = std::move(state);

    // Every method below reaches its own object's state through a raw
    // pointer captured at construction time. That pointer stays valid for
    // exactly as long as this object does, because the closure holding it
    // lives inside this same object's `fields` map -- there is no window in
    // which the closure outlives the state it points to, so no
    // native_captures entry is needed here (contrast Crayon, in a later
    // stage, whose methods reach a *different* object's state and do need
    // one).
    object->fields["largeur"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.largeur", native_args.site);
            expect_canevas_open(runtime, *raw_state, "Canevas.largeur", native_args.site);
            return Value::entier(raw_state->width);
        }));
    object->fields["hauteur"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.hauteur", native_args.site);
            expect_canevas_open(runtime, *raw_state, "Canevas.hauteur", native_args.site);
            return Value::entier(raw_state->height);
        }));
    object->fields["est_visible"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.est_visible", native_args.site);
            expect_canevas_open(runtime, *raw_state, "Canevas.est_visible", native_args.site);
            return Value::logique(raw_state->visible);
        }));
    object->fields["est_ouvert"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.est_ouvert", native_args.site);
            return Value::logique(raw_state->open);
        }));
    object->fields["fermer"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.fermer", native_args.site);
            // Idempotent: closing an already-closed canvas is a no-op.
            raw_state->open = false;
            raw_state->pixels.clear();
            raw_state->pixels.shrink_to_fit();
            return Value::rien();
        }));
    object->fields["effacer"] = Value::fonction(make_native_function(
        [raw_state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.effacer", native_args.site);
            expect_canevas_open(runtime, *raw_state, "Canevas.effacer", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[0].value, "Canevas.effacer", native_args.site);
            for (std::size_t i = 0; i < raw_state->pixels.size(); i += 4)
            {
                raw_state->pixels[i + 0] = color.r;
                raw_state->pixels[i + 1] = color.g;
                raw_state->pixels[i + 2] = color.b;
                raw_state->pixels[i + 3] = color.a;
            }
            return Value::rien();
        }));
    object->fields["lire_pixel"] = Value::fonction(make_native_function(
        [raw_state, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Canevas.lire_pixel", native_args.site);
            expect_canevas_open(runtime, *raw_state, "Canevas.lire_pixel", native_args.site);
            const int64_t x = stdlib_expect_integer(runtime, args[0].value, "Canevas.lire_pixel", native_args.site);
            const int64_t y = stdlib_expect_integer(runtime, args[1].value, "Canevas.lire_pixel", native_args.site);
            if (x < 0 || x >= raw_state->width || y < 0 || y >= raw_state->height)
            {
                runtime.raise_runtime_error(
                    native_args.site, "Canevas.lire_pixel attend des coordonnées à l'intérieur du canevas");
            }
            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(raw_state->width) +
                                        static_cast<std::size_t>(x)) *
                                       4;
            return make_couleur_value(
                raw_state->pixels[offset + 0],
                raw_state->pixels[offset + 1],
                raw_state->pixels[offset + 2],
                raw_state->pixels[offset + 3],
                make_native_function);
        }));
    object->fields["capturer"] = Value::fonction(make_native_function(
        [raw_state, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.capturer", native_args.site);
            expect_canevas_open(runtime, *raw_state, "Canevas.capturer", native_args.site);
            return make_image_value(raw_state->width, raw_state->height, raw_state->pixels, make_native_function);
        }));

    bind_canevas_drawing_methods(object, raw_state, make_native_function);
    bind_canevas_text_methods(object, raw_state, make_native_function);
    bind_canevas_image_methods(object, raw_state, make_native_function);

    return Value::objet(std::move(object));
}

} // namespace lumiere
