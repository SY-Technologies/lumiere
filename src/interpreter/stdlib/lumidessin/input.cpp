// Stage 8: the Lumière-facing input methods. This file only ever reads
// CanvasState::input (an InputSnapshot, defined in state.hpp) -- it has no
// SDL dependency of its own; window.cpp's event pump is what populates that
// snapshot each prochaine_image(). See docs/stdlib-lumidessin.md, "Input".

#include "state.hpp"

namespace lumiere
{

namespace
{

void expect_known_key(IRuntime &runtime, const std::string &name, const std::string &context, const RuntimeSite &site)
{
    if (canonical_key_names().find(name) == canonical_key_names().end())
    {
        runtime.raise_runtime_error(site, context + " ne reconnaît pas le nom de touche « " + name + " »");
    }
}

void expect_known_button(IRuntime &runtime, const std::string &name, const std::string &context, const RuntimeSite &site)
{
    if (canonical_button_names().find(name) == canonical_button_names().end())
    {
        runtime.raise_runtime_error(site, context + " ne reconnaît pas le nom de bouton « " + name + " »");
    }
}

} // namespace

const std::unordered_set<std::string> &canonical_key_names()
{
    static const std::unordered_set<std::string> names = [] {
        std::unordered_set<std::string> n;
        for (char c = 'a'; c <= 'z'; ++c)
        {
            n.insert(std::string(1, c));
        }
        for (char c = '0'; c <= '9'; ++c)
        {
            n.insert(std::string(1, c));
        }
        for (const char *name : {"espace", "entrée", "échappement", "tabulation", "retour_arrière", "supprimer",
                                 "gauche", "droite", "haut", "bas", "début", "fin", "page_haut", "page_bas"})
        {
            n.insert(name);
        }
        for (int i = 1; i <= 12; ++i)
        {
            n.insert("f" + std::to_string(i));
        }
        for (const char *name : {"majuscule", "contrôle", "option", "commande"})
        {
            n.insert(name);
        }
        return n;
    }();
    return names;
}

const std::unordered_set<std::string> &canonical_button_names()
{
    static const std::unordered_set<std::string> names{"gauche", "milieu", "droite", "x1", "x2"};
    return names;
}

void bind_canevas_input_methods(const Ref<LumiereObject> &object,
                                CanvasState *state,
                                const NativeFunctionFactory &make_native_function)
{
    object->fields["touche_enfoncée"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.touche_enfoncée", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.touche_enfoncée", native_args.site);
            const std::string name = stdlib_expect_text(runtime, args[0].value, "Canevas.touche_enfoncée", native_args.site);
            expect_known_key(runtime, name, "Canevas.touche_enfoncée", native_args.site);
            return Value::logique(state->input.keys_down.count(name) > 0);
        }));
    object->fields["touche_pressée"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.touche_pressée", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.touche_pressée", native_args.site);
            const std::string name = stdlib_expect_text(runtime, args[0].value, "Canevas.touche_pressée", native_args.site);
            expect_known_key(runtime, name, "Canevas.touche_pressée", native_args.site);
            return Value::logique(state->input.keys_pressed.count(name) > 0);
        }));
    object->fields["touche_relâchée"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.touche_relâchée", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.touche_relâchée", native_args.site);
            const std::string name = stdlib_expect_text(runtime, args[0].value, "Canevas.touche_relâchée", native_args.site);
            expect_known_key(runtime, name, "Canevas.touche_relâchée", native_args.site);
            return Value::logique(state->input.keys_released.count(name) > 0);
        }));
    object->fields["texte_saisi"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.texte_saisi", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.texte_saisi", native_args.site);
            return Value::texte(state->input.text);
        }));

    object->fields["position_souris"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.position_souris", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.position_souris", native_args.site);
            Value point = make_point_value(state->input.mouse_x, state->input.mouse_y);
            runtime.annotate_value(point, "LumiDessin.Point", native_args.site);
            return point;
        }));
    object->fields["souris_présente"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.souris_présente", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.souris_présente", native_args.site);
            return Value::logique(state->input.mouse_inside);
        }));
    object->fields["bouton_enfoncé"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.bouton_enfoncé", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.bouton_enfoncé", native_args.site);
            const std::string name = stdlib_expect_text(runtime, args[0].value, "Canevas.bouton_enfoncé", native_args.site);
            expect_known_button(runtime, name, "Canevas.bouton_enfoncé", native_args.site);
            return Value::logique(state->input.buttons_down.count(name) > 0);
        }));
    object->fields["bouton_pressé"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.bouton_pressé", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.bouton_pressé", native_args.site);
            const std::string name = stdlib_expect_text(runtime, args[0].value, "Canevas.bouton_pressé", native_args.site);
            expect_known_button(runtime, name, "Canevas.bouton_pressé", native_args.site);
            return Value::logique(state->input.buttons_pressed.count(name) > 0);
        }));
    object->fields["bouton_relâché"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.bouton_relâché", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.bouton_relâché", native_args.site);
            const std::string name = stdlib_expect_text(runtime, args[0].value, "Canevas.bouton_relâché", native_args.site);
            expect_known_button(runtime, name, "Canevas.bouton_relâché", native_args.site);
            return Value::logique(state->input.buttons_released.count(name) > 0);
        }));
    object->fields["défilement"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.défilement", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.défilement", native_args.site);
            Value point = make_point_value(state->input.wheel_x, state->input.wheel_y);
            runtime.annotate_value(point, "LumiDessin.Point", native_args.site);
            return point;
        }));
}

} // namespace lumiere
