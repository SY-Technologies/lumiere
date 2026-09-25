// PNG image I/O and image drawing (docs/stdlib-lumidessin.md, "Images" and
// "Canevas API -> Images"). Decoding and encoding go through stb_image.h /
// stb_image_write.h; drawing composites through raster.cpp's blend_pixel,
// the same source-over path every other primitive uses.

#include "state.hpp"

// Only this translation unit defines these -- exactly once across the
// binary, same convention as text.cpp's STB_TRUETYPE_IMPLEMENTATION.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <vector>

namespace lumiere
{

namespace
{

// ---------------------------------------------------------------------------
// Path validation -- the same component-wise '..' rule
// docs/stdlib-lumidessin.md's "Images" section points to (fichier.cpp's own
// sanitize_path is file-local, so this mirrors it rather than sharing it,
// same as this module's other small cross-cutting helpers).
// ---------------------------------------------------------------------------

void sanitize_image_path(IRuntime &runtime, const std::filesystem::path &path, const std::string &context,
                         const RuntimeSite &site)
{
    for (const auto &component : path)
    {
        if (component == "..")
        {
            runtime.raise_runtime_error(site, context + " rejette les chemins contenant '..'");
        }
    }
}

constexpr unsigned char kPngSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

Value load_png_image_impl(IRuntime &runtime, const std::filesystem::path &path,
                          const NativeFunctionFactory &make_native_function, const RuntimeSite &site)
{
    sanitize_image_path(runtime, path, "LumiDessin.charger_image", site);

    const std::string path_text = path.string();
    const auto fail = [&](const std::string &cause) {
        return stdlib_failure(make_erreur_image("charger_image", path_text, cause), site);
    };

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return fail("impossible d'ouvrir le fichier");
    }
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad())
    {
        return fail("échec pendant la lecture");
    }
    file.close();

    if (bytes.size() < sizeof(kPngSignature) ||
        !std::equal(std::begin(kPngSignature), std::end(kPngSignature), bytes.begin()))
    {
        return fail("format non pris en charge");
    }

    // Reject a hostile/oversized image before stb_image ever allocates a
    // decode buffer for it (docs/stdlib-lumidessin.md's "Rendering core":
    // "check dimensions ... before allocation").
    int width = 0;
    int height = 0;
    int source_channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &source_channels))
    {
        return fail("données PNG invalides");
    }
    if (width < 1 || height < 1 || width > kMaxCanvasDimension || height > kMaxCanvasDimension ||
        static_cast<int64_t>(width) * static_cast<int64_t>(height) > kMaxCanvasPixels)
    {
        return fail("dimensions hors limites");
    }

    int decoded_width = 0;
    int decoded_height = 0;
    int decoded_channels = 0;
    unsigned char *decoded = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()), &decoded_width, &decoded_height, &decoded_channels, 4);
    if (decoded == nullptr)
    {
        return fail("données PNG invalides");
    }

    std::vector<uint8_t> pixels(
        decoded, decoded + static_cast<std::size_t>(decoded_width) * static_cast<std::size_t>(decoded_height) * 4);
    stbi_image_free(decoded);

    Value image = make_image_value(decoded_width, decoded_height, std::move(pixels), make_native_function);
    return stdlib_success(std::move(image));
}

// ---------------------------------------------------------------------------
// Encoding -- write beside the destination, then atomically replace it
// (docs/stdlib-lumidessin.md, "Framebuffer operations"): a reader can never
// observe a partially-written file at the destination path.
// ---------------------------------------------------------------------------

struct PngBuffer
{
    std::vector<unsigned char> bytes;
};

void append_png_bytes(void *context, void *data, int size)
{
    auto *buffer = static_cast<PngBuffer *>(context);
    const auto *bytes = static_cast<unsigned char *>(data);
    buffer->bytes.insert(buffer->bytes.end(), bytes, bytes + size);
}

std::filesystem::path make_sibling_temp_path(const std::filesystem::path &destination)
{
    std::random_device random_device;
    std::mt19937_64 engine(random_device());
    std::uniform_int_distribution<uint64_t> distribution;
    return destination.parent_path() /
           (destination.filename().string() + ".tmp" + std::to_string(distribution(engine)));
}

