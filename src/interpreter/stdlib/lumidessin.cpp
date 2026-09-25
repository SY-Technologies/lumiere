#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumidessin/state.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace lumiere
{

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
            const double x = expect_finite(runtime, args[0].value, "LumiDessin.point", native_args.site);
            const double y = expect_finite(runtime, args[1].value, "LumiDessin.point", native_args.site);
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
