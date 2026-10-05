#pragma once

#include "memory/arena.hpp"
#include "memory/slice.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace render::typography {

    /// @brief The font collection, indexed once and resident in memory.
    ///
    /// A font family is reached by name in constant time, and the bytes behind
    /// it are mapped into memory at most once for the life of the process.
    /// Nothing here parses a font: post() only walks the directory and
    /// records where each file is, so a collection of any size costs one pass
    /// over the tree rather than one shaping-engine parse per file.
    ///
    /// @par Why mapped rather than read
    /// A maths face is a megabyte or more, and a document uses a few dozen of
    /// its glyphs. Reading it copies every byte out of the system's file
    /// cache into memory that has to be committed first; mapping it hands
    /// HarfBuzz the cache's own pages, and only the ones it actually touches
    /// -- the table directory, the metrics, the outlines of the glyphs that
    /// were drawn -- are ever brought in. A file that will not map, on a
    /// system that cannot map one or a share that refuses to, is read into
    /// the arena instead, exactly as it always was.
    ///
    /// @par Why not the system font database
    /// Asking the platform to index the collection means handing it every file
    /// and waiting while it reads the name table out of each one. For the
    /// hundred-odd faces a document set ships with that is tens of
    /// milliseconds, every run, to answer a question the file names already
    /// answer. post() answers it from the file names, and get() pays for a
    /// face only when a document actually asks for one.
    ///
    /// @par Naming
    /// A file's family is its stem, folded to lower case: `LMRoman10-Regular.otf`
    /// registers as `lmroman10-regular`. A lookup folds its query the same way,
    /// so a document may write the family however it likes.
    ///
    /// Each directory posted also registers its own name as a family, bound
    /// to the first face inside it. A tree laid out as `fonts/text` and
    /// `fonts/expression` therefore answers `text` and `expression` with no
    /// configuration at all, and set() overrides that when a document wants
    /// a particular face in the role.
    ///
    /// @par Use
    /// @code
    /// memory::Arena arena(16 * 1024 * 1024);
    /// typography::Collection collection(arena);
    ///
    /// collection.post("assets/fonts");                // index, no parsing
    /// collection.set("text", "lmroman10-regular");    // pick the body face
    ///
    /// if (const auto* entry = collection.get("text")) {
    ///     face.compose(entry->bytes);                 // resident, borrowed
    /// }
    /// @endcode
    ///
    /// @warning The arena must outlive the collection: every name handed out
    ///          points into it. The bytes of a mapped file live exactly as
    ///          long as the collection does, so nothing built over them -- a
    ///          Face, a Registry -- may outlive it either.
    class Collection {
    public:
        /// @brief One indexed face, or one alias standing in for another name.
        struct Entry {
            std::string_view family{};                 ///< Lower-cased lookup name.
            std::string_view path{};                   ///< Where the file lives, UTF-8 and zero-terminated.
            std::span<const std::uint8_t> bytes{};     ///< File contents, empty until mapped or read.
            Entry* target{nullptr};                    ///< Set on an alias: the entry it stands for.
            Entry* next{nullptr};                      ///< Next entry in this bucket.
        };

        /// @brief Builds an empty collection.
        /// @param arena Allocator for names, buckets and font bytes.
        /// @param buckets Bucket count; rounded up to a power of two so that a
        ///                lookup can mask rather than divide.
        explicit Collection(memory::Arena& arena, std::size_t buckets = 512) noexcept;

        /// @brief Unmaps every file mapped so far.
        ~Collection() noexcept;

        Collection(const Collection&) = delete("a collection owns the font bytes it keeps resident, and every face points into them");
        Collection& operator=(const Collection&) = delete("a collection owns the font bytes it keeps resident, and every face points into them");

        /// @brief Posts every font file under a directory to the collection,
        ///        recursively.
        ///
        /// Registers each file under its stem, and each directory under its own
        /// name bound to the first face found inside it. Files that are not
        /// fonts are skipped, and a family already registered is kept, so a
        /// directory posted earlier wins over one posted later.
        ///
        /// On Windows the tree is listed through the system's own directory
        /// search, one request per directory for its whole listing and nothing
        /// but names and attributes; std::filesystem asks for the same thing
        /// an entry and a conversion at a time, which on a tree of a hundred
        /// files is most of what listing it costs. Everywhere else it is
        /// walked with std::filesystem. Either way the order is the directory
        /// listing's, depth first, and links to directories are not followed.
        ///
        /// Given the tree's listing -- the build takes one of the engine's own
        /// font tree -- nothing is walked at all: each file named is registered
        /// under the directory as though the walk had found it there.
        ///
        /// @param directory Root to walk.
        /// @param files     The files under it, relative to it; empty to walk it.
        /// @return How many faces were indexed.
        /// @complexity O(n) in the files present, with no font parsing.
        std::size_t post(std::string_view directory, std::span<const std::string_view> files = {});

        /// @brief Posts the system's own font folders, once.
        ///
        /// The faces the engine carries draw Latin, Greek, Cyrillic, Hebrew,
        /// Armenian, Georgian and Devanagari; a script past them -- Arabic
        /// above all -- is drawn only by a face the system has. Its folders
        /// are posted the first time such a face is wanted and never
        /// otherwise, so a document in the carried scripts never looks
        /// outside the engine's own tree, and a face the engine carries
        /// keeps its name, since what was posted first wins.
        ///
        /// Taken under the same lock as get(), since a face may be opening
        /// on another thread while the folders are listed.
        ///
        /// @return How many faces the system added; 0 on every call after the first.
        /// @complexity O(n) in the files the folders hold, the first time.
        std::size_t post();

        /// @brief Sets a second name to stand for a face.
        ///
        /// This is how a role such as `text` is pointed at a concrete face.
        /// The alias resolves at lookup time, so aliasing a name that is not
        /// indexed yet is fine as long as it is indexed before the first read.
        ///
        /// @param name   New name, folded to lower case.
        /// @param family Face it stands for, folded to lower case.
        /// @complexity O(1).
        void set(std::string_view name, std::string_view family);

        /// @brief A family's file, its bytes mapped on first request.
        ///
        /// The bytes stay mapped for the life of the collection -- or, for a
        /// file that would not map, live in the arena for as long -- so every
        /// request after the first is a table lookup. Faces borrow the span
        /// rather than copying it, which is what keeps a second size of the
        /// same family free.
        ///
        /// An alias resolves to the entry it stands for, and the entry
        /// returned carries that file's own name in Entry::family. A font
        /// built from it keeps that name rather than the alias, which is how
        /// a style asked for later finds its sibling cut: `text` means
        /// nothing next to `bold`, while `lmroman10-regular` does.
        ///
        /// @param family Family or alias to resolve.
        /// Safe to call from several threads at once: the only thing here
        /// that is not.
        ///
        /// @return The file's entry with Entry::bytes filled, or nullptr when
        ///         the family is unknown or the file cannot be read.
        /// @complexity O(1) average after the first call for that family; the
        ///             first is O(1) in the file's size when it maps.
        [[nodiscard]] const Entry* get(std::string_view family) const;

        std::size_t faces{0};             ///< How many faces are indexed, aliases excluded.
        mutable std::size_t bytes{0};     ///< How many bytes of font data are resident, mapped or read.

    private:
        /// @brief One file mapped into memory, kept until the collection goes.
        ///
        /// Held apart from the entries on purpose. set() may turn an entry
        /// that was a face into one that stands for another, dropping the
        /// span it held; a Face opened over those bytes is still reading
        /// them, so the mapping has to outlive the entry's claim on it.
        struct View {
            const void* data{nullptr};   ///< Where the file was mapped.
            std::size_t size{0};         ///< How many bytes were mapped.
        };

        memory::Arena& arena;             ///< Storage for names, buckets and bytes.
        std::size_t slots{0};             ///< Bucket count, always a power of two.
        Entry** table{nullptr};           ///< Bucket heads.
        bool searched{false};             ///< Whether post() has listed the system's folders.

        /// Every file mapped so far, and every one read where it would not
        /// map. Kept on the heap rather than in the arena, and get() taken
        /// under a lock, so a second thread may open faces ahead of the one
        /// that sets the document while that one allocates from the arena.
        mutable std::vector<View> views{};
        mutable std::vector<std::unique_ptr<std::uint8_t[]>> copies{};
        mutable std::mutex guard{};
    };

}