Value save_png_impl(CanvasState &canvas, const std::filesystem::path &path, const RuntimeSite &site)
{
    const std::string path_text = path.string();
    const auto fail = [&](const std::string &cause) {
        return stdlib_failure(make_erreur_image("enregistrer_png", path_text, cause), site);
    };

    PngBuffer buffer;
    const int ok = stbi_write_png_to_func(
        append_png_bytes, &buffer, canvas.width, canvas.height, 4, canvas.pixels.data(), canvas.width * 4);
    if (!ok)
    {
        return fail("échec de l'encodage PNG");
    }

    const std::filesystem::path temp_path = make_sibling_temp_path(path);
    {
        std::ofstream temp_file(temp_path, std::ios::binary | std::ios::trunc);
        if (!temp_file.is_open())
        {
            return fail("impossible d'ouvrir le fichier");
        }
        temp_file.write(reinterpret_cast<const char *>(buffer.bytes.data()), static_cast<std::streamsize>(buffer.bytes.size()));
        temp_file.close();
        if (temp_file.fail())
        {
            std::error_code remove_error;
            std::filesystem::remove(temp_path, remove_error);
            return fail("échec pendant l'écriture");
        }
    }

    std::error_code rename_error;
    std::filesystem::rename(temp_path, path, rename_error);
    if (rename_error)
    {
        std::error_code remove_error;
        std::filesystem::remove(temp_path, remove_error);
        return fail("échec du remplacement atomique du fichier");
    }

    return stdlib_success(Value::rien());
}

// ---------------------------------------------------------------------------
// Drawing -- dessiner_image (1:1), dessiner_image_redimensionnée
// (bilinear), dessiner_image_nette (nearest-neighbor)
// ---------------------------------------------------------------------------

ColorState sample_source_pixel(const ImageState &image, int x, int y)
{
    ColorState pixel;
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4;
    pixel.r = image.pixels[offset + 0];
    pixel.g = image.pixels[offset + 1];
    pixel.b = image.pixels[offset + 2];
    pixel.a = image.pixels[offset + 3];
    return pixel;
}

ColorState sample_nearest(const ImageState &image, double fx, double fy)
{
    const int sx = std::clamp(static_cast<int>(std::floor(fx)), 0, image.width - 1);
    const int sy = std::clamp(static_cast<int>(std::floor(fy)), 0, image.height - 1);
    return sample_source_pixel(image, sx, sy);
}

// Bilinear sampling in premultiplied space: interpolating straight alpha
// directly would bleed the arbitrary RGB of a fully transparent neighbor
// into an edge, which premultiplying before the lerp (and dividing back out
// after) avoids.
ColorState sample_bilinear(const ImageState &image, double fx, double fy)
{
    const double clamped_x = std::clamp(fx, 0.0, static_cast<double>(image.width - 1));
    const double clamped_y = std::clamp(fy, 0.0, static_cast<double>(image.height - 1));
    const int x0 = static_cast<int>(std::floor(clamped_x));
    const int y0 = static_cast<int>(std::floor(clamped_y));
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const double tx = clamped_x - x0;
    const double ty = clamped_y - y0;

    struct Premultiplied
    {
        double r, g, b, a;
    };
    const auto premultiply = [&](int x, int y) -> Premultiplied {
        const ColorState px = sample_source_pixel(image, x, y);
        const double a = px.a / 255.0;
        return {px.r / 255.0 * a, px.g / 255.0 * a, px.b / 255.0 * a, a};
    };
    const Premultiplied p00 = premultiply(x0, y0);
    const Premultiplied p10 = premultiply(x1, y0);
    const Premultiplied p01 = premultiply(x0, y1);
    const Premultiplied p11 = premultiply(x1, y1);

    const auto lerp2d = [&](double v00, double v10, double v01, double v11) {
        const double top = v00 + (v10 - v00) * tx;
        const double bottom = v01 + (v11 - v01) * tx;
        return top + (bottom - top) * ty;
    };
    const double a = lerp2d(p00.a, p10.a, p01.a, p11.a);
    const double pr = lerp2d(p00.r, p10.r, p01.r, p11.r);
    const double pg = lerp2d(p00.g, p10.g, p01.g, p11.g);
    const double pb = lerp2d(p00.b, p10.b, p01.b, p11.b);

    ColorState result;
    const auto to_byte = [](double v) {
        return static_cast<uint8_t>(std::clamp(std::floor(v * 255.0 + 0.5), 0.0, 255.0));
    };
    result.a = to_byte(a);
    if (a <= 0.0)
    {
        result.r = result.g = result.b = 0;
    }
    else
    {
        result.r = to_byte(pr / a);
        result.g = to_byte(pg / a);
        result.b = to_byte(pb / a);
    }
    return result;
}

