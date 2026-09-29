#pragma once

#include "syntax/mouth.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace syntax {

    /// @brief Reads a macro argument back as plain text.
    ///
    /// A name, a key, a file path -- anywhere a document writes one brace
    /// group and a primitive wants its characters back as a string rather
    /// than as tokens. Every primitive that reads such an argument, in
    /// either layer, reads it through here rather than joining tokens itself.
    class Argument {
    public:
        /// @brief Reads one brace-group argument and joins its tokens' text.
        /// @param mouth Expander to read from.
        /// @return The joined characters; empty when the group itself was.
        /// @complexity O(k) in the number of tokens read.
        [[nodiscard]] static std::string text(Mouth& mouth) {
            const std::vector<Token> gathered = mouth.argument({}, 0);

            std::string joined;
            joined.reserve(gathered.size());
            for (const Token& token : gathered) joined += token.text;
            return joined;
        }

        /// @brief Reads one argument and expands it all the way, as text.
        ///
        /// TeX's `\\edef` in miniature: every macro in the argument is run
        /// and every primitive that makes tokens -- `\\evaluate`,
        /// `\\calculate`, `\\variable` -- has made them, and what is left is
        /// joined into the text a value is stored or computed from. A
        /// control word is followed by a space wherever a letter comes next,
        /// so the text reads back as the same tokens it was made from.
        ///
        /// The argument is read to a marker placed after it, which is a name
        /// the lexer can never produce. An argument whose last macro reads
        /// past its own end -- one taking an argument the group does not hold
        /// -- stops there, and whatever it read from beyond is put back.
        ///
        /// @param mouth Expander to read from.
        /// @return The expanded text; empty when the group itself was.
        /// @complexity O(k) in the tokens the expansion produces.
        [[nodiscard]] static std::string expanded(Mouth& mouth) {
            std::vector<Token> gathered = mouth.argument({}, 0);

            const Symbol marker = mouth.lexicon().intern("\\argument:end");
            gathered.push_back(Token{
                .symbol = marker,
                .category = CatCodes::Category::Escape,
                .text = mouth.lexicon().resolve(marker)
            });

            const std::size_t depth = mouth.stream().size();
            mouth.stream().inject(gathered);

            std::string joined;
            bool word = false;   // the last thing written was a control word
            for (;;) {
                const Token token = mouth.expand();
                if (token.symbol == marker || token.empty() || mouth.error()) break;
                if (mouth.stream().size() < depth) {
                    mouth.stream().inject(std::span{&token, 1});
                    break;
                }

                const bool letter = !token.text.empty() &&
                                    ((token.text[0] >= 'a' && token.text[0] <= 'z') ||
                                     (token.text[0] >= 'A' && token.text[0] <= 'Z'));
                if (word && letter) joined += ' ';
                joined += token.text;

                word = token.category == CatCodes::Category::Escape && token.text.size() > 1 &&
                       ((token.text[1] >= 'a' && token.text[1] <= 'z') ||
                        (token.text[1] >= 'A' && token.text[1] <= 'Z'));
            }
            return joined;
        }

        /// @brief Reads a keyword TeX writes after a primitive -- `plus`,
        ///        `minus`, `height`, `width` -- when it comes next, past any
        ///        blanks; reads nothing at all when it does not.
        /// @param mouth Expander to read from.
        /// @param word  The keyword, in lower case; matched in either case.
        /// @return Whether it was there.
        /// @complexity O(k) in the keyword's length.
        static bool keyword(Mouth& mouth, const std::string_view word) {
            std::size_t offset = 0;
            while (mouth.lookahead(offset).category == CatCodes::Category::Space) ++offset;
            for (std::size_t index = 0; index < word.size(); ++index) {
                const Token token = mouth.lookahead(offset + index);
                if (token.text.size() != 1 || (token.text[0] | 0x20) != word[index]) return false;
            }
            for (std::size_t index = 0; index < offset + word.size(); ++index) mouth.read();
            return true;
        }
    };

}
