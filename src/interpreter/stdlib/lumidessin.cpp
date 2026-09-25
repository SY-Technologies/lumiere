#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lumiere
{

namespace
{

// ---------------------------------------------------------------------------
// Canvas size limits (docs/stdlib-lumidessin.md, "Canvas creation")
// ---------------------------------------------------------------------------

constexpr int64_t kMaxCanvasDimension = 16384;
constexpr int64_t kMaxCanvasPixels = 16'777'216;

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

struct ColorState : NativeState
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    void trace_references(RefVisitor &) const override
    {
    }
    void clear_references() override
    {
    }
};

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
// Point / Dimensions -- immutable value types with plain readable fields
// (not methods), per docs/stdlib-lumidessin.md's "Public types" section.
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

// make_dimensions_value() is added in the stage that introduces
// mesurer_texte(), its first caller.

// ---------------------------------------------------------------------------
// Image -- immutable decoded/captured RGBA8 pixels
// ---------------------------------------------------------------------------

struct ImageState : NativeState
{
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8, row-major, width * height * 4 bytes

    void trace_references(RefVisitor &) const override
    {
    }
    void clear_references() override
    {
    }
};

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

// ---------------------------------------------------------------------------
// Canevas -- mutable RGBA8 framebuffer. Stage 2 covers off-screen lifetime,
// clear, pixel read/write, and capture; the visible window (`ouvrir`),
// drawing primitives, text, images, and input arrive in later stages.
// ---------------------------------------------------------------------------

struct CanvasState : NativeState
{
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8, row-major, width * height * 4 bytes
    bool open = true;
    bool visible = false;

    void trace_references(RefVisitor &) const override
    {
    }
    void clear_references() override
    {
    }
};

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
    // largeur/hauteur/est_visible are ordinary methods and follow the
    // closed-canvas rule; est_ouvert is the one method that must remain
    // callable after close (it is how a caller learns that), and fermer
    // itself is idempotent rather than erroring.
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

    return Value::objet(std::move(object));
}

} // namespace

