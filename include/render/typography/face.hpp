#pragma once

#include <harfbuzz/hb.h>

#include <cstdint>
#include <span>
#include <vector>

namespace render::typography {

    /// @brief One font file, opened for measuring and shaping.
    ///
    /// A Face is a HarfBuzz face over bytes somebody else keeps alive. It
    /// borrows rather than owns, so opening the same file at a second size
    /// costs a table directory and no copy.
    ///
    /// @par Why HarfBuzz alone
    /// A rasteriser is not needed here. Every glyph the engine draws is named
    /// by index and positioned from the font's own tables, and the writer
    /// embeds the file whole rather than rendering it -- so the outlines are
    /// never asked for. Opening a file through HarfBuzz reads only the table
    /// directory and leaves each table unread until something wants it, which
    /// is as little work as opening a font can be.
    ///
    /// @warning The bytes must outlive the Face. Library allocates them from
    ///          the arena and never frees them, which satisfies that as long
    ///          as the arena outlives the Face -- Registry arranges it.
    ///
    /// @par Use
    /// @code
    /// typography::Face face;
    /// if (const auto* entry = library.read("text")) {
    ///     face.compose(entry->bytes);
    /// }
    /// @endcode
    class Face {
    public:
        Face() noexcept = default;
        ~Face() noexcept;

        Face(const Face&) = delete("a face owns its HarfBuzz face, which would be destroyed twice");
        Face& operator=(const Face&) = delete("a face owns its HarfBuzz face, which would be destroyed twice");
        Face(Face&& input) noexcept;
        Face& operator=(Face&& input) noexcept;

        /// @brief Closes the face, leaving the borrowed bytes alone.
        void dispose() noexcept;

        /// @brief Opens a face over bytes somebody else keeps alive.
        /// @param data Font file contents; must outlive this object.
        /// @return True when HarfBuzz accepted the file.
        /// @complexity O(1) in the file size: only the table directory is read.
        [[nodiscard]] bool compose(std::span<const std::uint8_t> data) noexcept;

        /// @brief The HarfBuzz face, which shaping and metrics are read from.
        [[nodiscard]] hb_face_t* handle() const noexcept { return face; }

        /// @brief The bytes this face reads, for a writer that embeds the file.
        [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return storage; }

        /// @brief How many glyphs the file holds.
        ///
        /// The upper bound on any glyph index from this face, which is what a
        /// caller keeping a table indexed by glyph needs to size it.
        ///
        /// @complexity O(1).
        [[nodiscard]] std::size_t count() const noexcept;

        /// @brief One glyph's ink extent, in the face's own units.
        ///
        /// Kept here rather than by each Font, because it is the same at
        /// every size: the maths face is set at three sizes in any formula
        /// with a script, and finding a CFF glyph's extent means running its
        /// outline program, which is worth doing once per glyph, not once per
        /// size. A Font scales it by its size over #units.
        ///
        /// @param glyph Glyph index.
        /// @return Its extents, or all zero when the face has no such glyph.
        /// @complexity O(1) once a glyph has been asked for; direct-mapped, so
        ///             a collision costs one outline program and no search.
        [[nodiscard]] hb_glyph_extents_t extents(std::uint32_t glyph) const noexcept;

        /// @brief The face's units per em, which #extents is measured in.
        [[nodiscard]] float units() const noexcept { return scale; }

    private:
        /// @brief One remembered glyph extent.
        struct Slot {
            std::uint32_t glyph{0};       ///< Which glyph these extents describe.
            bool ready{false};            ///< False until this slot has been filled.
            hb_glyph_extents_t extents{}; ///< The extents, in the face's units.
        };

        /// Cache size, less one: a power of two, so the slot is a mask.
        static constexpr std::size_t mask = 0x3FF;

        hb_face_t* face{nullptr};                    ///< HarfBuzz's view of the file.
        std::span<const std::uint8_t> storage{};     ///< Borrowed file contents.
        float scale{1000.0f};                        ///< Units per em.

        mutable hb_font_t* measure{nullptr};         ///< The face at its own units, made when first asked.
        mutable std::vector<Slot> cache{};           ///< Remembered extents, sized when first asked.
    };

}
