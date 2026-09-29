/// @file
/// @brief Image implementation: PNG, JPEG and WebP, each read by the one
///        library that already knows the format, into the one shape
///        everything past this file expects.
#include "render/graphics/image.hpp"

#include <cstdio>
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>

#include <csetjmp>
#include <cstring>
#include <initializer_list>

namespace render::graphics {

    [[nodiscard]] static bool signature(
        const std::span<const std::byte> bytes,
        const std::initializer_list<unsigned char> want
    ) noexcept {
        if (bytes.size() < want.size()) return false;
        return std::memcmp(bytes.data(), want.begin(), want.size()) == 0;
    }

    [[nodiscard]] static bool riff(const std::span<const std::byte> bytes) noexcept {
        if (bytes.size() < 12) return false;
        return std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
               std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
    }

    [[nodiscard]] static std::optional<Image> png(const std::span<const std::byte> bytes) {
        png_image source{};
        source.version = PNG_IMAGE_VERSION;

        if (!png_image_begin_read_from_memory(&source, bytes.data(), bytes.size())) {
            return std::nullopt;
        }

        source.format = PNG_FORMAT_RGB;
        std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(source));

        if (!png_image_finish_read(&source, nullptr, pixels.data(), 0, nullptr)) {
            png_image_free(&source);
            return std::nullopt;
        }

        const int width = static_cast<int>(source.width);
        const int height = static_cast<int>(source.height);
        png_image_free(&source);

        // Its resolution, from the pHYs chunk before the pixels when there is
        // one in pixels to the metre: each chunk its length, its type, its
        // data and a check, the first of them eight bytes in.
        float resolution = 72.0f;
        const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
        const auto word = [data](const std::size_t at) {
            return (std::uint32_t{data[at]} << 24) | (std::uint32_t{data[at + 1]} << 16) |
                   (std::uint32_t{data[at + 2]} << 8) | std::uint32_t{data[at + 3]};
        };
        for (std::size_t at = 8; at + 8 <= bytes.size();) {
            const std::uint32_t length = word(at);
            if (std::memcmp(data + at + 4, "IDAT", 4) == 0 || at + 12 + length > bytes.size()) break;
            if (std::memcmp(data + at + 4, "pHYs", 4) == 0 && length == 9 && data[at + 16] == 1) {
                if (const std::uint32_t across = word(at + 8); across > 0) resolution = static_cast<float>(across) * 0.0254f;
                break;
            }
            at += 12 + length;
        }

        return Image(width, height, std::move(pixels), {}, 3, resolution);
    }

    // libjpeg's error handling is setjmp/longjmp by design, which is
    // exactly as safe as it looks here and nowhere else: nothing in
    // scope across the jump owns a resource a destructor would need to
    // release, since Failure and jpeg_decompress_struct are both plain
    // C structs. MSVC's two warnings about the pattern itself, not
    // about anything actually wrong here, are silenced for this one
    // struct and function rather than for the file, so a real one
    // elsewhere still surfaces.
    #if defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable: 4324 4611)
    #endif

    /// @brief libjpeg's own error path: a longjmp back out of the
    ///        callback it insists on calling rather than returning from.
    struct Failure {
        jpeg_error_mgr handler{};
        jmp_buf point{};
    };

    static void abandon(j_common_ptr info) {
        longjmp(reinterpret_cast<Failure*>(info->err)->point, 1);
    }

    [[nodiscard]] static std::optional<Image> jpeg(const std::span<const std::byte> bytes) {
        jpeg_decompress_struct info{};
        Failure failure;
        info.err = jpeg_std_error(&failure.handler);
        failure.handler.error_exit = abandon;

        if (setjmp(failure.point)) {
            jpeg_destroy_decompress(&info);
            return std::nullopt;
        }

        jpeg_create_decompress(&info);
        jpeg_mem_src(&info, reinterpret_cast<const unsigned char*>(bytes.data()),
                    static_cast<unsigned long>(bytes.size()));
        jpeg_read_header(&info, TRUE);

        // Its resolution from the JFIF header, in dots to the inch or to the
        // centimetre; 72 when it gives none.
        float resolution = 72.0f;
        if (info.saw_JFIF_marker && info.X_density > 0 && info.density_unit != 0) {
            resolution = static_cast<float>(info.X_density) * (info.density_unit == 2 ? 2.54f : 1.0f);
        }

        // In grey or in colour, a PDF reads the file itself: the header is
        // all that needs reading here, and the bytes are kept as they came.
        // CMYK and YCCK files, which PDF readers disagree about, are decoded.
        const J_COLOR_SPACE space = info.jpeg_color_space;
        if ((space == JCS_GRAYSCALE && info.num_components == 1) ||
            ((space == JCS_YCbCr || space == JCS_RGB) && info.num_components == 3)) {
            const int width = static_cast<int>(info.image_width);
            const int height = static_cast<int>(info.image_height);
            const int components = info.num_components;
            jpeg_destroy_decompress(&info);
            const auto* first = reinterpret_cast<const std::uint8_t*>(bytes.data());
            return Image(width, height, {}, std::vector<std::uint8_t>(first, first + bytes.size()), components,
                         resolution);
        }

        info.out_color_space = JCS_RGB;
        jpeg_start_decompress(&info);

        const int width = static_cast<int>(info.output_width);
        const int height = static_cast<int>(info.output_height);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 3);

        std::vector<JSAMPROW> rows(height);
        for (int row = 0; row < height; ++row) {
            rows[static_cast<std::size_t>(row)] = pixels.data() + static_cast<std::size_t>(row) * width * 3;
        }

        while (info.output_scanline < info.output_height) {
            jpeg_read_scanlines(&info, rows.data() + info.output_scanline,
                                info.output_height - info.output_scanline);
        }

        jpeg_finish_decompress(&info);
        jpeg_destroy_decompress(&info);

        return Image(width, height, std::move(pixels), {}, 3, resolution);
    }

    #if defined(_MSC_VER)
        #pragma warning(pop)
    #endif

    [[nodiscard]] static std::optional<Image> webp(const std::span<const std::byte> bytes) {
        int width = 0;
        int height = 0;
        std::uint8_t* decoded = WebPDecodeRGB(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &width, &height);
        if (!decoded) return std::nullopt;

        std::vector<std::uint8_t> pixels(
            decoded, decoded + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
        WebPFree(decoded);

        return Image(width, height, std::move(pixels));
    }

    std::optional<Image> Image::decode(const std::span<const std::byte> bytes) {
        if (signature(bytes, {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A})) return png(bytes);
        if (signature(bytes, {0xFF, 0xD8, 0xFF})) return jpeg(bytes);
        if (riff(bytes)) return webp(bytes);
        return std::nullopt;
    }

}
