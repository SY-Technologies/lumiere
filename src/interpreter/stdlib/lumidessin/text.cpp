// Text measurement and drawing (docs/stdlib-lumidessin.md, "Canevas API ->
// Text"). Rasterizes the bundled Inter-Regular font with stb_truetype and
// composites glyph coverage through raster.cpp's blend_pixel, so text uses
// exactly the same source-over path as every other drawing primitive.
//
// Missing glyphs: docs/stdlib-lumidessin.md says a missing glyph renders as
// "the replacement glyph". The bundled font (Inter-Regular.ttf) has no cmap
// entry for U+FFFD itself -- most fonts don't -- so this does not mean
// "look up U+FFFD in the cmap". stbtt_FindGlyphIndex already returns glyph
// index 0 (.notdef) for any codepoint the font doesn't map, and Inter's
// .notdef is a real drawn glyph (a hollow box), not empty -- so rasterizing
// whatever stbtt_FindGlyphIndex returns, unconditionally, already produces
// the documented behavior with no special-casing.

#include "lumiere/parser/utf8.hpp"
#include "state.hpp"

#include "lumidessin_font.generated.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lumiere
{

namespace
{

// ---------------------------------------------------------------------------
// The bundled font is parsed once, lazily, the first time any text method
// runs. The embedded byte array has static storage duration (it is a
// `constexpr` array in a generated header), so the stbtt_fontinfo's internal
// pointers into it stay valid for the life of the process.
// ---------------------------------------------------------------------------

struct BundledFont
{
    stbtt_fontinfo info{};
    bool ready = false;
};

const BundledFont &bundled_font()
{
    static const BundledFont font = [] {
        BundledFont f;
        f.ready = stbtt_InitFont(
                      &f.info, INTER_REGULAR_TTF, stbtt_GetFontOffsetForIndex(INTER_REGULAR_TTF, 0)) != 0;
        return f;
    }();
    return font;
}

// stbtt_InitFont can only fail on a malformed font file. The embedded font
// is a fixed build-time asset, not user input, so this is not a reachable
// runtime state -- but native code must never assume a C library call
// succeeded silently (docs/stdlib-lumidessin.md's "Failure policy"), so a
// corrupt embed still surfaces as a Lumière runtime error instead of
// rasterizing garbage or reading out of bounds.
void expect_font_ready(IRuntime &runtime, const std::string &context, const RuntimeSite &site)
{
    if (!bundled_font().ready)
    {
        runtime.raise_runtime_error(site, context + " ne peut pas charger la police intégrée");
    }
}

void expect_text_size(IRuntime &runtime, int64_t taille, const std::string &context, const RuntimeSite &site)
{
    if (taille < 1 || taille > 1024)
    {
        runtime.raise_runtime_error(site, context + " attend une taille entre 1 et 1024");
    }
}

// One glyph, already positioned along its line: `pen_x` is the pen position
// (in pixels, relative to the line's own left edge) at which this glyph's
// origin sits.
struct GlyphRun
{
    int glyph_index;
    double pen_x;
};

struct LineRun
{
    std::vector<GlyphRun> glyphs;
    double width = 0.0;
};

double glyph_advance(const stbtt_fontinfo &font, int glyph_index, float scale)
{
    int advance_width = 0;
    int left_side_bearing = 0;
    stbtt_GetGlyphHMetrics(&font, glyph_index, &advance_width, &left_side_bearing);
    return advance_width * static_cast<double>(scale);
}

// Newlines start new lines; tabs advance to the next four-space tab stop
// (docs/stdlib-lumidessin.md, "Text"). mesurer_texte and dessiner_texte both
// call this, so their notion of layout can never disagree.
std::vector<LineRun> layout_text(IRuntime &runtime, const stbtt_fontinfo &font, const std::string &text, float scale,
                                 const std::string &context, const RuntimeSite &site)
{
    const int space_glyph = stbtt_FindGlyphIndex(&font, ' ');
    // Guarded away from zero: a tab must always make forward progress, even
    // in the theoretical case of a font whose space glyph has no advance.
    const double tab_stop_width =
        std::max(4.0 * glyph_advance(font, space_glyph, scale), static_cast<double>(scale));

    std::vector<LineRun> lines;
    LineRun current;
    double pen_x = 0.0;
    int previous_glyph = -1;

    const auto flush_line = [&] {
        current.width = pen_x;
        lines.push_back(std::move(current));
        current = LineRun{};
        pen_x = 0.0;
        previous_glyph = -1;
    };

    std::size_t offset = 0;
    while (offset < text.size())
    {
        char32_t character = 0;
        const auto next = utf8::decode_one(text, offset, character);
        if (!next)
        {
            runtime.raise_runtime_error(site, context + " attend un texte UTF-8 valide");
        }
        offset = *next;

        if (character == U'\n')
        {
            flush_line();
            continue;
        }
        if (character == U'\t')
        {
            const double stops = std::floor(pen_x / tab_stop_width) + 1.0;
            pen_x = stops * tab_stop_width;
            previous_glyph = -1;
            continue;
        }

        const int glyph_index = stbtt_FindGlyphIndex(&font, static_cast<int>(character));
        if (previous_glyph >= 0)
        {
            pen_x += stbtt_GetGlyphKernAdvance(&font, previous_glyph, glyph_index) * static_cast<double>(scale);
        }
        current.glyphs.push_back({glyph_index, pen_x});
        pen_x += glyph_advance(font, glyph_index, scale);
        previous_glyph = glyph_index;
    }
    flush_line();
    return lines;
}

double line_height_px(const stbtt_fontinfo &font, float scale, double *out_ascent_px = nullptr)
{
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    if (out_ascent_px != nullptr)
    {
        *out_ascent_px = ascent * static_cast<double>(scale);
    }
    // Guarded away from zero for the same reason as the tab stop above: a
    // font whose own metrics collapse to nothing must not collapse every
    // line onto the one above it.
    return std::max((ascent - descent + line_gap) * static_cast<double>(scale), static_cast<double>(scale));
}

} // namespace

void bind_canevas_text_methods(const Ref<LumiereObject> &object, CanvasState *state,
                               const NativeFunctionFactory &make_native_function)
{
    object->fields["mesurer_texte"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Canevas.mesurer_texte", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.mesurer_texte", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "Canevas.mesurer_texte", native_args.site);
            const int64_t taille =
                stdlib_expect_integer(runtime, args[1].value, "Canevas.mesurer_texte", native_args.site);
            expect_text_size(runtime, taille, "Canevas.mesurer_texte", native_args.site);
            expect_font_ready(runtime, "Canevas.mesurer_texte", native_args.site);

            const auto &font = bundled_font().info;
            const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(taille));
            const auto lines =
                layout_text(runtime, font, text, scale, "Canevas.mesurer_texte", native_args.site);
            const double height = line_height_px(font, scale);

            double max_width = 0.0;
            for (const LineRun &line : lines)
            {
                max_width = std::max(max_width, line.width);
            }
            const int32_t width_px = static_cast<int32_t>(std::ceil(max_width));
            const int32_t height_px = static_cast<int32_t>(std::ceil(height * static_cast<double>(lines.size())));
            return make_dimensions_value(width_px, height_px);
        }));

    object->fields["dessiner_texte"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 5, "Canevas.dessiner_texte", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.dessiner_texte", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "Canevas.dessiner_texte", native_args.site);
            const double x = expect_finite(runtime, args[1].value, "Canevas.dessiner_texte", native_args.site);
            const double y = expect_finite(runtime, args[2].value, "Canevas.dessiner_texte", native_args.site);
            const int64_t taille =
                stdlib_expect_integer(runtime, args[3].value, "Canevas.dessiner_texte", native_args.site);
            expect_text_size(runtime, taille, "Canevas.dessiner_texte", native_args.site);
            const ColorState &color = expect_couleur(runtime, args[4].value, "Canevas.dessiner_texte", native_args.site);
            expect_font_ready(runtime, "Canevas.dessiner_texte", native_args.site);

            const auto &font = bundled_font().info;
            const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(taille));
            const auto lines =
                layout_text(runtime, font, text, scale, "Canevas.dessiner_texte", native_args.site);
            double ascent_px = 0.0;
            const double height = line_height_px(font, scale, &ascent_px);

            for (std::size_t line_index = 0; line_index < lines.size(); ++line_index)
            {
                const double baseline_y = y + static_cast<double>(line_index) * height + ascent_px;
                for (const GlyphRun &glyph : lines[line_index].glyphs)
                {
                    int bitmap_width = 0;
                    int bitmap_height = 0;
                    int x_offset = 0;
                    int y_offset = 0;
                    unsigned char *bitmap = stbtt_GetGlyphBitmap(
                        &font, scale, scale, glyph.glyph_index, &bitmap_width, &bitmap_height, &x_offset, &y_offset);
                    if (bitmap == nullptr)
                    {
                        continue; // Whitespace and other zero-ink glyphs produce no bitmap.
                    }
                    const int dest_x = static_cast<int>(std::lround(x + glyph.pen_x)) + x_offset;
                    const int dest_y = static_cast<int>(std::lround(baseline_y)) + y_offset;
                    for (int py = 0; py < bitmap_height; ++py)
                    {
                        for (int px = 0; px < bitmap_width; ++px)
                        {
                            const double coverage = bitmap[py * bitmap_width + px] / 255.0;
                            if (coverage > 0.0)
                            {
                                blend_pixel(*state, dest_x + px, dest_y + py, color, coverage);
                            }
                        }
                    }
                    stbtt_FreeBitmap(bitmap, nullptr);
                }
            }
            return Value::rien();
        }));
}

} // namespace lumiere
