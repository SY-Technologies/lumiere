// Stage 7 (and the event-pump half of stage 8): the SDL3 visible-window
// backend. This is the one translation unit in the module that includes an
// SDL3 header (docs/stdlib-lumidessin.md, "Platform backend"): every other
// file reaches a canvas's frame/input state through the plain data types in
// state.hpp, never through SDL.
//
// Compiled unconditionally. With LUMIERE_ENABLE_LUMIDESSIN_WINDOW off, the
// functions below still exist -- so lumidessin.cpp and values.cpp never
// need to #ifdef around calling them -- but fenêtre() always raises the
// documented availability error and no SDL header is included at all.

#include "state.hpp"

#include <chrono>
#include <cmath>
#include <thread>
#include <unordered_map>

#if LUMIERE_ENABLE_LUMIDESSIN_WINDOW
#include <SDL3/SDL.h>
#endif

namespace lumiere
{

// ---------------------------------------------------------------------------
// Frame pacing -- pure, no SDL, no sleep. See its declaration in state.hpp
// for the rule this implements: never catch up a missed deadline.
// ---------------------------------------------------------------------------

FrameWait compute_frame_wait(bool first_call, double now, double next_deadline)
{
    if (first_call || now >= next_deadline)
    {
        // First frame: never waits. A late frame: also never waits -- the
        // next deadline is scheduled from *now*, not from the missed one,
        // so a stall does not turn into a burst of instant frames after it.
        return FrameWait{0.0, now};
    }
    return FrameWait{next_deadline - now, next_deadline};
}

namespace
{

double real_now_seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void real_sleep_until(double absolute_seconds)
{
    using namespace std::chrono;
    const auto target = steady_clock::time_point(duration_cast<steady_clock::duration>(duration<double>(absolute_seconds)));
    std::this_thread::sleep_until(target);
}

// Process-wide window subsystem state (docs/stdlib-lumidessin.md's
// "LumiDessinModuleState"): whether SDL's video subsystem has been
// initialized, and which CanvasState -- if any -- currently owns the sole
// visible window. `visible_canvas` is a raw, non-owning pointer for the
// same reason CanvasState's own `crayons` list is: the canvas it names is
// kept alive by whatever Lumière Value or Ref reaches it, this subsystem
// only remembers which one that is, and CanvasState's destructor clears the
// pointer here if it is still the one recorded -- so it can never dangle.
struct WindowSubsystem
{
    bool sdl_video_initialized = false;
    CanvasState *visible_canvas = nullptr;
};

WindowSubsystem &window_subsystem()
{
    static WindowSubsystem subsystem;
    return subsystem;
}

} // namespace

#if LUMIERE_ENABLE_LUMIDESSIN_WINDOW

namespace
{

// docs/stdlib-lumidessin.md, "Input": canonical names identify a physical
// key position (scancode), not the character a layout produces, except for
// texte_saisi(). Both left/right variants of a modifier key collapse to one
// canonical name.
const std::unordered_map<SDL_Scancode, std::string> &scancode_names()
{
    static const std::unordered_map<SDL_Scancode, std::string> table = [] {
        std::unordered_map<SDL_Scancode, std::string> t;
        const char letters[] = "abcdefghijklmnopqrstuvwxyz";
        for (int i = 0; i < 26; ++i)
        {
            t[static_cast<SDL_Scancode>(SDL_SCANCODE_A + i)] = std::string(1, letters[i]);
        }
        // SDL orders digit scancodes 1..9, 0, not 0..9.
        for (int i = 0; i < 9; ++i)
        {
            t[static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i)] = std::string(1, static_cast<char>('1' + i));
        }
        t[SDL_SCANCODE_0] = "0";
        t[SDL_SCANCODE_SPACE] = "espace";
        t[SDL_SCANCODE_RETURN] = "entrée";
        t[SDL_SCANCODE_ESCAPE] = "échappement";
        t[SDL_SCANCODE_TAB] = "tabulation";
        t[SDL_SCANCODE_BACKSPACE] = "retour_arrière";
        t[SDL_SCANCODE_DELETE] = "supprimer";
        t[SDL_SCANCODE_LEFT] = "gauche";
        t[SDL_SCANCODE_RIGHT] = "droite";
        t[SDL_SCANCODE_UP] = "haut";
        t[SDL_SCANCODE_DOWN] = "bas";
        t[SDL_SCANCODE_HOME] = "début";
        t[SDL_SCANCODE_END] = "fin";
        t[SDL_SCANCODE_PAGEUP] = "page_haut";
        t[SDL_SCANCODE_PAGEDOWN] = "page_bas";
        for (int i = 0; i < 12; ++i)
        {
            t[static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + i)] = "f" + std::to_string(i + 1);
        }
        t[SDL_SCANCODE_LSHIFT] = "majuscule";
        t[SDL_SCANCODE_RSHIFT] = "majuscule";
        t[SDL_SCANCODE_LCTRL] = "contrôle";
        t[SDL_SCANCODE_RCTRL] = "contrôle";
        t[SDL_SCANCODE_LALT] = "option";
        t[SDL_SCANCODE_RALT] = "option";
        t[SDL_SCANCODE_LGUI] = "commande";
        t[SDL_SCANCODE_RGUI] = "commande";
        return t;
    }();
    return table;
}

