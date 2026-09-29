#pragma once

#include "syntax/catcodes.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/tokens.hpp"
#include "syntax/traceback.hpp"
#include "memory/location.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace syntax {

    class Lexer {
    public:
        enum class Type {
            Newline, ///< State N: line start; an end-of-line here is \\par
            Skip,    ///< State S: skipping blanks; an end-of-line here starts a new line and nothing more
            Middle,  ///< State M: mid-line; an end-of-line here is a space
            Blank    ///< Just past a \\par; further empty lines add nothing
        };

        /// @brief Binds a lexer to a source buffer.
        /// @param source Text to lex; must outlive the lexer.
        /// @param codes  Category table, consulted per character and live to
        ///               `\\catcode` changes made while lexing.
        /// @param lexicon  Interning table for the tokens produced.
        Lexer(std::string_view source, CatCodes& codes, Lexicon& lexicon);

        Token advance();
        [[nodiscard]] bool empty() const noexcept;

        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return tracebacks_; }

    private:
        [[nodiscard]] static constexpr std::size_t length(const char lead) noexcept {
            const auto byte = static_cast<unsigned char>(lead);
            if ((byte & 0x80) == 0) return 1;
            if ((byte & 0xE0) == 0xC0) return 2;
            if ((byte & 0xF0) == 0xE0) return 3;
            if ((byte & 0xF8) == 0xF0) return 4;
            return 1;
        }

        [[nodiscard]] static constexpr bool ending(const char symbol) noexcept {
            return symbol == '\n' || symbol == '\r';
        }

        std::string_view sources{};
        CatCodes& table;
        Lexicon& lexicon;
        std::size_t offset = 0;
        memory::Location location{1, 1};
        Type type = Type::Newline;

        /// @name Text read as it stands
        /// A \\verb's text, lexed along with the command and handed out as
        /// the next token; and a verbatim block's body, from #opening to
        /// #stop, handed out whole as one token when the scan reaches it.
        /// @{
        Token held{};
        std::size_t opening = std::string_view::npos;
        std::size_t stop = 0;
        /// @}
        std::vector<Traceback> tracebacks_{};   ///< Illegal bytes, drained by Mouth::ingest.
    };

}