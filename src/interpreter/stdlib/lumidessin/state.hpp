#pragma once

// Internal, non-public header shared by lumidessin.cpp and the files under
// src/interpreter/stdlib/lumidessin/. Not part of include/: nothing here is
// reachable from outside this module's own translation units, matching
// docs/stdlib-lumidessin.md's "Platform backend" rule that implementation
// types stay out of public headers -- the same discipline applies to these
// value-representation types, which are just as much an implementation
// detail of the module as the platform layer is.

#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace lumiere
{

// ---------------------------------------------------------------------------
// Canvas size limits (docs/stdlib-lumidessin.md, "Canvas creation")
// ---------------------------------------------------------------------------

inline constexpr int64_t kMaxCanvasDimension = 16384;
inline constexpr int64_t kMaxCanvasPixels = 16'777'216;

double expect_finite(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site);
double expect_non_negative_finite(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site);

void validate_canvas_dimensions(IRuntime &runtime,
                                int64_t width,
                                int64_t height,
                                const std::string &context,
                                const RuntimeSite &site);

// ---------------------------------------------------------------------------
// Couleur -- immutable, straight RGBA8
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

Value make_couleur_value(uint8_t r, uint8_t g, uint8_t b, uint8_t a, const NativeFunctionFactory &make_native_function);
bool is_couleur_object(const Value &value);
const ColorState &expect_couleur(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site);
int64_t expect_color_component(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site);
Value make_erreur_couleur(const std::string &operation, const std::string &value_text, const std::string &cause);
std::optional<uint8_t> parse_hex_byte(char high, char low);

// ---------------------------------------------------------------------------
// Point -- immutable, plain readable x/y fields (not methods)
// ---------------------------------------------------------------------------

Value make_point_value(double x, double y);

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
                       const NativeFunctionFactory &make_native_function);
bool is_image_object(const Value &value);
const ImageState &expect_image(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site);
Value make_erreur_image(const std::string &operation, const std::string &path, const std::string &cause);

// ---------------------------------------------------------------------------
// Canevas -- mutable RGBA8 framebuffer
// ---------------------------------------------------------------------------

// Forward-declared so CanvasState can hold raw, non-owning pointers to the
// crayons drawing on it (see CrayonState below and docs/stdlib-lumidessin.md's
// "Native architecture" -- native-to-native ownership must be acyclic, so
// this direction is never a Ref).
struct CrayonState;

// Opaque handle to the SDL3 window/renderer/texture triple backing a visible
// canvas. Defined only in window.cpp, which is the sole translation unit in
// this module that includes an SDL header -- docs/stdlib-lumidessin.md's
// "Platform backend" section requires the platform layer's types to stay out
// of public Lumière headers, and this pImpl split keeps them out of every
// other .cpp in the module too, not just out of include/. A null
// platform_window means "off-screen, or visible and closed"; CanvasState's
// destructor is declared (not defaulted) below and defined only in
// window.cpp for exactly this reason -- std::unique_ptr's deleter needs
// PlatformWindow complete wherever a CanvasState is destroyed.
struct PlatformWindow;

// Keyboard/mouse/wheel state captured by the most recent prochaine_image()
// call (docs/stdlib-lumidessin.md, "Input"). Keyed by the canonical French
// names the language exposes (touche_enfoncée("a"), bouton_enfoncé("gauche"))
// rather than by a platform scancode/button enum, so this header -- and
// input.cpp, which only ever reads these sets -- never needs to see an SDL
// type. window.cpp is the only place a platform code is translated into one
// of these names, once, while pumping events.
struct InputSnapshot
{
    std::unordered_set<std::string> keys_down;
    std::unordered_set<std::string> keys_pressed;   // transitioned down during the latest pump
    std::unordered_set<std::string> keys_released;  // transitioned up during the latest pump
    std::string text;                                // texte_saisi(): OS-composed text from the latest pump

    double mouse_x = 0.0;
    double mouse_y = 0.0;
    bool mouse_inside = false;
    std::unordered_set<std::string> buttons_down;
    std::unordered_set<std::string> buttons_pressed;
    std::unordered_set<std::string> buttons_released;
    double wheel_x = 0.0;
    double wheel_y = 0.0;
};

struct CanvasState : NativeState
{
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8, row-major, width * height * 4 bytes
    bool open = true;
    bool visible = false;
    // Raw, non-owning, in creation order -- overlay draw order for the
    // visible cursor (stage 7). Each CrayonState registers itself here in
    // its constructor and deregisters in its destructor; see CrayonState's
    // own comment for why a dangling pointer can never appear in this list.
    std::vector<CrayonState *> crayons;

