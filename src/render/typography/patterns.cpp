/// @file
/// @brief The one translation unit that holds the compiled hyphenation tries.
///
/// The pattern files are compiled in as they stand in assets/hyphens, with
/// `#embed`, and the compiler parses each into a trie with the same
/// Hyphenator::Trie::parse() a file read at run time goes through -- so a
/// language compiled in and the same language read from disk are the same
/// trie, and a run spends nothing building this one. Defined here, apart
/// from the rest of the class, so the bytes live in exactly one translation
/// unit.
#include "typography/hyphenator.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>

namespace render::typography {

// #embed is C++26's, and Clang still calls it an extension of its own there.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif

    /// American English, the language a document is broken in unless it
    /// asks for another.
    static constexpr char english[] = {
#embed "../../../assets/hyphens/hyph-en-us.pat.txt" suffix(,)
        0};

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    /// @brief A language's trie as the compiler leaves it: the packed trie
    ///        parse() made, copied into arrays of exactly its size.
    template <std::size_t Nodes, std::size_t Edges, std::size_t Levels>
    struct Compiled {
        std::array<Hyphenator::Node, Nodes> nodes{};                    ///< Every node, packed; the first is the root.
        std::array<Hyphenator::Edge, Edges> edges{};                    ///< Every node's children, side by side.
        std::array<std::uint8_t, Levels> levels{};                      ///< Every pattern's digits.
        std::array<std::uint32_t, Hyphenator::alphabet> initials{};     ///< The root's child for each first letter.
        std::size_t patterns{0};                                        ///< How many patterns it holds.
    };

    /// The English trie's size, found by building it once; the trie itself
    /// is built again into arrays of that size, since what the compiler
    /// allocates while it works cannot outlive the work.
    static constexpr auto measured = [] {
        Hyphenator::Trie trie;
        trie.parse({english, sizeof english - 1});
        return std::array{trie.nodes.size(), trie.edges.size(), trie.levels.size()};
    }();

    static constexpr auto american = [] {
        Hyphenator::Trie trie;
        trie.parse({english, sizeof english - 1});
        Compiled<measured[0], measured[1], measured[2]> built{};
        std::ranges::copy(trie.nodes, built.nodes.begin());
        std::ranges::copy(trie.edges, built.edges.begin());
        std::ranges::copy(trie.levels, built.levels.begin());
        built.initials = trie.initials;
        built.patterns = trie.patterns;
        return built;
    }();

    std::size_t Hyphenator::embed(const std::string_view name) const {
        // A compiled language is taken whole, and only into a hyphenator
        // that holds nothing yet. Its own patterns are kept as well, for
        // compose() to read into a trie of the hyphenator's if more are added.
        if (patterns != 0 || name != "hyph-en-us.pat.txt") return 0;

        source = {english, sizeof english - 1};
        nodes = american.nodes;
        edges = american.edges;
        levels = american.levels;
        initials = american.initials.data();
        patterns = american.patterns;
        Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                    "Embedded {} hyphenation patterns for {}", patterns, name);
        return patterns;
    }

}