void register_lumidessin_module(Module &module)
{
    const auto &make_native_function = native_function_factory();

    stdlib_bind_public_type(module, "Canevas");
    stdlib_bind_public_type(module, "Crayon");
    stdlib_bind_public_type(module, "Couleur");
    stdlib_bind_public_type(module, "Image");
    stdlib_bind_public_type(module, "Point");
    stdlib_bind_public_type(module, "Dimensions");

    auto erreur_image_class = make_ref<LumiereClass>();
    erreur_image_class->name = "LumiDessin.ErreurImage";
    stdlib_bind_public_value(module, "ErreurImage", Value::classe(std::move(erreur_image_class)));

    auto erreur_couleur_class = make_ref<LumiereClass>();
    erreur_couleur_class->name = "LumiDessin.ErreurCouleur";
    stdlib_bind_public_value(module, "ErreurCouleur", Value::classe(std::move(erreur_couleur_class)));

    // ---- Canvas creation -----------------------------------------------

    stdlib_bind_public_function(
        module,
        make_native_function,
        "créer_hors_écran",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "LumiDessin.créer_hors_écran", native_args.site);
            const int64_t width =
                stdlib_expect_integer(runtime, args[0].value, "LumiDessin.créer_hors_écran", native_args.site);
            const int64_t height =
                stdlib_expect_integer(runtime, args[1].value, "LumiDessin.créer_hors_écran", native_args.site);
            validate_canvas_dimensions(runtime, width, height, "LumiDessin.créer_hors_écran", native_args.site);
            Value canvas = make_canevas_value(
                static_cast<int32_t>(width), static_cast<int32_t>(height), false, make_native_function);
            runtime.annotate_value(canvas, "LumiDessin.Canevas", native_args.site);
            return canvas;
        });

    // ---- Values and colors ------------------------------------------------

    stdlib_bind_public_function(
        module,
        make_native_function,
        "point",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "LumiDessin.point", native_args.site);
            const double x = stdlib_expect_decimal(runtime, args[0].value, "LumiDessin.point", native_args.site);
            const double y = stdlib_expect_decimal(runtime, args[1].value, "LumiDessin.point", native_args.site);
            Value point = make_point_value(x, y);
            runtime.annotate_value(point, "LumiDessin.Point", native_args.site);
            return point;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "rvb",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 3, "LumiDessin.rvb", native_args.site);
            const int64_t r = expect_color_component(runtime, args[0].value, "LumiDessin.rvb", native_args.site);
            const int64_t g = expect_color_component(runtime, args[1].value, "LumiDessin.rvb", native_args.site);
            const int64_t b = expect_color_component(runtime, args[2].value, "LumiDessin.rvb", native_args.site);
            Value color = make_couleur_value(
                static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 255, make_native_function);
            runtime.annotate_value(color, "LumiDessin.Couleur", native_args.site);
            return color;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "rvba",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 4, "LumiDessin.rvba", native_args.site);
            const int64_t r = expect_color_component(runtime, args[0].value, "LumiDessin.rvba", native_args.site);
            const int64_t g = expect_color_component(runtime, args[1].value, "LumiDessin.rvba", native_args.site);
            const int64_t b = expect_color_component(runtime, args[2].value, "LumiDessin.rvba", native_args.site);
            const int64_t a = expect_color_component(runtime, args[3].value, "LumiDessin.rvba", native_args.site);
            Value color = make_couleur_value(
                static_cast<uint8_t>(r),
                static_cast<uint8_t>(g),
                static_cast<uint8_t>(b),
                static_cast<uint8_t>(a),
                make_native_function);
            runtime.annotate_value(color, "LumiDessin.Couleur", native_args.site);
            return color;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "depuis_hex",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiDessin.depuis_hex", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "LumiDessin.depuis_hex", native_args.site);

            const auto fail = [&](const std::string &cause) {
                return stdlib_failure(make_erreur_couleur("depuis_hex", text, cause), native_args.site);
            };

            if (text.size() != 7 && text.size() != 9)
            {
                return fail("attend #RRGGBB ou #RRGGBBAA");
            }
            if (text[0] != '#')
            {
                return fail("attend un « # » en tête");
            }
            const auto r = parse_hex_byte(text[1], text[2]);
            const auto g = parse_hex_byte(text[3], text[4]);
            const auto b = parse_hex_byte(text[5], text[6]);
            if (!r || !g || !b)
            {
                return fail("attend des chiffres hexadécimaux");
            }
            uint8_t a = 255;
            if (text.size() == 9)
            {
                const auto parsed_a = parse_hex_byte(text[7], text[8]);
                if (!parsed_a)
                {
                    return fail("attend des chiffres hexadécimaux");
                }
                a = *parsed_a;
            }
            Value color = make_couleur_value(*r, *g, *b, a, make_native_function);
            runtime.annotate_value(color, "LumiDessin.Couleur", native_args.site);
            return stdlib_success(std::move(color));
        });

    // ---- Named colors (LumiDessin.Couleurs.*) ------------------------------

    struct NamedColor
    {
        const char *name;
        uint8_t r, g, b, a;
    };
    static constexpr std::array<NamedColor, 14> kNamedColors{{
        {"transparent", 0, 0, 0, 0},
        {"noir", 0, 0, 0, 255},
        {"blanc", 255, 255, 255, 255},
        {"gris", 128, 128, 128, 255},
        {"rouge", 255, 0, 0, 255},
        {"vert", 0, 255, 0, 255},
        {"bleu", 0, 0, 255, 255},
        {"jaune", 255, 255, 0, 255},
        {"cyan", 0, 255, 255, 255},
        {"magenta", 255, 0, 255, 255},
        {"orange", 255, 165, 0, 255},
        {"violet", 128, 0, 128, 255},
        {"rose", 255, 192, 203, 255},
        {"brun", 165, 42, 42, 255},
    }};

    auto couleurs_object = make_ref<LumiereObject>();
    for (const NamedColor &named : kNamedColors)
    {
        couleurs_object->fields[named.name] =
            make_couleur_value(named.r, named.g, named.b, named.a, make_native_function);
    }
    stdlib_bind_public_value(module, "Couleurs", Value::objet(std::move(couleurs_object)));
}

} // namespace lumiere
