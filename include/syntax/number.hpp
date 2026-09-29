#pragma once

#include "syntax/cursor.hpp"
#include "syntax/mouth.hpp"
#include "syntax/semantics/registers.hpp"
#include "syntax/tokens.hpp"
#include "logger.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace syntax {

    /// @brief Scans TeX numerals and dimensions out of the token stream.
    ///
    /// The lexer emits one token per character, so `12` is two tokens, `'17`
    /// is three, and a unit like `pt` is two. Everything here consumes digit
    /// *runs* across tokens rather than looking for a whole numeral inside one
    /// token's text.
    ///
    /// Arithmetic is done in std::int64_t and clamped to TeX's real limits:
    /// #ceiling for integers, #maximum for dimensions. Nothing here can
    /// overflow a 32-bit intermediate.
    ///
    /// @par Two kinds of stream
    /// Every scanner has a Cursor overload, which reads tokens exactly as
    /// written and is what \\evaluate's own arithmetic parser scans its
    /// operands with, and a Mouth overload, which expands the one thing at
    /// the front first -- so `\\vskip\\evaluate{2*6}pt` sees a numeral where
    /// the Cursor overload alone would see an unexpanded `\\evaluate`. Mouth
    /// itself scans no number; this is the whole of how one is read from it.
    ///
    /// @par Precision
    /// A decimal constant is kept as an exact rational -- an integer part plus
    /// a numerator over a power of ten -- and converted once, at the end, by
    /// the unit's own rational factor. Up to eight fraction digits are kept,
    /// which is well past the resolution of a 1/65536 pt grid; further digits
    /// are consumed and discarded, as TeX does.
    class Number {
    public:
        static constexpr std::int32_t scale = 65536;            ///< sp per pt
        static constexpr std::int32_t maximum = 0x3FFFFFFF;     ///< TeX max dimen, in sp
        static constexpr std::int64_t ceiling = 2147483647;     ///< TeX max integer

        /// @brief Scans an integer.
        ///
        /// Grammar:
        /// @code
        /// <optional signs><internal integer | integer constant><one optional space>
        /// @endcode
        ///
        /// An integer constant is a run of decimal digits, `'` followed by
        /// octal digits, `"` followed by hex digits, or `` ` `` followed by a
        /// character (or a single-character control sequence) whose code point
        /// is the value. An internal integer is `\\count<number>` or a control
        /// sequence bound to a Count slot.
        ///
        /// @param cursor    Stream to read from; consumed in place.
        /// @param registers Used to resolve register references, and asked
        ///                  which control sequences name a bank.
        /// @return The value, or std::nullopt when the stream does not start
        ///         with a numeral or a register reference resolves to nothing.
        /// @complexity O(d) in the number of digit tokens consumed.
        [[nodiscard]] static std::optional<std::int32_t> integer(
            Cursor& cursor, const semantics::Registers& registers) {

            const int sign = signs(cursor);
            if (cursor.empty()) return std::nullopt;

            const Token lead = cursor.lookahead(0);

            if (registers.bank(lead.symbol) == semantics::Registers::Type::Count) {
                cursor.advance();
                const auto index = integer(cursor, registers);
                if (!index || *index < 0 || *index > 255) {
                    Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                                "Count register index out of range 0-255");
                    return std::nullopt;
                }
                return saturate(sign * static_cast<std::int64_t>(
                    registers.get(semantics::Registers::Type::Count, static_cast<std::size_t>(*index))));
            }

            if (lead.category == CatCodes::Category::Escape) {
                const auto target = registers.target(lead.symbol);
                if (!target) {
                    Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                                "'{}' is not a number and is not bound to a register", lead.text);
                    return std::nullopt;
                }
                cursor.advance();
                const auto value = static_cast<std::int64_t>(registers.get(target->type, target->slot));
                space(cursor);
                return saturate(sign * value);
            }

            std::int64_t value = 0;
            std::size_t counted = 0;

            if (lead.is('\'')) {
                cursor.advance();
                value = run(cursor, 8, counted);
            } else if (lead.is('"')) {
                cursor.advance();
                value = run(cursor, 16, counted);
            } else if (lead.is('`')) {
                cursor.advance();
                if (cursor.empty()) return std::nullopt;
                const Token target = cursor.advance();
                std::string_view text = target.text;
                if (target.category == CatCodes::Category::Escape && text.size() > 1) {
                    text.remove_prefix(1);
                }
                if (text.empty()) return std::nullopt;
                value = codepoint(text);
                counted = 1;
            } else {
                value = run(cursor, 10, counted);
            }

            if (counted == 0) {
                return std::nullopt;
            }

            space(cursor);
            return saturate(sign * value);
        }

        /// @brief Scans a dimension, in scaled points.
        ///
        /// Grammar:
        /// @code
        /// <optional signs><internal dimen | decimal constant><unit><one optional space>
        /// @endcode
        ///
        /// The sign is taken once, up front, and applied to the finished
        /// magnitude -- applying it to the integer part alone is what made
        /// `-1.5pt` come out as -0.5pt.
        ///
        /// An internal dimen is `\\dimen<number>` or a control sequence bound
        /// to a Dimension or Glue slot, and carries no unit. A control
        /// sequence bound to a Count slot, or `\\count<number>`, is instead a
        /// *factor* and still needs a unit, so `\\dimen0=\\count5 pt` works.
        ///
        /// @param cursor    Stream to read from; consumed in place.
        /// @param registers Used to resolve register references, and asked
        ///                  which control sequences name a bank.
        /// @return The value in scaled points, clamped to +/-#maximum, or
        ///         std::nullopt when no numeral is present or the unit is
        ///         font-relative.
        /// @complexity O(d) in the number of digit tokens consumed.
        [[nodiscard]] static std::optional<std::int32_t> dimension(
            Cursor& cursor, const semantics::Registers& registers) {

            int sign = signs(cursor);
            if (cursor.empty()) return std::nullopt;

            const Token lead = cursor.lookahead(0);
            const auto named = registers.bank(lead.symbol);

            // Any bank but the integers' holds a length: `\dimen3`, `\skip2`
            // by its natural width, `\wd0` a box's width.
            if (named && *named != semantics::Registers::Type::Count) {
                cursor.advance();
                const auto index = integer(cursor, registers);
                if (!index || *index < 0 || *index > 255) {
                    Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                                "Dimension register index out of range 0-255");
                    return std::nullopt;
                }
                return saturate(sign * static_cast<std::int64_t>(
                    registers.get(*named, static_cast<std::size_t>(*index))));
            }

            std::int64_t whole = 0;
            std::size_t counted = 0;
            bool factored = false;   // an internal integer still needs a unit

            if (named == semantics::Registers::Type::Count) {
                const auto scanned = integer(cursor, registers);
                if (!scanned) return std::nullopt;
                whole = *scanned;
                counted = 1;
                factored = true;
            } else if (lead.category == CatCodes::Category::Escape) {
                const auto target = registers.target(lead.symbol);
                if (!target) {
                    Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                                "'{}' is not a dimension and is not bound to a register", lead.text);
                    return std::nullopt;
                }
                cursor.advance();
                if (target->type != semantics::Registers::Type::Count) {
                    const auto value = static_cast<std::int64_t>(registers.get(target->type, target->slot));
                    space(cursor);
                    return saturate(sign * value);
                }
                whole = registers.get(semantics::Registers::Type::Count, target->slot);
                counted = 1;
                factored = true;
            } else {
                whole = run(cursor, 10, counted);
            }

            if (whole < 0) {
                sign = -sign;
                whole = -whole;
            }

            std::int64_t numerator = 0;
            std::int64_t denominator = 1;
            std::size_t decimals = 0;

            if (!factored && !cursor.empty() &&
                (cursor.lookahead(0).is('.') || cursor.lookahead(0).is(','))) {
                cursor.advance();
                while (!cursor.empty() && decimals < 8) {
                    int digit = 0;
                    if (!decode(cursor.lookahead(0), 10, digit)) break;
                    cursor.advance();
                    numerator = numerator * 10 + digit;
                    denominator *= 10;
                    decimals++;
                }
                while (!cursor.empty()) {
                    int digit = 0;
                    if (!decode(cursor.lookahead(0), 10, digit)) break;
                    cursor.advance();
                }
            }

            if (counted == 0 && decimals == 0) {
                return std::nullopt;
            }

            // A register for a unit, as TeX has it: `0.4\textwidth` is four
            // tenths of whatever that register holds.
            std::optional<Measure> measure;
            blanks(cursor);
            if (const Token after = cursor.lookahead(0); after.category == CatCodes::Category::Escape) {
                if (const auto held = registers.target(after.symbol);
                    held && held->type != semantics::Registers::Type::Count) {
                    cursor.advance();
                    measure = Measure{registers.get(held->type, held->slot), 1};
                } else if (const auto kind = registers.bank(after.symbol);
                           kind && *kind != semantics::Registers::Type::Count) {
                    // A bank and its slot: `0.5\wd0`.
                    cursor.advance();
                    const auto index = integer(cursor, registers);
                    if (index && *index >= 0 && *index <= 255) {
                        measure = Measure{registers.get(*kind, static_cast<std::size_t>(*index)), 1};
                    }
                }
            }
            if (!measure) measure = unit(cursor, registers.quad());
            if (!measure) {
                return std::nullopt;
            }

            const std::int64_t base = whole * measure->numerator / measure->denominator;

            std::int64_t part = 0;
            if (numerator > 0) {
                const std::int64_t divisor = denominator * measure->denominator;
                part = (numerator * measure->numerator + divisor / 2) / divisor;
            }

            std::int64_t total = base + part;

            if (total > maximum) {
                Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                            "Dimension too large ({} sp); clamped to 16383.99998pt", total);
                total = maximum;
            }

            space(cursor);
            return static_cast<std::int32_t>(sign * total);
        }

        /// @brief Scans an integer from a Mouth's own stream.
        ///
        /// A number may be written as a macro or as `\\evaluate{6*7}`, and
        /// integer(Cursor&, ...) reads tokens as they stand, so whatever
        /// expands at the front is expanded first here -- once is enough,
        /// since expand() stops at the first token that does not and this
        /// puts that one back for the Cursor overload to read.
        ///
        /// @param mouth     Expander to scan from.
        /// @param registers Bank to resolve register references against, and
        ///                  to ask which control sequences name a bank.
        /// @return The value, or std::nullopt when the stream does not start with one.
        /// @complexity O(d) in the number of digit tokens consumed.
        [[nodiscard]] static std::optional<std::int32_t> integer(
            Mouth& mouth, const semantics::Registers& registers) {
            settle(mouth);
            return integer(mouth.stream(), registers);
        }

        /// @brief Scans a dimension from a Mouth's own stream.
        /// @param mouth     Expander to scan from.
        /// @param registers Bank to resolve register references against, and
        ///                  to ask which control sequences name a bank.
        /// @return The value in scaled points, or std::nullopt.
        /// @complexity O(d) in the number of digit tokens consumed.
        [[nodiscard]] static std::optional<std::int32_t> dimension(
            Mouth& mouth, const semantics::Registers& registers) {
            settle(mouth);
            return dimension(mouth.stream(), registers);
        }

    private:
        /// @brief Expands the one macro or primitive at the front, if there is one.
        ///
        /// A Mouth's stream may start with something that has not been
        /// expanded yet -- `\\evaluate{...}`, or a user macro standing for a
        /// number -- and the Cursor-based scanners below read tokens as
        /// written, not as they would expand. This is the one step that
        /// bridges the two: it costs nothing when the stream already starts
        /// with a numeral.
        ///
        /// @param mouth Expander to settle.
        /// @complexity O(1) amortized: at most one expansion.
        static void settle(Mouth& mouth) noexcept {
            Cursor& cursor = mouth.stream();
            while (!cursor.empty() && cursor.lookahead(0).category == CatCodes::Category::Space) {
                cursor.advance();
            }
            if (const Token next = cursor.lookahead(0);
                next.category == CatCodes::Category::Escape &&
                (mouth.macro(next.symbol) || mouth.handler(next.symbol))) {
                if (const Token produced = mouth.expand(); !produced.empty()) {
                    cursor.inject(std::span{&produced, 1});
                }
            }
        }

        /// @brief One unit, as an exact rational number of scaled points.
        struct Measure {
            std::int64_t numerator;     ///< sp
            std::int64_t denominator;
        };

        /// @brief Skips a run of spaces.
        /// @param cursor Stream to read from.
        static void blanks(Cursor& cursor) noexcept {
            while (!cursor.empty() && cursor.lookahead(0).category == CatCodes::Category::Space) {
                cursor.advance();
            }
        }

        /// @brief Skips the single optional space that terminates a numeral.
        /// @param cursor Stream to read from.
        /// @complexity O(1).
        static void space(Cursor& cursor) noexcept {
            if (!cursor.empty() && cursor.lookahead(0).category == CatCodes::Category::Space) {
                cursor.advance();
            }
        }

        /// @brief Consumes `<optional signs>`: any mix of + and - with spaces between.
        /// @param cursor Stream to read from.
        /// @return 1 or -1.
        [[nodiscard]] static int signs(Cursor& cursor) noexcept {
            int sign = 1;
            while (!cursor.empty()) {
                blanks(cursor);
                if (cursor.empty()) break;
                const Token token = cursor.lookahead(0);
                if (token.is('+')) {
                    cursor.advance();
                } else if (token.is('-')) {
                    sign = -sign;
                    cursor.advance();
                } else {
                    break;
                }
            }
            return sign;
        }

        /// @brief Reads one token as a digit in a given base.
        /// @param token Token to interpret.
        /// @param base  8, 10 or 16.
        /// @param digit Receives the value on success.
        /// @return True when the token is a single valid digit for that base.
        /// @complexity O(1).
        [[nodiscard]] static bool decode(const Token& token, const int base, int& digit) noexcept {
            if (token.text.size() != 1) return false;
            const char symbol = token.text[0];

            if (symbol >= '0' && symbol <= '9') digit = symbol - '0';
            else if (base == 16 && symbol >= 'A' && symbol <= 'F') digit = symbol - 'A' + 10;
            else if (base == 16 && symbol >= 'a' && symbol <= 'f') digit = symbol - 'a' + 10;
            else return false;

            return digit < base;   // rejects 8 and 9 in octal
        }

        /// @brief Consumes a run of digit tokens.
        /// @param cursor  Stream to read from.
        /// @param base    8, 10 or 16.
        /// @param counted Receives how many digits were consumed; 0 means none.
        /// @return The accumulated value, clamped at #ceiling.
        /// @complexity O(d).
        [[nodiscard]] static std::int64_t run(Cursor& cursor, const int base, std::size_t& counted) noexcept {
            std::int64_t total = 0;
            counted = 0;

            while (!cursor.empty()) {
                int digit = 0;
                if (!decode(cursor.lookahead(0), base, digit)) break;
                cursor.advance();
                counted++;
                if (total <= ceiling) {
                    total = total * base + digit;
                }
                if (total > ceiling) {
                    total = ceiling;
                }
            }

            return total;
        }

        /// @brief Decodes the first UTF-8 character of a token's text.
        /// @param text Non-empty text.
        /// @return Its code point, or the lead byte when the encoding is malformed.
        /// @complexity O(1): at most four bytes.
        [[nodiscard]] static std::int64_t codepoint(const std::string_view text) noexcept {
            const auto lead = static_cast<unsigned char>(text[0]);
            if ((lead & 0x80) == 0) return lead;

            std::size_t width = 1;
            std::uint32_t point = 0;
            if ((lead & 0xE0) == 0xC0) { width = 2; point = lead & 0x1Fu; }
            else if ((lead & 0xF0) == 0xE0) { width = 3; point = lead & 0x0Fu; }
            else if ((lead & 0xF8) == 0xF0) { width = 4; point = lead & 0x07u; }
            else return lead;

            if (text.size() < width) return lead;
            for (std::size_t index = 1; index < width; ++index) {
                const auto next = static_cast<unsigned char>(text[index]);
                if ((next & 0xC0) != 0x80) return lead;
                point = (point << 6) | (next & 0x3Fu);
            }
            return point;
        }

        /// @brief Reads a two-letter unit of measure.
        ///
        /// The unit is two tokens, never one: testing a single token's length
        /// against 2 can never succeed, which is why every dimension used to
        /// keep its pt value.
        ///
        /// The two font-relative units are measured against @p quad, the width
        /// of an em in the face in use: `1em` is that, and `1ex` is taken as
        /// 0.43 of it, the x-height of the Computer Modern faces.
        ///
        /// @param cursor Stream to read from.
        /// @param quad   Width of an em, in scaled points.
        /// @return The unit's rational factor. An unrecognised unit is reported
        ///         and treated as pt, as TeX does.
        /// @complexity O(1): at most six lookaheads.
        [[nodiscard]] static std::optional<Measure> unit(Cursor& cursor, const std::int32_t quad) {
            blanks(cursor);

            if (prefix(cursor)) {
                blanks(cursor);
            }

            char first = 0;
            char second = 0;
            if (!pair(cursor, first, second)) {
                Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                            "Illegal unit of measure; pt assumed");
                return Measure{scale, 1};
            }

            const auto key = static_cast<std::uint16_t>(static_cast<unsigned char>(first) << 8 |
                                                        static_cast<unsigned char>(second));

            switch (key) {
                case 'p' << 8 | 't': consume(cursor); return Measure{scale, 1};                    // 1 pt
                case 's' << 8 | 'p': consume(cursor); return Measure{1, 1};                        // 1 sp
                case 'p' << 8 | 'c': consume(cursor); return Measure{scale * 12LL, 1};             // 1 pc = 12 pt
                case 'i' << 8 | 'n': consume(cursor); return Measure{scale * 7227LL, 100};         // 1 in = 72.27 pt
                case 'b' << 8 | 'p': consume(cursor); return Measure{scale * 7227LL, 7200};        // 1 bp = 72.27/72 pt
                case 'c' << 8 | 'm': consume(cursor); return Measure{scale * 7227LL, 254};         // 1 cm = 72.27/2.54 pt
                case 'm' << 8 | 'm': consume(cursor); return Measure{scale * 7227LL, 2540};        // 1 mm = 72.27/25.4 pt
                case 'd' << 8 | 'd': consume(cursor); return Measure{scale * 1238LL, 1157};        // 1 dd = 1238/1157 pt
                case 'c' << 8 | 'c': consume(cursor); return Measure{scale * 14856LL, 1157};       // 1 cc = 12 dd

                case 'e' << 8 | 'm': consume(cursor); return Measure{quad, 1};                     // 1 em
                case 'e' << 8 | 'x': consume(cursor); return Measure{quad * 43LL, 100};            // 1 ex = 0.43 em

                default:
                    Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                                "Illegal unit of measure; pt assumed");
                    return Measure{scale, 1};
            }
        }

        /// @brief Consumes the two tokens of a unit.
        /// @param cursor Stream to read from.
        /// @complexity O(1).
        static void consume(Cursor& cursor) noexcept {
            cursor.advance();
            cursor.advance();
        }

        /// @brief Peeks at two adjacent letter tokens.
        /// @param cursor Stream to inspect; not consumed.
        /// @param first  Receives the first letter, lowercased.
        /// @param second Receives the second letter, lowercased.
        /// @return True when the next two tokens are single letters.
        /// @complexity O(1).
        [[nodiscard]] static bool pair(const Cursor& cursor, char& first, char& second) noexcept {
            const Token one = cursor.lookahead(0);
            const Token two = cursor.lookahead(1);
            if (one.text.size() != 1 || two.text.size() != 1) return false;
            if (one.category != CatCodes::Category::Letter || two.category != CatCodes::Category::Letter) {
                return false;
            }
            first = lower(one.text[0]);
            second = lower(two.text[0]);
            return true;
        }

        /// @brief Consumes an optional `true` prefix before a unit.
        ///
        /// `truept` disables `\\mag` scaling. With `\\mag` at its default of
        /// 1000 the two are identical, so the prefix is accepted and skipped.
        ///
        /// @param cursor Stream to read from; consumed only on a match.
        /// @return True when the prefix was present and consumed.
        /// @complexity O(1): four lookaheads.
        [[nodiscard]] static bool prefix(Cursor& cursor) noexcept {
            constexpr std::string_view text = "true";

            for (std::size_t index = 0; index < text.size(); ++index) {
                const Token token = cursor.lookahead(index);
                if (token.text.size() != 1 || lower(token.text[0]) != text[index]) {
                    return false;
                }
            }
            for (std::size_t index = 0; index < text.size(); ++index) {
                cursor.advance();
            }
            return true;
        }

        /// @brief ASCII lowercase.
        /// @param symbol Character to fold.
        /// @return The lowercase form, or the character unchanged.
        [[nodiscard]] static constexpr char lower(const char symbol) noexcept {
            return (symbol >= 'A' && symbol <= 'Z') ? static_cast<char>(symbol - 'A' + 'a') : symbol;
        }

        /// @brief Clamps to the 32-bit integer range TeX allows.
        /// @param value Value to clamp.
        /// @return The value, bounded to +/-#ceiling.
        [[nodiscard]] static std::int32_t saturate(const std::int64_t value) noexcept {
            if (value > ceiling) return static_cast<std::int32_t>(ceiling);
            if (value < -ceiling) return static_cast<std::int32_t>(-ceiling);
            return static_cast<std::int32_t>(value);
        }
    };

}