    // ---- Frame lifecycle (docs/stdlib-lumidessin.md, "Frame lifecycle") --
    // Present on every canvas, visible or off-screen: an off-screen canvas
    // still paces frames (régler_cadence/prochaine_image/écart_image), it
    // just has no window to present to or events to pump.
    int frame_rate = 60;          // régler_cadence; 1..240, default 60
    bool frame_started = false;   // false until the first prochaine_image()
    double last_frame_time = 0.0; // monotonic seconds at the most recent call
    double next_deadline = 0.0;   // monotonic seconds prochaine_image should not return before
    double last_elapsed = 0.0;    // écart_image()'s answer; never negative

    // ---- Window (stage 7) and input (stage 8) -- meaningful only for a
    // visible, open canvas. `platform_window` is null off-screen and after
    // fermer(); `close_requested` is set by prochaine_image() when the
    // native window reports a close request, and consumed (canvas closed)
    // before that call returns.
    std::unique_ptr<PlatformWindow> platform_window;
    bool close_requested = false;
    InputSnapshot input;

    // Both declared here, defined only in window.cpp: the pImpl idiom
    // requires PlatformWindow complete for the *constructor* too, not just
    // the destructor -- the compiler-generated body must be able to unwind
    // (destroy platform_window) if a later member's construction throws.
    CanvasState();
    ~CanvasState() override;

    void trace_references(RefVisitor &) const override
    {
    }
    void clear_references() override
    {
    }
};

void expect_canevas_open(IRuntime &runtime, const CanvasState &state, const std::string &context, const RuntimeSite &site);
Value make_canevas_value(int32_t width, int32_t height, bool visible, const NativeFunctionFactory &make_native_function);

// Defined in raster.cpp; called once by make_canevas_value() to add the
// drawing-primitive methods to a freshly constructed canvas object. Split
// out because it is a distinct, substantial responsibility (the rasterizer),
// not because the file layout alone calls for it.
void bind_canevas_drawing_methods(const Ref<LumiereObject> &object,
                                  CanvasState *state,
                                  const NativeFunctionFactory &make_native_function);

// Defined in raster.cpp. Source-over blend of `color` into the pixel at
// (x, y), scaled by `coverage` in [0, 1]. Bounds-checked (a no-op outside
// the canvas), so every caller -- every SDF primitive, and text.cpp's glyph
// compositing -- can call it without its own clip test.
void blend_pixel(CanvasState &canvas, int x, int y, const ColorState &color, double coverage);

// Defined in raster.cpp. Draws one round-capped line segment (a "capsule")
// of `half_thickness * 2` width, the same primitive Canevas.tracer_ligne
// uses. Exposed with external linkage, like blend_pixel above, so
// crayon.cpp can draw a crayon's movement trail without a second stroke
// implementation.
void raster_capsule(CanvasState &canvas, double ax, double ay, double bx, double by, double half_thickness,
                    const ColorState &color);

// ---------------------------------------------------------------------------
// Dimensions -- immutable value type with plain readable fields (not
// methods), returned by mesurer_texte().
// ---------------------------------------------------------------------------

Value make_dimensions_value(int32_t width, int32_t height);

// ---------------------------------------------------------------------------
// Text -- defined in text.cpp; called once by make_canevas_value() to add
// dessiner_texte/mesurer_texte to a freshly constructed canvas object.
// ---------------------------------------------------------------------------

void bind_canevas_text_methods(const Ref<LumiereObject> &object,
                               CanvasState *state,
                               const NativeFunctionFactory &make_native_function);

// ---------------------------------------------------------------------------
// Images -- defined in image.cpp. bind_canevas_image_methods() adds
// enregistrer_png/dessiner_image/dessiner_image_redimensionnée/
// dessiner_image_nette to a freshly constructed canvas object;
// load_png_image() backs the module-level charger_image().
// ---------------------------------------------------------------------------

void bind_canevas_image_methods(const Ref<LumiereObject> &object,
                                CanvasState *state,
                                const NativeFunctionFactory &make_native_function);

Value load_png_image(IRuntime &runtime,
                     const std::filesystem::path &path,
                     const NativeFunctionFactory &make_native_function,
                     const RuntimeSite &site);

// ---------------------------------------------------------------------------
// Crayon -- turtle-style cursor bound to one canvas. Defined in crayon.cpp;
// bind_canevas_crayon_methods() adds crayon() to a freshly constructed
// canvas object. See docs/stdlib-lumidessin.md's "Crayon API" and "Native
// architecture" sections for the coordinate mapping and ownership contract
// this state implements.
// ---------------------------------------------------------------------------

struct CrayonState : NativeState
{
    // CrayonState strongly owns the canvas it draws on: this Ref is what
    // keeps the canvas alive for as long as any crayon referencing it
    // exists, even past the canvas's own Lumière value going out of scope.
    // fermer() is orthogonal -- it clears the framebuffer and the open flag
    // but does not destroy the CanvasState -- so every method below must
    // still check expect_canevas_open() itself.
    explicit CrayonState(Ref<CanvasState> canvas_ref) : canvas(std::move(canvas_ref))
    {
        canvas->crayons.push_back(this);
    }

    ~CrayonState() override
    {
        std::vector<CrayonState *> &siblings = canvas->crayons;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }

