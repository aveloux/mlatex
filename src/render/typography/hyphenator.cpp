/// @file
/// @brief Hyphenator implementation: Liang's algorithm over a pattern trie.
///
/// Loading writes one path through the trie per pattern -- by the compiler,
/// for a language built in. Hyphenating walks every suffix of the word
/// through that trie and takes the highest digit claimed at each position,
/// which is exactly what Liang's method asks for.
#include "typography/hyphenator.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>

namespace render::typography {

    std::size_t Hyphenator::compose(const std::string_view path) const {
        // std::string, not path.data(): a string_view is not guaranteed to be
        // terminated, and handing an unterminated one to a stream reads past
        // the end of the name. It is a file path, so the copy is nothing.
        std::ifstream file{std::string(path), std::ios::binary | std::ios::ate};
        if (!file) {
            Logger::log(Logger::Type::Layout, Logger::Level::Warning,
                        "Hyphenation patterns missing: {}", path);
            return 0;
        }

        // Read whole. A pattern file is a few hundred kilobytes and holds
        // several thousand lines; reading it a line at a time would allocate
        // once per line to look at a dozen bytes.
        const std::streamsize size = file.tellg();
        if (size <= 0) return 0;
        file.seekg(0, std::ios::beg);

        std::string contents(static_cast<std::size_t>(size), '\0');
        if (!file.read(contents.data(), size)) return 0;

        const std::size_t added = parse(contents);
        Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                    "Loaded {} hyphenation patterns from {}", added, path);
        return added;
    }

    std::size_t Hyphenator::parse(const std::string_view text) const {
        // A compiled language is taken as the compiler left it; adding to it
        // means reading its own patterns into a trie of this hyphenator's,
        // which is the only time they are read at run time.
        if (!source.empty()) {
            grown.parse(std::exchange(source, std::string_view{}));
        }

        const std::size_t added = grown.parse(text);
        nodes = grown.nodes;
        edges = grown.edges;
        levels = grown.levels;
        initials = grown.initials.data();
        patterns = grown.patterns;
        return added;
    }

    memory::Slice<std::uint8_t> Hyphenator::execute(
        memory::Arena& scratch,
        const memory::Slice<std::uint32_t> word,
        const std::uint32_t pad,
        const std::size_t left,
        const std::size_t right
    ) const {
        // A break needs `left` letters before it and `right` after, so a word
        // shorter than both together has nowhere legal to break.
        if (nodes.size() < 2 || left == 0 || right == 0 || word.count < left + right) return {};

        const std::size_t length = word.count;
        const std::size_t total = length + 2;

        // The patterns are written against a marked word boundary, so the
        // word is padded with that marker before any of them can match.
        const memory::Slice<std::uint32_t> padded = scratch.allocate<std::uint32_t>(total);
        padded[0] = pad;
        for (std::size_t index = 0; index < length; ++index) padded[index + 1] = word[index];
        padded[total - 1] = pad;

        const memory::Slice<std::uint8_t> claimed = scratch.allocate<std::uint8_t>(total + 1);
        for (std::size_t index = 0; index <= total; ++index) claimed[index] = 0;

        // The walk reads through plain pointers: it is the innermost loop of
        // hyphenating a document, and a checked accessor per letter would be
        // most of what it costs.
        const std::uint32_t* const letters = padded.data;
        std::uint8_t* const highest = claimed.data;
        const Node* const trie = nodes.data();
        const Edge* const ways = edges.data();
        const std::uint8_t* const digits = levels.data();

        for (std::size_t start = 0; start < total; ++start) {
            // The first letter straight from the root's index; see #alphabet.
            // Every letter after it from the node's children, side by side
            // and sorted, so the scan stops at the first letter past it.
            const std::uint32_t first = letters[start];
            std::uint32_t current = first < alphabet ? initials[first] : 0;

            for (std::size_t step = start; current != 0;) {
                // Highest digit wins, which is how an even level in a longer
                // pattern vetoes the odd level a shorter one asked for.
                const Node& node = trie[current];
                for (std::size_t offset = 0; offset < node.count; ++offset) {
                    const std::size_t target = start + offset;
                    const std::uint8_t level = digits[node.levels + offset];
                    if (target <= total && level > highest[target]) highest[target] = level;
                }

                if (++step >= total) break;
                const std::uint32_t code = letters[step];
                current = 0;
                for (const Edge* way = ways + node.begin, *last = way + node.children; way != last; ++way) {
                    if (way->code < code) continue;
                    if (way->code == code) current = way->child;
                    break;
                }
            }
        }

        const memory::Slice<std::uint8_t> result = scratch.allocate<std::uint8_t>(length);
        for (std::size_t index = 0; index < length; ++index) result[index] = 0;

        // Odd means a break is allowed. Level `index + 2` is the gap after the
        // word's letter `index`, because the accumulator is indexed over the
        // padded word and its first entry sits before the opening marker. The
        // range leaves `left` letters before a break and `right` after it.
        for (std::size_t index = left - 1; index + right < length; ++index) {
            if (highest[index + 2] & 1u) result[index] = 1;
        }

        return result;
    }

}
