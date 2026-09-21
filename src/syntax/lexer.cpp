/// @file
/// @brief Lexer implementation: TeX's three-state line scanner.
///
/// The interesting part is advance(), which reproduces TeX's rule for what a
/// line ending means: state N yields `\\par`, state S yields nothing, state M
/// yields one space. Getting that wrong does not merely lose paragraph breaks
/// -- `\\par` is also the parser's error-recovery anchor.
#include "syntax/lexer.hpp"
#include "syntax/cache.hpp"
#include "logger.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace syntax {

    Lexer::Lexer(const std::string_view source, CatCodes& codes, Lexicon& names)
        : sources(source), table(codes), lexicon(names) {
        Logger::fmt(Logger::Type::Lexer, Logger::Level::Informative,
                    "Lexer bound to {} byte buffer", source.size());
    }

    bool Lexer::empty() const noexcept {
        return offset >= sources.size();
    }

    Token Lexer::advance() {
        const std::size_t size = sources.size();

        while (offset < size) {
            const std::size_t origin = offset;
            const memory::Location position = location;
            const char symbol = sources[offset];
            const CatCodes::Category category = table.get(symbol);

            if (category == CatCodes::Category::Space && ending(symbol)) {
                offset++;
                if (symbol == '\r' && offset < size && sources[offset] == '\n') {
                    offset++;
                }
                location.line++;
                location.column = 1;

                const Type previous = type;

                if (previous == Type::Middle) {
                    type = Type::Newline;
                    const auto [bound, value] = entry(lexicon, " ");
                    return Token{bound, CatCodes::Category::Space, position, value};
                }

                if (previous == Type::Newline) {
                    type = Type::Skip;
                    const Symbol bound = lexicon.intern("\\par");
                    Logger::fmt(Logger::Type::Lexer, Logger::Level::Debug,
                                "Lexed <Escape> [\\par] at {}:{}", position.line, position.column);
                    return Token{bound, CatCodes::Category::Escape, position, lexicon.resolve(bound)};
                }

                continue;
            }

            if (category == CatCodes::Category::Space) {
                offset++;
                location.column++;
                if (type == Type::Skip || type == Type::Newline) continue;
                type = Type::Skip;
                const auto [bound, value] = entry(lexicon, " ");
                return Token{bound, CatCodes::Category::Space, position, value};
            }

            if (category == CatCodes::Category::Ignore) {
                offset++;
                location.column++;
                continue;
            }

            if (category == CatCodes::Category::Invalid) {
                faults.emplace_back(Traceback::Type::Token, position,
                                    std::format("Illegal byte 0x{:02X} in input",
                                                static_cast<unsigned>(static_cast<unsigned char>(symbol))));
                Logger::fmt(Logger::Type::Lexer, Logger::Level::Warning,
                            "Illegal byte 0x{:02X} at {}:{} discarded",
                            static_cast<unsigned>(static_cast<unsigned char>(symbol)),
                            position.line, position.column);
                offset++;
                location.column++;
                continue;
            }

            if (category == CatCodes::Category::Comment) {
                while (offset < size && !ending(sources[offset])) {
                    offset++;
                    location.column++;
                }
                if (offset < size) {
                    const char terminator = sources[offset];
                    offset++;
                    if (terminator == '\r' && offset < size && sources[offset] == '\n') {
                        offset++;
                    }
                    location.line++;
                    location.column = 1;
                }
                type = Type::Newline;
                continue;
            }

            if (category == CatCodes::Category::Escape) {
                offset++;
                location.column++;

                if (offset < size) {
                    const char next = sources[offset];

                    if (table.get(next) == CatCodes::Category::Letter) {
                        while (offset < size && table.get(sources[offset]) == CatCodes::Category::Letter) {
                            offset++;
                            location.column++;
                        }
                        type = Type::Skip;
                    } else if (ending(next)) {
                        offset++;
                        if (next == '\r' && offset < size && sources[offset] == '\n') {
                            offset++;
                        }
                        location.line++;
                        location.column = 1;
                        type = Type::Middle;
                    } else if (table.get(next) == CatCodes::Category::Space) {
                        offset++;
                        location.column++;
                        type = Type::Skip;
                    } else {
                        const std::size_t span = ((static_cast<unsigned char>(next) & 0x80) == 0)
                                                     ? 1
                                                     : std::min(length(next), size - offset);
                        offset += span;
                        location.column++;
                        type = Type::Middle;
                    }
                } else {
                    type = Type::Middle;
                }

                const std::string_view slice = sources.substr(origin, offset - origin);
                const Symbol bound = lexicon.intern(slice);
                const std::string_view value = lexicon.resolve(bound);
                Logger::fmt(Logger::Type::Lexer, Logger::Level::Debug,
                            "Lexed <Escape> [{}] at {}:{}", slice, position.line, position.column);
                return Token{bound, CatCodes::Category::Escape, position, value};
            }

            const std::size_t span = (static_cast<unsigned char>(symbol) & 0x80) == 0
                                         ? 1
                                         : std::min(length(symbol), size - offset);
            offset += span;
            location.column++;
            type = Type::Middle;

            const std::string_view slice = sources.substr(origin, span);
            const auto [bound, value] = entry(lexicon, slice);
            Logger::fmt(Logger::Type::Lexer, Logger::Level::Traceback,
                        "Lexed <{}> [{}] at {}:{}", std::to_underlying(category), slice,
                        position.line, position.column);
            return Token{bound, category, position, value};
        }

        return {};
    }

    void Lexer::reset() noexcept {
        offset = 0;
        location = {1, 1};
        type = Type::Newline;
        faults.clear();
        Logger::log(Logger::Type::Lexer, Logger::Level::Debug, "Lexer state reset");
    }

}