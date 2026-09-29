#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace render::graphics {

    /// @brief A raster image: plain RGB pixels, top row first -- or, for a
    ///        JPEG, its own bytes, which a PDF carries as they are.
    ///
    /// Whatever format the bytes arrived in -- PNG, JPEG or WebP -- the
    /// result here is the same shape, which is the whole point: everything
    /// past decode() treats an image as a width, a height, and three bytes
    /// per pixel, and never has to know which library read the file. The one
    /// exception is a JPEG in grey or in colour: a PDF reads those itself, so
    /// only its header is read here, and its bytes kept whole in place of
    /// pixels -- nothing decoded, nothing compressed again, nothing lost.
    class Image {
    public:
        /// @brief Decodes a PNG, JPEG or WebP file's bytes.
        ///
        /// The format is read from the bytes themselves, not guessed from a
        /// file name, since a fetched URL may not have an extension at all.
        ///
        /// @param bytes A whole file's contents, as \\includegraphics's
        ///               source read them.
        /// @return The image, or nullopt when the bytes match none of the
        ///         three signatures, or the matching library rejects them.
        [[nodiscard]] static std::optional<Image> decode(std::span<const std::byte> bytes);

        [[nodiscard]] int width() const noexcept { return width_; }
        [[nodiscard]] int height() const noexcept { return height_; }

        /// @brief The pixels, row-major, top row first, three bytes each;
        ///        empty when the image is kept encoded().
        [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept { return pixels_; }

        /// @brief A JPEG's own bytes, kept whole for a PDF to decode; empty
        ///        for an image kept as pixels().
        [[nodiscard]] const std::vector<std::uint8_t>& encoded() const noexcept { return encoded_; }

        /// @brief How many colour components encoded() holds: 1 for grey, 3
        ///        for colour.
        [[nodiscard]] int components() const noexcept { return components_; }

        /// @brief How many of its pixels make an inch, as the file says:
        ///        PNG's pHYs, JPEG's JFIF density. 72 when it says nothing,
        ///        so a pixel is a point, as pdfTeX has it.
        [[nodiscard]] float resolution() const noexcept { return resolution_; }

        /// @param width      Pixels across.
        /// @param height     Pixels down.
        /// @param pixels     Three bytes a pixel, or none when @p encoded holds it.
        /// @param encoded    A JPEG's bytes, or none.
        /// @param components The colour components @p encoded holds.
        /// @param resolution Pixels to the inch.
        Image(int width, int height, std::vector<std::uint8_t> pixels, std::vector<std::uint8_t> encoded = {},
              int components = 3, float resolution = 72.0f) noexcept
            : width_(width), height_(height), pixels_(std::move(pixels)), encoded_(std::move(encoded)),
              components_(components), resolution_(resolution) {}

    private:
        int width_;
        int height_;
        std::vector<std::uint8_t> pixels_;
        std::vector<std::uint8_t> encoded_;
        int components_;
        float resolution_;
    };

}