    Ref<CanvasState> canvas;

    // Crayon-space state (docs/stdlib-lumidessin.md, "Crayon API"): origin
    // at the canvas center, +x right, +y up, headings counterclockwise from
    // 0 degrees pointing right -- the opposite y and rotation sense from
    // direct canvas drawing, converted to canvas pixels only at draw time.
    double x = 0.0;
    double y = 0.0;
    double heading_deg = 0.0;
    bool pen_down = true;
    uint8_t color_r = 0, color_g = 0, color_b = 0, color_a = 255; // starts black
    double thickness = 1.0;
    bool visible = true;

    // No Value is ever stored here, so there is nothing for the cycle
    // collector to trace; canvas is a plain Ref<CanvasState>, not a boxed
    // Value, so it falls outside trace_references/clear_references
    // entirely (see values.cpp's comment on why Crayon needs no
    // native_captures either, for the same reason).
    void trace_references(RefVisitor &) const override
    {
    }
    void clear_references() override
    {
    }
};

// Unlike the sibling bind_canevas_*_methods functions, this one only adds
// crayon() -- creating a Crayon needs a strong Ref<CanvasState> (see
// CrayonState above), which crayon()'s own closure constructs from `state`
// via Ref<CanvasState>(state) at call time, retaining a live raw pointer
// the same way every other Canevas method closure already does.
void bind_canevas_crayon_methods(const Ref<LumiereObject> &object,
                                 CanvasState *state,
                                 const NativeFunctionFactory &make_native_function);

Value make_crayon_value(Ref<CanvasState> canvas, const NativeFunctionFactory &make_native_function);

// ---------------------------------------------------------------------------
// Window and frame lifecycle -- stage 7. Defined in window.cpp, the only
// translation unit in this module that includes an SDL3 header, and the
// only one whose behavior differs under LUMIERE_ENABLE_LUMIDESSIN_WINDOW:
// with it off, these functions still exist and still compile everywhere
// else unchanged, but fenêtre() raises the documented availability error
// instead of ever creating a window. docs/stdlib-lumidessin.md, "Frame
// lifecycle" and "Platform backend".
// ---------------------------------------------------------------------------

// Creates the single visible canvas. Raises a runtime error if a visible
// canvas is already open, the window system is unavailable (including
// window support having been compiled out), or creation fails.
Value make_fenetre_value(IRuntime &runtime,
                         int32_t width,
                         int32_t height,
                         const std::string &title,
                         const NativeFunctionFactory &make_native_function,
                         const RuntimeSite &site);

// Adds régler_cadence/prochaine_image/écart_image/présenter/
// attendre_fermeture to a freshly constructed canvas object -- visible or
// off-screen, since cadence timing applies to both.
void bind_canevas_frame_methods(const Ref<LumiereObject> &object,
                                CanvasState *state,
                                const NativeFunctionFactory &make_native_function);

// Destroys `state`'s platform window, if any, and clears its close
// callback. Idempotent. Called by Canevas.fermer(), by ~CanvasState(), and
// by attendre_fermeture() once the window closes.
void release_platform_window(CanvasState &state);

// Pure and sleep-free: given whether this is the canvas's first frame, the
// current monotonic time, the previously recorded deadline, and the frame
// interval, returns how long prochaine_image() should still wait (zero if
// the deadline already passed -- a missed deadline is abandoned, never made
// up) and the "now" the frame should be timed from. Exposed here, rather
// than kept local to window.cpp, specifically so a unit test can exercise
// the frame-pacing rule without a real clock or a real sleep
// (docs/stdlib-lumidessin.md, "Testing contract": "frame-deadline
// calculation without real sleeps").
struct FrameWait
{
    double wait_seconds;
    double frame_now;
};
// interval_seconds is not a parameter: the deadline already encodes it
// (the caller advances next_deadline by the interval after each call),
// so this function only ever needs to compare `now` against it.
FrameWait compute_frame_wait(bool first_call, double now, double next_deadline);

// ---------------------------------------------------------------------------
// Input -- stage 8. Defined in input.cpp, which only ever reads
// CanvasState::input (see InputSnapshot above) and therefore needs no SDL
// dependency of its own; window.cpp is what populates it.
// ---------------------------------------------------------------------------

// Adds touche_enfoncée/touche_pressée/touche_relâchée/texte_saisi/
// position_souris/souris_présente/bouton_enfoncé/bouton_pressé/
// bouton_relâché/défilement to a freshly constructed canvas object.
void bind_canevas_input_methods(const Ref<LumiereObject> &object,
                                CanvasState *state,
                                const NativeFunctionFactory &make_native_function);

// The canonical key and mouse-button names documented under "Input", used
// both to bind the methods above and by a conformance/unit test enumerating
// them. An unrecognized name is a runtime error, never a silent false.
const std::unordered_set<std::string> &canonical_key_names();
const std::unordered_set<std::string> &canonical_button_names();

} // namespace lumiere