const char *button_name(Uint8 sdl_button)
{
    switch (sdl_button)
    {
    case SDL_BUTTON_LEFT:
        return "gauche";
    case SDL_BUTTON_MIDDLE:
        return "milieu";
    case SDL_BUTTON_RIGHT:
        return "droite";
    case SDL_BUTTON_X1:
        return "x1";
    case SDL_BUTTON_X2:
        return "x2";
    default:
        return nullptr;
    }
}

} // namespace

// The real platform handle: an SDL window, a renderer, and the streaming
// texture présenter() uploads the framebuffer into. One per process, since
// only one visible canvas can exist at a time.
struct PlatformWindow
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    int32_t texture_width = 0;
    int32_t texture_height = 0;

    ~PlatformWindow()
    {
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
        if (renderer != nullptr)
            SDL_DestroyRenderer(renderer);
        if (window != nullptr)
            SDL_DestroyWindow(window);
    }
};

namespace
{

bool ensure_sdl_video(IRuntime &runtime, const RuntimeSite &site, const std::string &context)
{
    WindowSubsystem &subsystem = window_subsystem();
    if (subsystem.sdl_video_initialized)
    {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        runtime.raise_runtime_error(
            site, context + " : le système de fenêtrage n'est pas disponible (" + std::string(SDL_GetError()) + ")");
    }
    subsystem.sdl_video_initialized = true;
    return true;
}

// Pumps every pending SDL event into `state`'s input snapshot and close
// flag. Called only for a visible, open canvas. Transitions (`*_pressed`/
// `*_released`) accumulate across the whole pump, matching the documented
// rule that a press and release observed in the same pump make both
// transition queries true while current state ends released.
void pump_events(CanvasState &state)
{
    InputSnapshot fresh;
    fresh.keys_down = state.input.keys_down;
    fresh.mouse_x = state.input.mouse_x;
    fresh.mouse_y = state.input.mouse_y;
    fresh.mouse_inside = state.input.mouse_inside;
    fresh.buttons_down = state.input.buttons_down;
    // keys_pressed/keys_released/text/wheel_x/wheel_y/buttons_pressed/
    // buttons_released are transient: they start empty every pump.

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            state.close_requested = true;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const auto it = scancode_names().find(event.key.scancode);
            if (it == scancode_names().end())
            {
                break; // A physical key with no canonical name is simply invisible to Lumière.
            }
            const std::string &name = it->second;
            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (!event.key.repeat && fresh.keys_down.insert(name).second)
                {
                    fresh.keys_pressed.insert(name);
                }
            }
            else
            {
                if (fresh.keys_down.erase(name) > 0)
                {
                    fresh.keys_released.insert(name);
                }
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            fresh.text += event.text.text;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            fresh.mouse_x = event.motion.x;
            fresh.mouse_y = event.motion.y;
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            fresh.mouse_inside = true;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            fresh.mouse_inside = false;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const char *name = button_name(event.button.button);
            if (name == nullptr)
            {
                break;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                if (fresh.buttons_down.insert(name).second)
                {
                    fresh.buttons_pressed.insert(name);
                }
            }
            else
            {
                if (fresh.buttons_down.erase(name) > 0)
                {
                    fresh.buttons_released.insert(name);
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            fresh.wheel_x += event.wheel.x;
            fresh.wheel_y += event.wheel.y;
            break;
        default:
            break;
        }
    }
    state.input = std::move(fresh);
}

} // namespace

