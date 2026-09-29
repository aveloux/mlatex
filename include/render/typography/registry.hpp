#pragma once

#include "memory/arena.hpp"
#include "typography/face.hpp"
#include "typography/font.hpp"
#include "typography/library.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace render::typography {

    /// @brief Fonts already built, reached by what was asked for.
    ///
    /// A Font is a face at a size, and the two are cached apart: a face --
    /// the file's table directory, what a subset is cut from -- is opened
    /// once per family no matter how many sizes of it a document asks for,
    /// because a face costs nothing a second size would want re-read. Asking
    /// for the same request twice, family and size both, returns the same
    /// Font.
    ///
    /// @par Why apart
    /// A formula sets its scripts smaller through this same registry, one
    /// request per size a script nests to. Without a face cache of its own,
    /// each of those sizes would open a second HarfBuzz face over the same
    /// bytes -- indistinguishable in memory, but a distinct object, which is
    /// what a writer keys a font resource on. A maths-heavy page would then
    /// embed the same file two or three times over, paying for a subset pass
    /// each time, rather than once for every glyph the whole document drew
    /// from it.
    ///
    /// @par Use
    /// @code
    /// typography::Registry registry(arena, library);
    ///
    /// const typography::Registry::Request body{ .family = "text", .size = 12.0f };
    /// typography::Font* font = registry.get(body);
    ///
    /// // Same request, same pointer, no work:
    /// assert(registry.get(body) == font);
    ///
    /// // A second size of the same family shares its face with the first:
    /// typography::Font* small = registry.get({ .family = "text", .size = 8.0f });
    /// assert(small->face() == font->face());
    /// @endcode
    class Registry {
    public:
        /// @brief What a caller is asking for.
        struct Request {
            std::string_view family{};   ///< Family or alias, as the Library names it.
            int weight{400};             ///< Stroke weight, 400 being regular.
            int slant{0};                ///< Non-zero for an italic or oblique cut.
            float size{12.0f};           ///< Size in points.
        };

        /// @brief Builds an empty registry over a font collection.
        /// @param arena Allocator for buckets and entries.
        /// @param library  Where faces come from; must outlive this object.
        /// @param buckets Bucket count for each cache; rounded up to a power of two.
        Registry(memory::Arena& arena, Library& library, std::size_t buckets = 128) noexcept;

        ~Registry() noexcept;

        Registry(const Registry&) = delete("every font it built is found through it, and points into its tables");
        Registry& operator=(const Registry&) = delete("every font it built is found through it, and points into its tables");
        Registry(Registry&&) = delete("every font it built is found through it, and points into its tables");
        Registry& operator=(Registry&&) = delete("every font it built is found through it, and points into its tables");

        /// @brief The font for a request, building it on first sight.
        ///
        /// The face it reads is shared with every other size of the same
        /// family already built, and opened only when none has been yet.
        ///
        /// @param request Family, cut and size.
        /// @return The font, or nullptr when the family is unknown or the file
        ///         will not open.
        /// @complexity O(1) average after the first request for that family.
        [[nodiscard]] Font* get(const Request& request) noexcept;

        /// @brief A face that draws a character the one in hand does not, in
        ///        the same design where there is one, at the same size.
        ///
        /// Latin Modern draws Latin alone. New Computer Modern is the same
        /// design with Greek, Cyrillic, Hebrew, Armenian and Georgian beside
        /// it, and Devanagari in faces of its own, and is asked first, in the
        /// cut in hand: bold for bold, italic for italic, sans for sans --
        /// what a LaTeX document in Russian is set in, Computer Modern's
        /// Cyrillic. A script no carried face draws, Arabic above all, is
        /// looked for among the system's own faces, a serif first, whose
        /// folders are listed the first time one is needed.
        ///
        /// @param font    The face in hand.
        /// @param code    The character it has no glyph for.
        /// @param outside Whether the system's faces may be looked in, past
        ///                the carried ones; a caller with a fallback of its
        ///                own asks without them first.
        /// @return A face that draws it, or nullptr when none does.
        /// @complexity O(1) once a character of its block has been asked
        ///             about for this face.
        [[nodiscard]] const Font* cover(const Font& font, std::uint32_t code, bool outside = true) noexcept;

        /// @brief How many distinct fonts have been built.
        [[nodiscard]] std::size_t count() const noexcept { return fonts; }

        /// @brief How many distinct faces have been opened.
        ///
        /// At most one per family and cut a document actually used, whatever
        /// number of sizes it was asked for at -- the number a writer will
        /// later cut exactly that many subsets down to.
        [[nodiscard]] std::size_t opened() const noexcept { return faces; }

    private:
        /// @brief One font, at the size it was asked for.
        struct Entry {
            Request request{};      ///< What produced it.
            Font font{};            ///< The face at this size.
            Entry* next{nullptr};   ///< Next entry in this bucket.
        };

        /// @brief One face, shared by every size of its family a Font wraps.
        struct Surface {
            std::string_view family{};   ///< Family, as resolved and copied.
            int weight{0};                ///< Stroke weight this face was opened for.
            int slant{0};                  ///< Slant this face was opened for.
            Face face{};                   ///< The file, opened.
            Surface* next{nullptr};       ///< Next surface in this bucket.
        };

        /// @brief What cover() found for a face and a block of characters.
        struct Cover {
            const Font* font{nullptr};    ///< The face in hand.
            std::uint32_t block{0};       ///< The character's block of 128.
            bool outside{false};          ///< Whether the system's faces were looked in.
            const Font* found{nullptr};   ///< The face that draws it, or null for none.
        };

        memory::Arena& arena;       ///< Storage for buckets and entries.
        Library& library;           ///< Where faces come from.
        std::vector<Cover> covers{};   ///< Every answer cover() gave, by face and block.
        std::size_t slots{0};       ///< Bucket count, always a power of two.
        Entry** table{nullptr};     ///< Font buckets.
        Surface** surfaces{nullptr};   ///< Face buckets.
        std::size_t fonts{0};       ///< Distinct fonts built so far.
        std::size_t faces{0};       ///< Distinct faces opened so far.
    };

}
