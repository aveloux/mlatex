/// @file
/// @brief Strings implementation: xstring's tests and functions, each one
///        native command over the expanded text.
#include "syntax/primitives/strings.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace syntax::primitives {

    Strings::Strings(Lexicon& lexicon) noexcept {
        static constexpr std::array<std::string_view, 21> names{
            "\\IfSubStr", "\\IfBeginWith", "\\IfEndWith", "\\IfStrEq", "\\IfEq", "\\IfInteger", "\\IfDecimal",
            "\\StrLen", "\\StrLeft", "\\StrRight", "\\StrMid", "\\StrChar", "\\StrGobbleLeft", "\\StrGobbleRight",
            "\\StrBefore", "\\StrBehind", "\\StrBetween", "\\StrSubstitute", "\\StrDel", "\\StrCount",
            "\\StrPosition",
        };
        for (const std::string_view name : names) lexicon.intern(name);
    }

    void Strings::operator()(Mouth& mouth, Context&) const {
        // The text as its characters, each UTF-8 sequence whole.
        const auto characters = [](const std::string_view text) {
            std::vector<std::string_view> list;
            for (std::size_t at = 0; at < text.size();) {
                const auto lead = static_cast<unsigned char>(text[at]);
                const std::size_t span = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
                list.push_back(text.substr(at, std::min(span, text.size() - at)));
                at += span;
            }
            return list;
        };
        // Characters `from` up to, not including, `to`, joined again.
        const auto join = [](const std::vector<std::string_view>& list, const std::size_t from, const std::size_t to) {
            std::string text;
            for (std::size_t at = from; at < std::min(to, list.size()); ++at) text += list[at];
            return text;
        };
        // How many characters a run of bytes holds.
        const auto length = [](const std::string_view text) {
            return static_cast<std::size_t>(std::ranges::count_if(
                text, [](const char byte) { return (static_cast<unsigned char>(byte) & 0xC0) != 0x80; }));
        };
        // A count as written, or none for text that is not a whole number --
        // which is reported where it stands.
        const auto number = [this](Mouth& mouth, const std::string_view text, const std::string_view name) {
            std::string_view trimmed = text;
            while (!trimmed.empty() && trimmed.front() == ' ') trimmed.remove_prefix(1);
            while (!trimmed.empty() && trimmed.back() == ' ') trimmed.remove_suffix(1);
            long long value = 0;
            const auto [end, fault] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
            if (fault != std::errc{} || end != trimmed.data() + trimmed.size()) {
                tracebacks.emplace_back(Traceback::Type::Argument, mouth.lookahead().location,
                                         std::format("{} needs a whole number, not '{}'", name, text));
                return 0LL;
            }
            return value;
        };
        // Where the n-th time `part` stands in `text` starts, counted from
        // the first, in bytes; npos when it stands there fewer times.
        const auto locate = [](const std::string& text, const std::string& part, const std::size_t nth,
                               const std::size_t from) {
            if (part.empty() || nth == 0) return std::string::npos;
            std::size_t at = text.find(part, from);
            for (std::size_t seen = 1; at != std::string::npos && seen < nth; ++seen) {
                at = text.find(part, at + part.size());
            }
            return at;
        };
        // The counts in brackets before the arguments, `[2]` or `[1,2]`:
        // which occurrence each is of; `fallback` for one not given.
        const auto which = [number](Mouth& mouth, const std::string_view name, const long long fallback) {
            std::string written;
            for (const Token& token : mouth.argument(Mouth::Parameter{.optional = true}, 0)) written += token.text;
            std::pair counts{fallback, fallback};
            if (written.empty()) return counts;
            const std::size_t comma = written.find(',');
            counts.first = number(mouth, std::string_view(written).substr(0, comma), name);
            if (comma != std::string::npos) counts.second = number(mouth, std::string_view(written).substr(comma + 1), name);
            return counts;
        };
        // A function's result: set where it stands, or kept in the macro
        // named in brackets after its arguments, `\\StrLen{word}[\\size]`.
        const auto give = [](Mouth& mouth, const std::string& result) {
            const std::vector<Token> named = mouth.argument(Mouth::Parameter{.optional = true}, 0);
            const auto macro = std::ranges::find(named, Catcodes::Category::Escape, &Token::category);
            if (macro != named.end()) {
                mouth.ingest(mouth.arena.copy("\\@define" + std::string(macro->text) + "{" + result + "}"),
                             memory::Location{});
                return;
            }
            mouth.ingest(mouth.arena.copy(result), memory::Location{});
        };
        // A test's two branches, and the one that holds put back to be read.
        const auto branch = [](Mouth& mouth, const bool held) {
            const std::vector<Token> yes = mouth.argument({}, 1);
            const std::vector<Token> no = mouth.argument({}, 1);
            const std::vector<Token>& chosen = held ? yes : no;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        };
        // A starred test reads its arguments as written; the text is the same.
        const auto star = [](Mouth& mouth) {
            if (mouth.lookahead().is('*')) mouth.read();
        };

        // --- The tests ---------------------------------------------------------
        mouth.bind("\\IfSubStr", [star, which, locate, branch](Mouth& mouth) {
            star(mouth);
            const long long nth = which(mouth, "\\IfSubStr", 1).first;
            const std::string text = Argument::expanded(mouth);
            const std::string part = Argument::expanded(mouth);
            branch(mouth, locate(text, part, static_cast<std::size_t>(std::max(nth, 1LL)), 0) != std::string::npos);
        });
        mouth.bind("\\IfBeginWith", [star, branch](Mouth& mouth) {
            star(mouth);
            const std::string text = Argument::expanded(mouth);
            const std::string part = Argument::expanded(mouth);
            branch(mouth, text.starts_with(part));
        });
        mouth.bind("\\IfEndWith", [star, branch](Mouth& mouth) {
            star(mouth);
            const std::string text = Argument::expanded(mouth);
            const std::string part = Argument::expanded(mouth);
            branch(mouth, text.ends_with(part));
        });
        mouth.bind("\\IfStrEq", [star, branch](Mouth& mouth) {
            star(mouth);
            const std::string left = Argument::expanded(mouth);
            const std::string right = Argument::expanded(mouth);
            branch(mouth, left == right);
        });
        // Two numbers compare as numbers, `1.0` as `1`; anything else as text.
        mouth.bind("\\IfEq", [star, branch](Mouth& mouth) {
            star(mouth);
            const std::string left = Argument::expanded(mouth);
            const std::string right = Argument::expanded(mouth);
            double first = 0.0;
            double second = 0.0;
            const auto one = std::from_chars(left.data(), left.data() + left.size(), first);
            const auto two = std::from_chars(right.data(), right.data() + right.size(), second);
            const bool numeric = !left.empty() && !right.empty() && one.ec == std::errc{} &&
                                 one.ptr == left.data() + left.size() && two.ec == std::errc{} &&
                                 two.ptr == right.data() + right.size();
            branch(mouth, numeric ? first == second : left == right);
        });
        // A whole number, signed or not; a decimal one, its point a full
        // stop or a comma.
        for (const bool whole : {true, false}) {
            mouth.bind(whole ? "\\IfInteger" : "\\IfDecimal", [star, branch, whole](Mouth& mouth) {
                star(mouth);
                const std::string written = Argument::expanded(mouth);
                std::string_view text = written;
                if (!text.empty() && (text.front() == '-' || text.front() == '+')) text.remove_prefix(1);
                bool digits = false;
                bool point = false;
                bool held = !text.empty();
                for (const char letter : text) {
                    if (letter >= '0' && letter <= '9') {
                        digits = true;
                    } else if ((letter == '.' || letter == ',') && !whole && !point) {
                        point = true;
                    } else {
                        held = false;
                    }
                }
                branch(mouth, held && digits);
            });
        }

        // --- The functions -----------------------------------------------------
        mouth.bind("\\StrLen", [length, give](Mouth& mouth) {
            give(mouth, std::to_string(length(Argument::expanded(mouth))));
        });

        // So many characters off one end or the other: kept, or let go.
        enum class Cut { Left, Right, GobbleLeft, GobbleRight };
        for (const auto& [name, cut] : {std::pair{"\\StrLeft", Cut::Left}, std::pair{"\\StrRight", Cut::Right},
                                        std::pair{"\\StrGobbleLeft", Cut::GobbleLeft},
                                        std::pair{"\\StrGobbleRight", Cut::GobbleRight}}) {
            mouth.bind(name, [characters, join, number, give, name, cut](Mouth& mouth) {
                const std::string text = Argument::expanded(mouth);
                const auto list = characters(text);
                const long long asked = number(mouth, Argument::expanded(mouth), name);
                const std::size_t many = static_cast<std::size_t>(std::clamp<long long>(asked, 0, static_cast<long long>(list.size())));
                std::string result;
                switch (cut) {
                    case Cut::Left: result = join(list, 0, many); break;
                    case Cut::Right: result = join(list, list.size() - many, list.size()); break;
                    case Cut::GobbleLeft: result = join(list, many, list.size()); break;
                    case Cut::GobbleRight: result = join(list, 0, list.size() - many); break;
                }
                give(mouth, result);
            });
        }
        // The characters from one place to another, both kept, counted from 1.
        mouth.bind("\\StrMid", [characters, join, number, give](Mouth& mouth) {
            const std::string text = Argument::expanded(mouth);
            const auto list = characters(text);
            const long long from = number(mouth, Argument::expanded(mouth), "\\StrMid");
            const long long to = number(mouth, Argument::expanded(mouth), "\\StrMid");
            give(mouth, from < 1 || to < from ? std::string{}
                                              : join(list, static_cast<std::size_t>(from - 1), static_cast<std::size_t>(to)));
        });
        mouth.bind("\\StrChar", [characters, join, number, give](Mouth& mouth) {
            const std::string text = Argument::expanded(mouth);
            const auto list = characters(text);
            const long long at = number(mouth, Argument::expanded(mouth), "\\StrChar");
            give(mouth, at < 1 ? std::string{} : join(list, static_cast<std::size_t>(at - 1), static_cast<std::size_t>(at)));
        });

        // What stands before or after the n-th time a piece does; nothing
        // when it stands there fewer times.
        for (const bool before : {true, false}) {
            const std::string_view name = before ? "\\StrBefore" : "\\StrBehind";
            mouth.bind(name, [which, locate, give, before, name](Mouth& mouth) {
                const long long nth = which(mouth, name, 1).first;
                const std::string text = Argument::expanded(mouth);
                const std::string part = Argument::expanded(mouth);
                const std::size_t at = locate(text, part, static_cast<std::size_t>(std::max(nth, 1LL)), 0);
                give(mouth, at == std::string::npos ? std::string{}
                            : before                ? text.substr(0, at)
                                                    : text.substr(at + part.size()));
            });
        }
        // Between the n-th time one piece stands and the m-th time the other
        // stands after it.
        mouth.bind("\\StrBetween", [which, locate, give](Mouth& mouth) {
            const auto [first, second] = which(mouth, "\\StrBetween", 1);
            const std::string text = Argument::expanded(mouth);
            const std::string opening = Argument::expanded(mouth);
            const std::string closing = Argument::expanded(mouth);
            const std::size_t open = locate(text, opening, static_cast<std::size_t>(std::max(first, 1LL)), 0);
            const std::size_t start = open == std::string::npos ? open : open + opening.size();
            const std::size_t stop = start == std::string::npos
                                         ? start
                                         : locate(text, closing, static_cast<std::size_t>(std::max(second, 1LL)), start);
            give(mouth, stop == std::string::npos ? std::string{} : text.substr(start, stop - start));
        });
        // Every time a piece stands -- or the first n times -- replaced by
        // another, or taken out.
        for (const bool replacing : {true, false}) {
            const std::string_view name = replacing ? "\\StrSubstitute" : "\\StrDel";
            mouth.bind(name, [which, give, replacing, name](Mouth& mouth) {
                const long long many = which(mouth, name, 0).first;
                std::string text = Argument::expanded(mouth);
                const std::string part = Argument::expanded(mouth);
                const std::string replacement = replacing ? Argument::expanded(mouth) : std::string{};
                long long done = 0;
                for (std::size_t at = part.empty() ? std::string::npos : text.find(part);
                     at != std::string::npos && (many <= 0 || done < many); at = text.find(part, at + replacement.size())) {
                    text.replace(at, part.size(), replacement);
                    ++done;
                }
                give(mouth, text);
            });
        }
        mouth.bind("\\StrCount", [give](Mouth& mouth) {
            const std::string text = Argument::expanded(mouth);
            const std::string part = Argument::expanded(mouth);
            std::size_t seen = 0;
            for (std::size_t at = part.empty() ? std::string::npos : text.find(part); at != std::string::npos;
                 at = text.find(part, at + part.size())) {
                ++seen;
            }
            give(mouth, std::to_string(seen));
        });
        // Where the n-th time a piece stands starts, counted in characters
        // from 1; 0 when it stands there fewer times.
        mouth.bind("\\StrPosition", [which, locate, length, give](Mouth& mouth) {
            const long long nth = which(mouth, "\\StrPosition", 1).first;
            const std::string text = Argument::expanded(mouth);
            const std::string part = Argument::expanded(mouth);
            const std::size_t at = locate(text, part, static_cast<std::size_t>(std::max(nth, 1LL)), 0);
            give(mouth, at == std::string::npos ? std::string("0") : std::to_string(length(text.substr(0, at)) + 1));
        });

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Bound the string primitives");
    }

}
