#pragma once

#include "layout/pager.hpp"
#include "memory/slice.hpp"
#include "render/composer.hpp"
#include "render/graphics/image.hpp"

#include <harfbuzz/hb.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace render {

    /// @brief Writes laid-out pages out as a PDF.
    ///
    /// A PDF is a list of numbered objects, a table saying where each one
    /// starts, and a trailer pointing at the root. This writes exactly that
    /// and nothing more: no compression, no incremental updates, no rendering.
    /// Every glyph was positioned by the time it gets here, so the only work
    /// left is naming things and counting bytes.
    ///
    /// @par Fonts
    /// Each face is cut down to the glyphs the document actually used and
    /// embedded whole under Identity encoding, so a glyph index in the page
    /// description is the same index in the embedded file. A character map
    /// goes in beside it, which is what makes the text in the finished file
    /// selectable and searchable rather than a picture of itself.
    ///
    /// Cutting is most of the writer's time, and each face's cut depends on
    /// that face alone, so the faces are cut in parallel -- one thread per
    /// face, up to the machine's cores, the writing thread among them. The
    /// file itself is still written by one thread in one order, taking each
    /// face's cut as it reaches that face, so the bytes are the same whichever
    /// cut finishes first.
    ///
    /// @par Why not a graphics library
    /// One was used here before. It rasterises, composites, and subsets fonts
    /// for output formats this engine does not produce -- and it cost more
    /// time per document than the entire rest of the pipeline. Writing the
    /// file directly is a few hundred lines and two orders of magnitude
    /// faster, because every decision it would make has already been made.
    ///
    /// @par Use
    /// @code
    /// const auto pages = typesetter.compose(composer.document());
    /// if (!render::Pdf::compose(composer, pages, "build/main.pdf")) {
    ///     // the path could not be written
    /// }
    /// @endcode
    class Pdf {
    public:
        /// @brief Draws every page into a PDF held in memory.
        ///
        /// What compose() writes, before it is written: a program that wants
        /// the document to send or store rather than to leave on disk takes
        /// it from here and no file is ever made.
        ///
        /// @param composer Composer holding the document; it describes each page.
        /// @param pages  The pages, as the typesetter laid them out.
        /// @return The whole file, or nothing when there were no pages or the
        ///         page has no size.
        /// @complexity O(n) in the marks on the pages, plus the fonts embedded.
        [[nodiscard]] static std::string render(Composer& composer, memory::Slice<layout::Pager::Page> pages);

        /// @brief Draws every page and writes the file.
        /// @param composer Composer holding the document; it describes each page.
        /// @param pages  The pages, as the typesetter laid them out.
        /// @param path   File to write.
        /// @return True when the file was written.
        /// @complexity O(n) in the marks on the pages, plus the fonts embedded.
        [[nodiscard]] static bool compose(
            Composer& composer,
            memory::Slice<layout::Pager::Page> pages,
            std::string_view path
        );

    private:
        /// @brief One PDF file, assembled object by object.
        ///
        /// Objects are numbered from one as they are added, and every one's
        /// byte offset is recorded, because the cross-reference table at the
        /// end of the file is a list of exactly those offsets.
        struct File {
            std::string bytes{};                   ///< The file so far.
            std::vector<std::size_t> offsets{};    ///< Where each object starts.

            /// @brief Reserves the next object number without writing it yet.
            ///
            /// A page has to name its contents before the contents exist, so
            /// numbers are handed out ahead of the objects that fill them.
            ///
            /// @return The number reserved.
            [[nodiscard]] std::size_t reserve();

            /// @brief Opens the object with a given number at this point.
            /// @param number Number reserved earlier.
            void open(std::size_t number);

            /// @brief Closes the object most recently opened.
            void close();

            /// @brief Writes a stream object: a dictionary, then raw bytes.
            /// @param number  Number reserved earlier.
            /// @param extra   Dictionary entries to write besides the length.
            /// @param payload The stream's contents.
            void stream(std::size_t number, std::string_view extra, std::string_view payload);
        };
    };

}
