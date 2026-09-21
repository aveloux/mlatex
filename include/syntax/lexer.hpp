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
            Skip,    ///< State S: skipping blanks; an end-of-line here vanishes
            Middle   ///< State M: mid-line; an end-of-line here is a space
        };

        /// @brief Binds a lexer to a source buffer.
        /// @param source Text to lex; must outlive the lexer.
        /// @param codes  Category table, consulted per character and live to
        ///               `\\catcode` changes made while lexing.
        /// @param names  Interning table for the tokens produced.
        Lexer(std::string_view source, CatCodes& codes, Lexicon& names);

        Token advance();
        [[nodiscard]] bool empty() const noexcept;
        void reset() noexcept;

        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return faults; }

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
        std::vector<Traceback> faults{};   ///< illegal bytes, drained by Mouth::ingest
    };

}