Value make_fenetre_value(IRuntime &runtime,
                         int32_t width,
                         int32_t height,
                         const std::string &title,
                         const NativeFunctionFactory &make_native_function,
                         const RuntimeSite &site)
{
    WindowSubsystem &subsystem = window_subsystem();
    if (subsystem.visible_canvas != nullptr)
    {
        runtime.raise_runtime_error(site, "LumiDessin.fenêtre : une fenêtre visible est déjà ouverte");
    }
    ensure_sdl_video(runtime, site, "LumiDessin.fenêtre");

    SDL_Window *window = SDL_CreateWindow(title.c_str(), width, height, 0);
    if (window == nullptr)
    {
        runtime.raise_runtime_error(
            site, "LumiDessin.fenêtre : la création de la fenêtre a échoué (" + std::string(SDL_GetError()) + ")");
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        SDL_DestroyWindow(window);
        runtime.raise_runtime_error(
            site, "LumiDessin.fenêtre : la création du moteur de rendu a échoué (" + std::string(SDL_GetError()) + ")");
    }

    Value canvas = make_canevas_value(width, height, true, make_native_function);
    auto *state = dynamic_cast<CanvasState *>(canvas.as_objet()->native_state.get());
    auto platform = std::make_unique<PlatformWindow>();
    platform->window = window;
    platform->renderer = renderer;
    state->platform_window = std::move(platform);
    subsystem.visible_canvas = state;

    runtime.annotate_value(canvas, "LumiDessin.Canevas", site);
    return canvas;
}

void release_platform_window(CanvasState &state)
{
    if (state.platform_window == nullptr)
    {
        return;
    }
    state.platform_window.reset();
    WindowSubsystem &subsystem = window_subsystem();
    if (subsystem.visible_canvas == &state)
    {
        subsystem.visible_canvas = nullptr;
    }
}

namespace
{

// présenter(): uploads `state`'s framebuffer to its window. A no-op when
// there is no platform window (off-screen canvas), matching the documented
// "valid no-op ... allowing the same rendering loop to run with or without
// a window". Overlay crayons are drawn into the RGBA framebuffer itself by
// crayon.cpp as each move happens, so there is nothing extra to composite
// here -- présenter() only has to get the existing framebuffer on screen.
void present_framebuffer(CanvasState &state)
{
    PlatformWindow *platform = state.platform_window.get();
    if (platform == nullptr)
    {
        return;
    }
    if (platform->texture == nullptr || platform->texture_width != state.width || platform->texture_height != state.height)
    {
        if (platform->texture != nullptr)
        {
            SDL_DestroyTexture(platform->texture);
        }
        platform->texture =
            SDL_CreateTexture(platform->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, state.width, state.height);
        platform->texture_width = state.width;
        platform->texture_height = state.height;
    }
    if (platform->texture != nullptr)
    {
        SDL_UpdateTexture(platform->texture, nullptr, state.pixels.data(), state.width * 4);
        SDL_RenderClear(platform->renderer);
        SDL_RenderTexture(platform->renderer, platform->texture, nullptr, nullptr);
        SDL_RenderPresent(platform->renderer);
    }
}

} // namespace

void bind_canevas_frame_methods(const Ref<LumiereObject> &object,
                                CanvasState *state,
                                const NativeFunctionFactory &make_native_function)
{
    object->fields["régler_cadence"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.régler_cadence", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.régler_cadence", native_args.site);
            const int64_t rate =
                stdlib_expect_integer(runtime, args[0].value, "Canevas.régler_cadence", native_args.site);
            if (rate < 1 || rate > 240)
            {
                runtime.raise_runtime_error(
                    native_args.site, "Canevas.régler_cadence attend une cadence entre 1 et 240 images par seconde");
            }
            state->frame_rate = static_cast<int>(rate);
            return Value::rien();
        }));

    object->fields["prochaine_image"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.prochaine_image", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.prochaine_image", native_args.site);

            const double interval = 1.0 / static_cast<double>(state->frame_rate);
            const double now = real_now_seconds();
            const bool first_call = !state->frame_started;
            const FrameWait wait = compute_frame_wait(first_call, now, state->next_deadline);
            if (wait.wait_seconds > 0.0)
            {
                real_sleep_until(wait.frame_now);
            }
            const double frame_now = wait.frame_now;
            state->last_elapsed = first_call ? 0.0 : (frame_now - state->last_frame_time);
            state->last_frame_time = frame_now;
            state->next_deadline = frame_now + interval;
            state->frame_started = true;

            if (state->visible && state->platform_window != nullptr)
            {
                pump_events(*state);
                if (state->close_requested)
                {
                    state->open = false;
                    state->pixels.clear();
                    state->pixels.shrink_to_fit();
                    release_platform_window(*state);
                    return Value::logique(false);
                }
            }
            return Value::logique(true);
        }));

    object->fields["écart_image"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.écart_image", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.écart_image", native_args.site);
            return Value::decimal(state->last_elapsed);
        }));

    object->fields["présenter"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.présenter", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.présenter", native_args.site);
            present_framebuffer(*state);
            return Value::rien();
        }));

    object->fields["attendre_fermeture"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.attendre_fermeture", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.attendre_fermeture", native_args.site);
            if (!state->visible || state->platform_window == nullptr)
            {
                runtime.raise_runtime_error(
                    native_args.site, "Canevas.attendre_fermeture attend un canevas visible");
            }
            present_framebuffer(*state);
            while (!state->close_requested)
            {
                pump_events(*state);
                if (!state->close_requested)
                {
                    SDL_Delay(1);
                }
            }
            state->open = false;
            state->pixels.clear();
            state->pixels.shrink_to_fit();
            release_platform_window(*state);
            return Value::rien();
        }));
}

