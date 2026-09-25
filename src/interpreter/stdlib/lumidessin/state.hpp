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
#include <optional>
#include <string>
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

} // namespace lumiere
