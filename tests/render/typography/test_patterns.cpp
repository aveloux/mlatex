#include "memory/arena.hpp"
#include "typography/hyphenator.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// The compiled patterns: the language built into the engine with #embed
// and the same file read from disk must build the same trie and answer
// identically for every word -- both reach it through parse(), and nothing
// between the file and the trie may change a single break.

int main() {
    const std::filesystem::path source =
        std::filesystem::path(__FILE__).lexically_normal().parent_path().parent_path().parent_path().parent_path() /
        "assets" / "hyphens" / "hyph-en-us.pat.txt";

    memory::Arena arena(1u << 20);
    memory::Arena scratch(1u << 22);

    const render::typography::Hyphenator read;
    const render::typography::Hyphenator compiled;

    const std::size_t loaded = read.compose(source.string());
    const std::size_t embedded = compiled.embed(source.filename().string());
    assert((loaded > 0) && "the pattern file is read");
    assert((embedded == loaded) && "the compiled language has every pattern");
    assert((compiled.count() == read.count()) && "both tries hold the same patterns");

    // A second language is refused rather than merged.
    const std::size_t again = compiled.embed(source.filename().string());
    assert((again == 0) && "a second language is refused");

    // The words every pattern is made of, and random ones besides.
    std::vector<std::string> words;
    std::ifstream file(source, std::ios::binary);
    for (std::string pattern; file >> pattern;) {
        std::string letters;
        for (const char letter : pattern) {
            if (letter >= 'a' && letter <= 'z') letters += letter;
        }
        if (letters.size() >= 2) {
            words.push_back(letters);
            words.push_back("re" + letters + "ing");
        }
    }

    std::mt19937 random(7);
    std::uniform_int_distribution<int> length(4, 14);
    std::uniform_int_distribution<int> letter('a', 'z');
    for (int count = 0; count < 20000; ++count) {
        std::string word(static_cast<std::size_t>(length(random)), 'a');
        for (char& place : word) place = static_cast<char>(letter(random));
        words.push_back(word);
    }

    std::size_t broken = 0;
    for (const std::string& word : words) {
        const memory::Slice<std::uint32_t> codes = scratch.allocate<std::uint32_t>(word.size());
        for (std::size_t index = 0; index < word.size(); ++index) {
            codes[index] = static_cast<unsigned char>(word[index]);
        }

        const memory::Slice<std::uint8_t> expected = read.execute(scratch, codes);
        const memory::Slice<std::uint8_t> actual = compiled.execute(scratch, codes);
        assert((expected.count == actual.count) && "both tries answer for every letter");
        for (std::size_t index = 0; index < expected.count && index < actual.count; ++index) {
            if (expected[index] != actual[index]) {
                std::fprintf(stderr, "'%s' differs at %zu\n", word.c_str(), index);
                assert((false) && "both tries break every word the same way");
            }
            broken += expected[index] != 0;
        }
    }

    // The comparison means something only if some words do break.
    assert((broken > 0) && "some words do break");

    return 0;
}
