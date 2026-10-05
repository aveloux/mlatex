/// @file
/// @brief Lexer implementation: TeX's three-state line scanner.
///
/// The interesting part is advance(), which reproduces TeX's rule for what a
/// line ending means: state N yields `\\par`, state S yields nothing, state M
/// yields one space. Getting that wrong does not merely lose paragraph breaks
/// -- `\\par` is also the parser's error-recovery anchor.
#include "syntax/lexer.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <format>
#include <utility>

namespace syntax {

    Lexer::Lexer(const std::string_view source, Catcodes& codes, Lexicon& lexicon)
        : sources(source), table(codes), lexicon(lexicon) {
        Logger::log(Logger::Type::Lexer, Logger::Level::Informative,
                    "Lexer bound to {} byte buffer", source.size());
    }

    bool Lexer::empty() const noexcept {
        return offset >= sources.size() && held.empty();
    }

    Token Lexer::advance() {
        // Read through a plain pointer: this loop runs once per character of
        // every buffer the engine lexes, and a checked accessor per character
        // costs more than the character does.
        const char* const text = sources.data();
        const std::size_t size = sources.size();

        // The text of a \verb, read along with the command itself.
        if (!held.empty()) return std::exchange(held, Token{});

        while (offset < size) {
            // A verbatim block's body, whole: without the rest of the line its
            // \begin stands on when that is empty, and without the line its
            // \end stands on, as LaTeX's verbatim leaves both out.
            if (offset == opening) {
                std::string_view body(text + opening, stop - opening);
                std::size_t first = 0;
                while (first < body.size() && (body[first] == ' ' || body[first] == '\t')) ++first;
                if (first < body.size() && ending(body[first])) {
                    body.remove_prefix(first + (body[first] == '\r' && first + 1 < body.size() &&
                                                body[first + 1] == '\n' ? 2 : 1));
                }
                const std::size_t last = body.find_last_not_of(" \t");
                body = last == std::string_view::npos ? std::string_view{} : body.substr(0, last + 1);
                if (body.ends_with('\n')) body.remove_suffix(1);
                if (body.ends_with('\r')) body.remove_suffix(1);

                const memory::Location position = location;
                for (std::size_t at = opening; at < stop; ++at) {
                    if (text[at] == '\n') {
                        location.line++;
                        location.column = 1;
                    } else {
                        location.column++;
                    }
                }
                offset = stop;
                opening = std::string_view::npos;
                type = Type::Middle;
                return Token{none, Catcodes::Category::Other, position, body};
            }

            const std::size_t origin = offset;
            const memory::Location position = location;
            const char symbol = text[offset];
            const Catcodes::Category category = table.get(symbol);

            if (category == Catcodes::Category::Space && ending(symbol)) {
                offset++;
                if (symbol == '\r' && offset < size && text[offset] == '\n') {
                    offset++;
                }
                location.line++;
                location.column = 1;

                const Type previous = type;

                if (previous == Type::Middle) {
                    type = Type::Newline;
                    const Symbol bound = lexicon.intern(" ");
                    return Token{bound, Catcodes::Category::Space, position, lexicon.resolve(bound)};
                }

                if (previous == Type::Newline) {
                    type = Type::Blank;
                    const Symbol bound = lexicon.intern("\\par");
                    Logger::log(Logger::Type::Lexer, Logger::Level::Debug,
                                "Lexed <Escape> [\\par] at {}:{}", position.line, position.column);
                    return Token{bound, Catcodes::Category::Escape, position, lexicon.resolve(bound)};
                }

                // Every line starts afresh, however the last one ended: a
                // line that ended in spaces, or in a control word, has had
                // its space already, and an empty line after it is still a
                // paragraph break -- which TeX gets by stripping every line's
                // trailing spaces before it reads it. Only a run of empty
                // lines stays one break rather than several.
                if (previous == Type::Skip) type = Type::Newline;
                continue;
            }

            if (category == Catcodes::Category::Space) {
                offset++;
                location.column++;
                if (type == Type::Skip || type == Type::Newline || type == Type::Blank) continue;
                type = Type::Skip;
                const Symbol bound = lexicon.intern(" ");
                return Token{bound, Catcodes::Category::Space, position, lexicon.resolve(bound)};
            }

            if (category == Catcodes::Category::Ignore) {
                offset++;
                location.column++;
                continue;
            }

            if (category == Catcodes::Category::Invalid) {
                tracebacks.emplace_back(Traceback::Type::Token, position,
                                    std::format("Illegal byte 0x{:02X} in input",
                                                static_cast<unsigned>(static_cast<unsigned char>(symbol))));
                Logger::log(Logger::Type::Lexer, Logger::Level::Warning,
                            "Illegal byte 0x{:02X} at {}:{} discarded",
                            static_cast<unsigned>(static_cast<unsigned char>(symbol)),
                            position.line, position.column);
                offset++;
                location.column++;
                continue;
            }

            if (category == Catcodes::Category::Comment) {
                while (offset < size && !ending(text[offset])) {
                    offset++;
                    location.column++;
                }
                if (offset < size) {
                    const char terminator = text[offset];
                    offset++;
                    if (terminator == '\r' && offset < size && text[offset] == '\n') {
                        offset++;
                    }
                    location.line++;
                    location.column = 1;
                }
                type = Type::Newline;
                continue;
            }

            if (category == Catcodes::Category::Escape) {
                offset++;
                location.column++;

                if (offset < size) {
                    const char next = text[offset];

                    if (table.get(next) == Catcodes::Category::Letter) {
                        while (offset < size && table.get(text[offset]) == Catcodes::Category::Letter) {
                            offset++;
                            location.column++;
                        }
                        type = Type::Skip;
                    } else if (ending(next)) {
                        offset++;
                        if (next == '\r' && offset < size && text[offset] == '\n') {
                            offset++;
                        }
                        location.line++;
                        location.column = 1;
                        type = Type::Middle;
                    } else if (table.get(next) == Catcodes::Category::Space) {
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

                const std::string_view slice(text + origin, offset - origin);
                const Symbol bound = lexicon.intern(slice);
                const std::string_view value = lexicon.resolve(bound);
                Logger::log(Logger::Type::Lexer, Logger::Level::Debug,
                            "Lexed <Escape> [{}] at {}:{}", slice, position.line, position.column);

                // Text read as it stands, as TeX reads it once \verb or a
                // verbatim block has made every character an ordinary one:
                // `\verb|a_b $x$|`, fancyvrb's `\Verb` the same way after its
                // options, and `\lstinline` the same way or in braces --
                // `\mintinline{python}{f(x)}` after its language.
                // The text is one token after the command's own, viewing the
                // buffer, which outlives every token lexed from it.
                if (slice == "\\verb" || slice == "\\Verb" || slice == "\\lstinline" || slice == "\\mintinline") {
                    std::size_t at = offset;
                    if (at < size && text[at] == '*' && (slice == "\\verb" || slice == "\\Verb")) ++at;
                    if (at < size && text[at] == '[' && slice != "\\verb") {
                        while (at < size && text[at] != ']' && !ending(text[at])) ++at;
                        if (at < size && text[at] == ']') ++at;
                    }
                    if (at < size && text[at] == '{' && slice == "\\mintinline") {
                        while (at < size && text[at] != '}' && !ending(text[at])) ++at;
                        if (at < size && text[at] == '}') ++at;
                    }
                    if (at < size && !ending(text[at]) && table.get(text[at]) != Catcodes::Category::Letter) {
                        const char close = text[at] == '{' ? '}' : text[at];
                        std::size_t last = at + 1;
                        while (last < size && text[last] != close && !ending(text[last])) ++last;
                        if (last < size && text[last] == close) {
                            held = Token{none, Catcodes::Category::Other,
                                         {location.line, location.column + static_cast<std::uint32_t>(at + 1 - offset)},
                                         std::string_view(text + at + 1, last - at - 1)};
                            location.column += static_cast<std::uint32_t>(last + 1 - offset);
                            offset = last + 1;
                            type = Type::Middle;
                        }
                    }
                }

                // A block whose body is read as it stands -- verbatim,
                // listings' lstlisting, the verbatim package's comment,
                // fancyvrb's, spverbatim's and moreverb's verbatim blocks,
                // acmart's CCSXML, luacode's Lua, the file a filecontents
                // block writes -- is marked here, where the text is still
                // text: the body begins after its name and any options in
                // brackets, and runs to the `\end` that closes it. Everything
                // up to the body is lexed as usual, so \begin reads its name
                // and the block its options.
                if (slice == "\\begin") {
                    static constexpr std::array<std::string_view, 15> blocks{
                        "{verbatim}", "{verbatim*}", "{lstlisting}", "{Verbatim}", "{Verbatim*}", "{BVerbatim}",
                        "{spverbatim}", "{boxedverbatim}", "{comment}", "{minted}", "{filecontents}",
                        "{filecontents*}", "{CCSXML}", "{luacode}", "{luacode*}",
                    };
                    const std::string_view rest = sources.substr(offset);
                    for (const std::string_view name : blocks) {
                        if (!rest.starts_with(name)) continue;
                        std::size_t begin = offset + name.size();
                        if (begin < size && text[begin] == '[') {
                            while (begin < size && text[begin] != ']') ++begin;
                            if (begin < size) ++begin;
                        }
                        // minted's language, after its options, and the name
                        // of the file filecontents writes.
                        if ((name == "{minted}" || name.starts_with("{filecontents")) && begin < size &&
                            text[begin] == '{') {
                            while (begin < size && text[begin] != '}') ++begin;
                            if (begin < size) ++begin;
                        }
                        const std::string closing = std::string("\\end") + std::string(name);
                        const std::size_t end = sources.find(closing, begin);
                        opening = begin;
                        stop = end == std::string_view::npos ? size : end;
                        break;
                    }
                }

                return Token{bound, Catcodes::Category::Escape, position, value};
            }

            const std::size_t span = (static_cast<unsigned char>(symbol) & 0x80) == 0
                                         ? 1
                                         : std::min(length(symbol), size - offset);
            offset += span;
            location.column++;
            type = Type::Middle;

            const std::string_view slice(text + origin, span);
            const Symbol bound = lexicon.intern(slice);
            Logger::log(Logger::Type::Lexer, Logger::Level::Traceback,
                        "Lexed <{}> [{}] at {}:{}", std::to_underlying(category), slice,
                        position.line, position.column);
            return Token{bound, category, position, lexicon.resolve(bound)};
        }

        return {};
    }

}