void draw_image_unscaled(CanvasState &canvas, const ImageState &image, double x, double y)
{
    const int dest_x0 = static_cast<int>(std::lround(x));
    const int dest_y0 = static_cast<int>(std::lround(y));
    for (int sy = 0; sy < image.height; ++sy)
    {
        for (int sx = 0; sx < image.width; ++sx)
        {
            const ColorState pixel = sample_source_pixel(image, sx, sy);
            blend_pixel(canvas, dest_x0 + sx, dest_y0 + sy, pixel, 1.0);
        }
    }
}

template <typename Sampler>
void draw_image_scaled(CanvasState &canvas, const ImageState &image, double x, double y, double width, double height,
                       double opacity, Sampler &&sample)
{
    const int x0 = std::max(0, static_cast<int>(std::floor(x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(y)));
    const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(x + width)) - 1);
    const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(y + height)) - 1);
    for (int py = y0; py <= y1; ++py)
    {
        for (int px = x0; px <= x1; ++px)
        {
            const double fx = (px + 0.5 - x) / width * image.width - 0.5;
            const double fy = (py + 0.5 - y) / height * image.height - 0.5;
            const ColorState pixel = sample(image, fx, fy);
            blend_pixel(canvas, px, py, pixel, opacity);
        }
    }
}

} // namespace

Value load_png_image(IRuntime &runtime, const std::filesystem::path &path,
                     const NativeFunctionFactory &make_native_function, const RuntimeSite &site)
{
    return load_png_image_impl(runtime, path, make_native_function, site);
}

void bind_canevas_image_methods(const Ref<LumiereObject> &object, CanvasState *state,
                                const NativeFunctionFactory &make_native_function)
{
    object->fields["enregistrer_png"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto path = stdlib_expect_path_arg(runtime, args, "Canevas.enregistrer_png", native_args.site);
            sanitize_image_path(runtime, path, "Canevas.enregistrer_png", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.enregistrer_png", native_args.site);
            return save_png_impl(*state, path, native_args.site);
        }));

    object->fields["dessiner_image"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 3, "Canevas.dessiner_image", native_args.site);
            expect_canevas_open(runtime, *state, "Canevas.dessiner_image", native_args.site);
            const ImageState &image = expect_image(runtime, args[0].value, "Canevas.dessiner_image", native_args.site);
            const double x = expect_finite(runtime, args[1].value, "Canevas.dessiner_image", native_args.site);
            const double y = expect_finite(runtime, args[2].value, "Canevas.dessiner_image", native_args.site);
            draw_image_unscaled(*state, image, x, y);
            return Value::rien();
        }));

    const auto bind_scaled = [&](const char *name, auto sampler) {
        object->fields[name] = Value::fonction(make_native_function(
            [state, name, sampler](IRuntime &runtime, const NativeArgs &native_args) -> Value {
                const auto &args = *native_args.arguments;
                const std::string context = std::string("Canevas.") + name;
                stdlib_expect_positional(runtime, args, 6, context, native_args.site);
                expect_canevas_open(runtime, *state, context, native_args.site);
                const ImageState &image = expect_image(runtime, args[0].value, context, native_args.site);
                const double x = expect_finite(runtime, args[1].value, context, native_args.site);
                const double y = expect_finite(runtime, args[2].value, context, native_args.site);
                const double width = expect_non_negative_finite(runtime, args[3].value, context, native_args.site);
                const double height = expect_non_negative_finite(runtime, args[4].value, context, native_args.site);
                const double opacity = expect_finite(runtime, args[5].value, context, native_args.site);
                if (opacity < 0.0 || opacity > 1.0)
                {
                    runtime.raise_runtime_error(native_args.site, context + " attend une opacité entre 0.0 et 1.0");
                }
                if (width > 0.0 && height > 0.0)
                {
                    draw_image_scaled(*state, image, x, y, width, height, opacity, sampler);
                }
                return Value::rien();
            }));
    };
    bind_scaled("dessiner_image_redimensionnée", sample_bilinear);
    bind_scaled("dessiner_image_nette", sample_nearest);
}

} // namespace lumiere
