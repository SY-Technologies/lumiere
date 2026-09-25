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

} // namespace lumiere
