#pragma once

#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "render/layout/node.hpp"
#include "typography/feature.hpp"
#include "typography/font.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace render::typography {

    // Forward declared: the registry builds fonts, and needs nothing from here.
    class Registry;

    /// @brief Turns text into positioned glyphs.
    ///
    /// Shaping is what maps characters onto the glyphs a font actually draws:
    /// it applies the font's ligatures and kerning, and it is the only correct
    /// way to get a glyph index out of a character. A run comes back as layout
    /// nodes ready for the line breaker -- glyphs for ink, glue for spaces,
    /// because a space is where a line is allowed to break.
    ///
    /// @par Fallback
    /// Several fonts may be offered. The first that can draw the run is used;
    /// a character it has no glyph for is looked for in its own design's
    /// faces for other scripts, through the registry when one was given, then
    /// in the fonts offered after it, then in the system's faces. The text is
    /// cut into runs by the face that draws each character and every run is
    /// shaped whole, so a word in another script keeps its kerning and its
    /// joins. A character no face covers keeps glyph zero, which is the box
    /// the font draws for exactly that situation.
    ///
    /// @par Direction
    /// A run in a script written right to left -- Arabic, Hebrew -- comes
    /// back in the order it is drawn in, its first letter last. Putting the
    /// words of a line in their order is the paragraph's work, not this.
    ///
    /// @par Use
    /// @code
    /// const typography::Font* fonts[] = { text, math };   // math covers what text does not
    /// const auto nodes = shaper.shape(memory::Slice{fonts, 2}, "delta \xCE\xB4", {});
    /// @endcode
    class Shaper {
    public:
        /// @brief Binds a shaper to an allocator.
        /// @param arena    Allocator for the nodes produced; must outlive them.
        /// @param registry Where a face for a script the one asked for lacks
        ///                 is found, or null to use only the fonts offered.
        explicit Shaper(memory::Arena& arena, Registry* registry = nullptr) noexcept
            : arena(arena), registry(registry) {}

        /// @brief Releases the buffer every run is shaped in.
        ~Shaper() noexcept;

        Shaper(const Shaper&) = delete("a shaper owns the HarfBuzz buffer every run is shaped in");
        Shaper& operator=(const Shaper&) = delete("a shaper owns the HarfBuzz buffer every run is shaped in");

        /// @brief Shapes one run of text.
        /// @param fonts    Preferred font first, fallbacks after it.
        /// @param text     UTF-8 to shape.
        /// @param features OpenType features to force on or off.
        /// @return One node per glyph, in reading order.
        /// @complexity O(n) in the text's length, plus the font's own shaping.
        [[nodiscard]] memory::Slice<layout::Node*> shape(
            memory::Slice<const Font*> fonts,
            std::string_view text,
            memory::Slice<Feature> features
        ) const;

    private:
        memory::Arena& arena;   ///< Allocator for the nodes produced.
        Registry* registry;     ///< Where a face for another script is found; may be null.

        /// The one HarfBuzz buffer every run is shaped in, made on the first
        /// run and reset before each after it. A buffer made and destroyed
        /// per run cost an allocation and a free around every word; reset,
        /// it is exactly the buffer a fresh one would be, with its storage
        /// already grown to fit.
        mutable hb_buffer_t* buffer{nullptr};

        /// @brief A shape plan kept for a face: what HarfBuzz would otherwise
        ///        look up in the face's own list of plans for every word.
        struct Plan {
            hb_face_t* face{nullptr};                    ///< The face it shapes.
            hb_segment_properties_t properties{};        ///< The script, language and direction it was made for.
            hb_shape_plan_t* plan{nullptr};              ///< The plan; one reference of it held here.
        };

        /// The plans for the faces a document shapes in without features --
        /// its running text, its italic, its headings -- found by a scan of
        /// a handful of entries, and replaced oldest first when full.
        mutable std::array<Plan, 8> plans{};
        mutable std::size_t oldest{0};   ///< Which of #plans is replaced next.
    };

}