#else // !LUMIERE_ENABLE_LUMIDESSIN_WINDOW

// Window support compiled out: PlatformWindow needs no real definition
// (fenêtre() never constructs one), but it must still be a complete type
// wherever a CanvasState is destroyed, so ~CanvasState() below can run
// std::unique_ptr<PlatformWindow>'s (trivial, always-null) deleter.
struct PlatformWindow
{
};

Value make_fenetre_value(IRuntime &runtime,
                         int32_t,
                         int32_t,
                         const std::string &,
                         const NativeFunctionFactory &,
                         const RuntimeSite &site)
{
    runtime.raise_runtime_error(
        site, "LumiDessin.fenêtre : le système de fenêtrage n'est pas disponible dans cette compilation");
}

void release_platform_window(CanvasState &)
{
}

void bind_canevas_frame_methods(const Ref<LumiereObject> &object,
                                CanvasState *state,
                                const NativeFunctionFactory &make_native_function)
{
    // Cadence timing still applies without window support -- an off-screen
    // canvas has always been the only kind reachable here, but the methods
    // themselves are unconditional so the rest of the module never needs to
    // know which build it is in.
    object->fields["régler_cadence"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Canevas.régler_cadence", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.régler_cadence", native_args.site);
            const int64_t rate =
                stdlib_expect_integer(runtime, args[0].value, "Canevas.régler_cadence", native_args.site);
            if (rate < 1 || rate > 240)
            {
                runtime.raise_runtime_error(
                    native_args.site, "Canevas.régler_cadence attend une cadence entre 1 et 240 images par seconde");
            }
            state->frame_rate = static_cast<int>(rate);
            return Value::rien();
        }));

    object->fields["prochaine_image"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.prochaine_image", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.prochaine_image", native_args.site);
            const double interval = 1.0 / static_cast<double>(state->frame_rate);
            const double now = real_now_seconds();
            const bool first_call = !state->frame_started;
            const FrameWait wait = compute_frame_wait(first_call, now, state->next_deadline);
            if (wait.wait_seconds > 0.0)
            {
                real_sleep_until(wait.frame_now);
            }
            const double frame_now = wait.frame_now;
            state->last_elapsed = first_call ? 0.0 : (frame_now - state->last_frame_time);
            state->last_frame_time = frame_now;
            state->next_deadline = frame_now + interval;
            state->frame_started = true;
            return Value::logique(true); // no window, so no close event ever arrives here.
        }));

    object->fields["écart_image"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.écart_image", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.écart_image", native_args.site);
            return Value::decimal(state->last_elapsed);
        }));

    object->fields["présenter"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.présenter", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.présenter", native_args.site);
            return Value::rien(); // always off-screen in this build: a valid no-op.
        }));

    object->fields["attendre_fermeture"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "Canevas.attendre_fermeture", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.attendre_fermeture", native_args.site);
            runtime.raise_runtime_error(
                native_args.site, "Canevas.attendre_fermeture attend un canevas visible");
        }));
}

#endif // LUMIERE_ENABLE_LUMIDESSIN_WINDOW

CanvasState::CanvasState() = default;

CanvasState::~CanvasState()
{
    release_platform_window(*this);
}

} // namespace lumiere
