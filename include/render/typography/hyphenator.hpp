#pragma once

#include "memory/arena.hpp"
#include "memory/slice.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace render::typography {

    /// @brief Liang's hyphenation algorithm, over a trie of patterns.
    ///
    /// A pattern is a short run of letters carrying a digit between each pair,
    /// and the digits say how willing the language is to break there: odd
    /// numbers permit a break, even numbers forbid one, and the highest number
    /// claimed by any pattern at a position wins. `hy3ph` says that `hyph` is
    /// a good place to break after `hy`.
    ///
    /// The patterns come from the TeX hyphenation files, which are per
    /// language. Loading English gives a trie of a few thousand patterns, and
    /// a word is then hyphenated by walking every suffix of it through that
    /// trie -- bounded by the word's length, not by the pattern count.
    ///
    /// @par Built by the compiler
    /// The trie is flat arrays that name each other by index, and Trie::parse()
    /// is a constant expression. So a language compiled in with `#embed` (see
    /// `patterns.cpp`) is parsed by the compiler into a trie that is part of
    /// the executable: embed() points at it and a run spends nothing on it.
    /// Any other language is read at run time with compose(), through the same
    /// parse(), into a trie of the hyphenator's own.
    ///
    /// @par Packed
    /// Each node's children sit side by side in one array, sorted by letter,
    /// so a step down the trie reads a few neighbouring entries and stops at
    /// the first letter past the one it wants, rather than following links
    /// scattered across twenty thousand nodes.
    ///
    /// @par Use
    /// @code
    /// typography::Hyphenator hyphenator;
    /// if (hyphenator.embed("hyph-en-us.pat.txt") == 0) {    // compiled in
    ///     hyphenator.compose("assets/hyphens/hyph-en-us.pat.txt");   // or read
    /// }
    ///
    /// // 'hyphenation' -> hy-phen-ation
    /// const auto breaks = hyphenator.execute(scratch, letters);
    /// for (std::size_t index = 0; index < breaks.size(); ++index) {
    ///     if (breaks[index]) { /* a break may go after letters[index] */ }
    /// }
    /// @endcode
    class Hyphenator {
    public:
        /// @brief One letter along a pattern, packed: where its children sit
        ///        in the edges, and what the patterns ending here say. Node 0
        ///        is the root, which no edge leads to, so 0 also means none.
        struct Node {
            std::uint32_t begin{0};      ///< Its first child's edge.
            std::uint32_t children{0};   ///< How many children it has.
            std::uint32_t levels{0};     ///< Where its digits start in the levels.
            std::uint32_t count{0};      ///< How many digits; 0 when no pattern ends here.
        };

        /// @brief The way from one node to a child: the letter, and the child.
        struct Edge {
            std::uint32_t code{0};    ///< The letter.
            std::uint32_t child{0};   ///< The node it leads to.
        };

        /// How many first letters a trie's root indexes directly: every byte,
        /// which is every letter a pattern file is read in. A walk's first
        /// step is one load rather than a scan of the root's twenty-odd
        /// children.
        static constexpr std::size_t alphabet = 256;

        /// @brief A trie that grows -- the compiler's while it builds a
        ///        language in, a hyphenator's own for one read at run time --
        ///        and its packed form, which is what a walk reads.
        struct Trie {
            /// @brief One node while the trie grows: its letter, its first
            ///        child and its next sibling, by index; its digits.
            struct Link {
                std::uint32_t code{0};
                std::uint32_t child{0};
                std::uint32_t next{0};
                std::uint32_t levels{0};
                std::uint32_t count{0};
            };

            std::vector<Link> links{Link{}};               ///< Every node as it grows; the first is the root.
            std::vector<Node> nodes{};                     ///< Every node, packed.
            std::vector<Edge> edges{};                     ///< Every node's children, side by side.
            std::vector<std::uint8_t> levels{};            ///< Every pattern's digits, one after another.
            std::array<std::uint32_t, alphabet> initials{};   ///< The root's child for each first letter.
            std::size_t patterns{0};                       ///< How many patterns it holds.

            /// @brief Reads patterns written as a TeX pattern file writes them
            ///        -- whitespace between them, `%` to the end of a line a
            ///        comment -- and packs the trie again.
            ///
            /// Each pattern writes one path through the trie, and its digits
            /// -- one more than its letters, since a digit may sit before the
            /// first and after the last -- where the path ends.
            ///
            /// @param text The patterns; read here and not kept.
            /// @return How many patterns were added.
            /// @complexity O(n) in the text's length, and O(m log m) in the
            ///             trie's nodes to pack it.
            constexpr std::size_t parse(const std::string_view text) {
                const std::size_t before = patterns;
                std::size_t cursor = 0;
                while (cursor < text.size()) {
                    const char letter = text[cursor];
                    if (letter == '%') {
                        while (cursor < text.size() && text[cursor] != '\n') ++cursor;
                        continue;
                    }
                    if (letter == ' ' || letter == '\t' || letter == '\r' || letter == '\n') {
                        ++cursor;
                        continue;
                    }

                    // One pattern: each letter walked into the trie as it is
                    // read, each digit kept for the level before the next.
                    std::array<std::uint8_t, 65> digits{};
                    std::size_t length = 0;
                    std::uint32_t current = 0;
                    while (cursor < text.size()) {
                        const char character = text[cursor];
                        if (character == ' ' || character == '\t' || character == '\r' || character == '\n' ||
                            character == '%') {
                            break;
                        }
                        ++cursor;
                        if (character >= '0' && character <= '9') {
                            digits[length] = static_cast<std::uint8_t>(character - '0');
                            continue;
                        }
                        if (length + 1 >= digits.size()) continue;

                        const auto code = static_cast<std::uint32_t>(static_cast<unsigned char>(character));
                        std::uint32_t match = 0;
                        for (std::uint32_t step = links[current].child; step != 0; step = links[step].next) {
                            if (links[step].code == code) {
                                match = step;
                                break;
                            }
                        }
                        if (match == 0) {
                            match = static_cast<std::uint32_t>(links.size());
                            links.push_back(Link{.code = code, .next = links[current].child});
                            links[current].child = match;
                        }
                        current = match;
                        ++length;
                    }
                    if (length == 0) continue;

                    links[current].levels = static_cast<std::uint32_t>(levels.size());
                    links[current].count = static_cast<std::uint32_t>(length + 1);
                    levels.insert(levels.end(), digits.begin(), digits.begin() + static_cast<std::ptrdiff_t>(length + 1));
                    ++patterns;
                }

                // Packed: every node's children copied side by side, sorted by
                // letter, and the root's also indexed by letter. Node indices
                // stay the ones the links have.
                nodes.assign(links.size(), Node{});
                edges.clear();
                edges.reserve(links.size());
                initials.fill(0);
                for (std::size_t index = 0; index < links.size(); ++index) {
                    Node& node = nodes[index];
                    node.levels = links[index].levels;
                    node.count = links[index].count;
                    node.begin = static_cast<std::uint32_t>(edges.size());
                    for (std::uint32_t step = links[index].child; step != 0; step = links[step].next) {
                        edges.push_back(Edge{.code = links[step].code, .child = step});
                    }
                    node.children = static_cast<std::uint32_t>(edges.size()) - node.begin;
                    std::sort(edges.begin() + node.begin, edges.end(),
                              [](const Edge& left, const Edge& right) { return left.code < right.code; });
                    if (index == 0) {
                        for (const Edge& edge : std::span{edges}.subspan(node.begin)) initials[edge.code] = edge.child;
                    }
                }
                return patterns - before;
            }
        };

        /// @brief Builds a hyphenator that breaks nothing until a language
        ///        is added.
        Hyphenator() noexcept = default;

        Hyphenator(const Hyphenator&) = delete("a hyphenator may point into a trie of its own");
        Hyphenator& operator=(const Hyphenator&) = delete("a hyphenator may point into a trie of its own");

        /// @brief Reads a TeX pattern file into the trie.
        ///
        /// A file that will not open leaves the trie as it was, so
        /// hyphenation simply does not happen rather than the run failing.
        ///
        /// @param path File to read.
        /// @return How many patterns were added.
        /// @complexity O(n) in the file's size.
        std::size_t compose(std::string_view path) const;

        /// @brief Reads patterns written as a TeX pattern file writes them.
        /// @param text The patterns; read here and not kept.
        /// @return How many patterns were added.
        /// @complexity O(n) in the text's length.
        std::size_t parse(std::string_view text) const;

        /// @brief Takes a language compiled into the engine.
        ///
        /// Nothing is read or built: the compiler built the trie, and this
        /// points at it. Patterns added by compose() afterwards go into a
        /// trie of the hyphenator's own, the language's patterns read again
        /// beside them.
        ///
        /// Defined beside the compiled data, in `patterns.cpp`, so the bytes
        /// of every language live in one translation unit.
        ///
        /// @param name The pattern file's name as it sits in the hyphenation
        ///             directory, such as `hyph-en-us.pat.txt`.
        /// @return How many patterns the language holds, or 0 when this build
        ///         did not compile it in -- or when the hyphenator already
        ///         holds a language, since a compiled one is taken whole.
        /// @complexity O(1).
        std::size_t embed(std::string_view name) const;

        /// @brief Finds the places a word may be broken.
        ///
        /// The defaults are LaTeX's for English: `\\lefthyphenmin` of 2 and
        /// `\\righthyphenmin` of 3, so no break leaves one letter behind or
        /// carries fewer than three over, and a word under five letters is
        /// never broken at all.
        ///
        /// @param scratch Allocator for the working arrays and the result.
        /// @param word    The word's code points, already folded to lower case.
        /// @param pad     The boundary marker the patterns were written with.
        /// @param left    Letters that must stay before a break.
        /// @param right   Letters that must follow it.
        /// @return One byte per letter; non-zero where a break may go after it.
        ///         Empty for a word too short to break.
        /// @complexity O(n * k) in the word's length and the longest pattern,
        ///             independent of how many patterns were loaded.
        [[nodiscard]] memory::Slice<std::uint8_t> execute(
            memory::Arena& scratch,
            memory::Slice<std::uint32_t> word,
            std::uint32_t pad = '.',
            std::size_t left = 2,
            std::size_t right = 3
        ) const;

        /// @brief How many patterns the trie holds, compiled in or added.
        [[nodiscard]] std::size_t count() const noexcept { return patterns; }

    private:
        mutable Trie grown{};                          ///< A trie of the hyphenator's own, for patterns read at run time.
        mutable std::string_view source{};             ///< The compiled language's own patterns, to read again into #grown.
        mutable std::span<const Node> nodes{};          ///< The trie execute() walks: the compiled one or #grown.
        mutable std::span<const Edge> edges{};          ///< Its edges.
        mutable std::span<const std::uint8_t> levels{}; ///< Its digits.
        mutable const std::uint32_t* initials{nullptr}; ///< Its root's child for each first letter.
        mutable std::size_t patterns{0};               ///< Patterns it holds.
    };

}
