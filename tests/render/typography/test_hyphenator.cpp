#include "engine.hpp"
#include "memory/arena.hpp"
#include "typography/hyphenator.hpp"
#include "typography/library.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// The hyphenator: Liang's patterns in a packed trie, and the places a word
// may be broken -- never leaving fewer than two letters before a break or
// three after, and never breaking a word under five.

/// The engine's assets directory, found from this file.
static std::filesystem::path assets() {
    return engine::locate(__FILE__);
}

/// The body face and the maths face at ten points, and what built them.
struct Fonts {
    memory::Arena arena{1u << 24};                            ///< What the faces and nodes take.
    memory::Arena scratch{1u << 22};                          ///< What a layout pass takes and drops.
    render::typography::Library library{arena};                ///< The indexed tree.
    render::typography::Registry registry{arena, library};    ///< The faces at their sizes.
    const render::typography::Shaper shaper{arena};           ///< Text into glyphs.
    const render::typography::Font* text{nullptr};            ///< Latin Modern Roman.
    const render::typography::Font* maths{nullptr};           ///< New Computer Modern Math.

    Fonts() {
        library.survey((assets() / "fonts").string());
        library.alias("text", "lmroman10-regular");
        library.alias("expression", "NewCMMath-Regular");
        text = registry.get({.family = "text", .size = 10.0f});
        maths = registry.get({.family = "expression", .size = 10.0f});
        assert(text && maths && "the engine's faces open");
    }
};

/// @brief A word with a hyphen at every place it may be broken.
static std::string broken(const render::typography::Hyphenator& hyphenator, memory::Arena& scratch,
                          const std::string_view word) {
    const memory::Slice<std::uint32_t> codes = scratch.allocate<std::uint32_t>(word.size());
    for (std::size_t index = 0; index < word.size(); ++index) codes[index] = static_cast<unsigned char>(word[index]);
    const memory::Slice<std::uint8_t> marks = hyphenator.execute(scratch, codes);
    std::string text;
    for (std::size_t index = 0; index < word.size(); ++index) {
        text += word[index];
        if (index < marks.size() && marks[index] != 0) text += '-';
    }
    return text;
}

int main() {
    memory::Arena scratch(1u << 22);

    // A few patterns written as a pattern file writes them.
    {
        const render::typography::Hyphenator hyphenator;
        assert((hyphenator.count() == 0) && "a hyphenator breaks nothing until given a language");
        assert((broken(hyphenator, scratch, "anything") == "anything") && "and so breaks nothing");
        assert((hyphenator.parse("1ba % a break before every ba\n") == 1) && "a pattern is read, its comment not");
        assert((broken(hyphenator, scratch, "abababa") == "aba-baba") &&
               "an odd digit is a break, but not one leaving a single letter before it or two after");
        assert((hyphenator.parse("a2b") == 1) && "a second pattern is read beside the first");
        assert((broken(hyphenator, scratch, "abababa") == "abababa") &&
               "a higher even digit at the same place forbids the break");
    }

    // American English, from the file on disk.
    {
        const render::typography::Hyphenator english;
        const std::size_t loaded =
            english.compose((assets() / "hyphens" / "hyph-en-us.pat.txt").string());
        assert((loaded > 4000) && "the English patterns are read");
        assert((broken(english, scratch, "hyphenation") == "hy-phen-ation") && "hyphenation");
        assert((broken(english, scratch, "typesetting") == "type-set-ting") && "typesetting");
        assert((broken(english, scratch, "algorithm") == "al-go-rithm") && "algorithm");
        assert((broken(english, scratch, "cat") == "cat") && "a short word is never broken");
        assert((english.compose((assets() / "no-such-file").string()) == 0) &&
               "a file that is not there adds nothing");
    }

    return 0;
}
