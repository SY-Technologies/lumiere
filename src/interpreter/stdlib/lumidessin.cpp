#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

namespace lumiere
{

// Stage 1 of docs/stdlib-lumidessin.md's implementation order: register the
// module and its nominal types. Drawing, color, image, and window behavior
// are added in the stages that follow; this file grows in place rather than
// being replaced, since the design commits to one final implementation and
// not a staged rewrite.
void register_lumidessin_module(Module &module)
{
    stdlib_bind_public_type(module, "Canevas");
    stdlib_bind_public_type(module, "Crayon");
    stdlib_bind_public_type(module, "Couleur");
    stdlib_bind_public_type(module, "Image");
    stdlib_bind_public_type(module, "Point");
    stdlib_bind_public_type(module, "Dimensions");

    auto erreur_image_class = make_ref<LumiereClass>();
    erreur_image_class->name = "LumiDessin.ErreurImage";
    stdlib_bind_public_value(
        module,
        "ErreurImage",
        Value::classe(std::move(erreur_image_class)));

    auto erreur_couleur_class = make_ref<LumiereClass>();
    erreur_couleur_class->name = "LumiDessin.ErreurCouleur";
    stdlib_bind_public_value(
        module,
        "ErreurCouleur",
        Value::classe(std::move(erreur_couleur_class)));
}

} // namespace lumiere
