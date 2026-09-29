/// @file
/// @brief Decimals implementation: exact arithmetic in millionths.
///
/// Values are whole numbers of millionths; see Decimals::Wide for the one
/// step that needs more than sixty-four bits to hold, and how it is done.
#include "syntax/primitives/decimals.hpp"
#include "syntax/primitives/variables.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <cstdint>
#include <format>
#include <limits>

namespace syntax::primitives {

    Decimals::Wide Decimals::multiply(const std::uint64_t left, const std::uint64_t right) noexcept {
        const std::uint64_t mask = 0xFFFFFFFFull;
        const std::uint64_t a = left & mask, b = left >> 32;
        const std::uint64_t c = right & mask, d = right >> 32;

        const std::uint64_t low = a * c;
        const std::uint64_t middle = b * c + (low >> 32);
        const std::uint64_t cross = a * d + (middle & mask);

        return Wide{
            .high = b * d + (middle >> 32) + (cross >> 32),
            .low = (cross << 32) | (low & mask)
        };
    }

    bool Decimals::divide(const Wide dividend, const std::uint64_t divisor, std::uint64_t& quotient) noexcept {
        // A quotient of more than one word starts with a remainder of at
        // least the divisor after the upper word alone.
        if (dividend.high >= divisor) return false;

        std::uint64_t remainder = dividend.high;
        std::uint64_t result = 0;
        for (int bit = 63; bit >= 0; --bit) {
            const bool carry = (remainder >> 63) != 0;
            remainder = (remainder << 1) | ((dividend.low >> bit) & 1u);
            if (carry || remainder >= divisor) {
                remainder -= divisor;
                result |= std::uint64_t{1} << bit;
            }
        }

        // Half away from zero: the remainder is at least half the divisor.
        if (remainder >= divisor - remainder) {
            if (result == std::numeric_limits<std::uint64_t>::max()) return false;
            ++result;
        }
        quotient = result;
        return true;
    }

    bool Decimals::sign(const std::uint64_t value, const bool negative, std::int64_t& result) noexcept {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
        result = negative ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value);
        return true;
    }

    Decimals::Decimals(Lexicon& lexicon) noexcept {
        lexicon.intern("\\calculate");
        lexicon.intern("\\amount");
        lexicon.intern("\\separators");
    }

    std::optional<std::int64_t> Decimals::evaluate(
        const std::string_view text, const Variables& variables, std::string& error
    ) {
        Pass pass{.text = text, .cursor = 0, .variables = variables, .error = error, .level = 0};
        const std::optional<std::int64_t> value = expression(pass);
        if (!value) return std::nullopt;

        // Everything has to have been read: `3 4` is not twelve, or three.
        while (pass.cursor < pass.text.size() && pass.text[pass.cursor] == ' ') ++pass.cursor;
        if (pass.cursor < pass.text.size()) {
            error = std::format("unexpected '{}' in '{}'", pass.text.substr(pass.cursor), text);
            return std::nullopt;
        }
        return value;
    }

    std::optional<std::int64_t> Decimals::expression(Pass& pass) {
        std::optional<std::int64_t> total = term(pass);
        while (total) {
            while (pass.cursor < pass.text.size() && pass.text[pass.cursor] == ' ') ++pass.cursor;
            if (pass.cursor >= pass.text.size()) break;

            const char operation = pass.text[pass.cursor];
            if (operation != '+' && operation != '-') break;
            ++pass.cursor;

            const std::optional<std::int64_t> next = term(pass);
            if (!next) return std::nullopt;

            // Checked before it is done, so an overflow is a report and not a
            // wrapped-around figure.
            const std::int64_t right = operation == '+' ? *next : -*next;
            if ((right > 0 && *total > std::numeric_limits<std::int64_t>::max() - right) ||
                (right < 0 && *total < std::numeric_limits<std::int64_t>::min() - right)) {
                pass.error = "result too large";
                return std::nullopt;
            }
            *total += right;
        }
        return total;
    }

    std::optional<std::int64_t> Decimals::term(Pass& pass) {
        std::optional<std::int64_t> total = unary(pass);
        while (total) {
            while (pass.cursor < pass.text.size() && pass.text[pass.cursor] == ' ') ++pass.cursor;
            if (pass.cursor >= pass.text.size()) break;

            const char operation = pass.text[pass.cursor];
            if (operation != '*' && operation != '/') break;
            ++pass.cursor;

            const std::optional<std::int64_t> next = unary(pass);
            if (!next) return std::nullopt;

            const bool negative = (*total < 0) != (*next < 0);
            std::uint64_t result = 0;

            if (operation == '*') {
                // The product is in millionths squared; one division by a
                // million brings it back, rounding what falls off the end.
                if (!divide(multiply(magnitude(*total), magnitude(*next)), static_cast<std::uint64_t>(scale), result) ||
                    !sign(result, negative, *total)) {
                    pass.error = "result too large";
                    return std::nullopt;
                }
                continue;
            }

            if (*next == 0) {
                pass.error = "division by zero";
                return std::nullopt;
            }
            // Scaled up by a million first, so the quotient keeps its six places.
            if (!divide(multiply(magnitude(*total), static_cast<std::uint64_t>(scale)), magnitude(*next), result) ||
                !sign(result, negative, *total)) {
                pass.error = "result too large";
                return std::nullopt;
            }
        }
        return total;
    }

    std::optional<std::int64_t> Decimals::unary(Pass& pass) {
        while (pass.cursor < pass.text.size() && pass.text[pass.cursor] == ' ') ++pass.cursor;
        if (pass.cursor < pass.text.size() && (pass.text[pass.cursor] == '-' || pass.text[pass.cursor] == '+')) {
            const bool negative = pass.text[pass.cursor] == '-';
            ++pass.cursor;
            const std::optional<std::int64_t> operand = unary(pass);
            if (!operand) return std::nullopt;
            return negative ? -*operand : *operand;
        }
        return primary(pass);
    }

    std::optional<std::int64_t> Decimals::primary(Pass& pass) {
        while (pass.cursor < pass.text.size() && pass.text[pass.cursor] == ' ') ++pass.cursor;
        if (pass.cursor >= pass.text.size()) {
            pass.error = "an operand is missing";
            return std::nullopt;
        }
        if (pass.level >= depth) {
            pass.error = "nested too deeply -- is a variable defined in terms of itself?";
            return std::nullopt;
        }

        const char lead = pass.text[pass.cursor];

        if (lead == '(') {
            ++pass.cursor;
            ++pass.level;
            const std::optional<std::int64_t> inner = expression(pass);
            --pass.level;
            if (!inner) return std::nullopt;

            while (pass.cursor < pass.text.size() && pass.text[pass.cursor] == ' ') ++pass.cursor;
            if (pass.cursor >= pass.text.size() || pass.text[pass.cursor] != ')') {
                pass.error = "a parenthesis is never closed";
                return std::nullopt;
            }
            ++pass.cursor;
            return inner;
        }

        // A name: the variable's value, read as an expression of its own.
        if (naming(lead, true)) {
            const std::size_t opening = pass.cursor;
            while (pass.cursor < pass.text.size() && naming(pass.text[pass.cursor], false)) ++pass.cursor;
            const std::string_view name = pass.text.substr(opening, pass.cursor - opening);

            const std::string* value = pass.variables.get(name);
            if (!value) {
                pass.error = std::format("nothing is set under '{}'", name);
                return std::nullopt;
            }

            Pass nested{.text = *value, .cursor = 0, .variables = pass.variables, .error = pass.error,
                        .level = pass.level + 1};
            const std::optional<std::int64_t> read = expression(nested);
            if (read && nested.text.find_first_not_of(' ', nested.cursor) != std::string_view::npos) {
                pass.error = std::format("'{}' holds '{}', which is not a number", name, *value);
                return std::nullopt;
            }
            return read;
        }

        // A number: digits, then at most one point and the digits after it.
        // Past the sixth place a digit only decides which way the sixth rounds.
        if ((lead >= '0' && lead <= '9') || lead == '.') {
            std::uint64_t whole = 0;
            std::uint64_t fraction = 0;
            int places = 0;
            bool seen = false;       // any digit at all
            bool rounding = false;   // the first digit past the sixth place was five or more

            while (pass.cursor < pass.text.size() && pass.text[pass.cursor] >= '0' && pass.text[pass.cursor] <= '9') {
                seen = true;
                whole = whole * 10 + static_cast<std::uint64_t>(pass.text[pass.cursor] - '0');
                if (whole > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / scale)) {
                    pass.error = "number too large";
                    return std::nullopt;
                }
                ++pass.cursor;
            }
            if (pass.cursor < pass.text.size() && pass.text[pass.cursor] == '.') {
                ++pass.cursor;
                while (pass.cursor < pass.text.size() && pass.text[pass.cursor] >= '0' &&
                       pass.text[pass.cursor] <= '9') {
                    seen = true;
                    const auto digit = static_cast<std::uint64_t>(pass.text[pass.cursor] - '0');
                    if (places < precision) {
                        fraction = fraction * 10 + digit;
                        ++places;
                    } else if (places == precision) {
                        rounding = digit >= 5;
                        ++places;
                    }
                    ++pass.cursor;
                }
            }
            if (!seen) {
                pass.error = "a point with no digits";
                return std::nullopt;
            }

            for (; places < precision; ++places) fraction *= 10;
            std::uint64_t value = whole * static_cast<std::uint64_t>(scale) + fraction + (rounding ? 1 : 0);
            std::int64_t result = 0;
            if (!sign(value, false, result)) {
                pass.error = "number too large";
                return std::nullopt;
            }
            return result;
        }

        pass.error = std::format("'{}' is not a number or a name", lead);
        return std::nullopt;
    }

    std::string Decimals::write(const std::int64_t value) {
        const std::uint64_t size = magnitude(value);
        std::uint64_t fraction = size % static_cast<std::uint64_t>(scale);

        std::string text = value < 0 ? "-" : "";
        text += std::to_string(size / static_cast<std::uint64_t>(scale));
        if (fraction != 0) {
            std::string digits = std::to_string(fraction);
            digits.insert(0, static_cast<std::size_t>(precision) - digits.size(), '0');
            while (!digits.empty() && digits.back() == '0') digits.pop_back();
            text += '.';
            text += digits;
        }
        return text;
    }

    void Decimals::operator()(Mouth& mouth, Context& context) const {
        mouth.bind("\\calculate", [this, &context](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string text = Argument::expanded(mouth);

            std::string error;
            const std::optional<std::int64_t> value = evaluate(text, context.variables, error);
            if (!value) {
                tracebacks.emplace_back(Traceback::Type::Primitive, origin,
                                         std::format("\\calculate: {}", error));
            }
            mouth.ingest(mouth.arena.copy(write(value.value_or(0))));
        });

        mouth.bind("\\amount", [this, &context](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;

            // Places are optional and two by default: an amount is money
            // unless the document says otherwise.
            int places = 2;
            const std::vector<Token> written = mouth.argument(Mouth::Parameter{.optional = true}, 0);
            if (!written.empty()) {
                std::string digits;
                for (const Token& token : written) digits += token.text;
                std::string error;
                const std::optional<std::int64_t> read = evaluate(digits, context.variables, error);
                if (!read || *read < 0 || *read % scale != 0 || *read / scale > precision) {
                    tracebacks.emplace_back(Traceback::Type::Argument, origin,
                                             std::format("\\amount: places must be a whole number from 0 to {}",
                                                         precision));
                } else {
                    places = static_cast<int>(*read / scale);
                }
            }

            const std::string text = Argument::expanded(mouth);
            std::string error;
            const std::optional<std::int64_t> value = evaluate(text, context.variables, error);
            if (!value) {
                tracebacks.emplace_back(Traceback::Type::Primitive, origin, std::format("\\amount: {}", error));
            }
            // Written out: rounded half away from zero to the places shown,
            // on the magnitude, so -2.675 goes to -2.68 as 2.675 goes to
            // 2.68; the whole part grouped in threes, and the point between.
            const std::int64_t amount = value.value_or(0);
            std::uint64_t unit = 1;
            for (int step = places; step < precision; ++step) unit *= 10;
            std::uint64_t size = magnitude(amount);
            size = (size + unit / 2) / unit;

            std::uint64_t divisor = 1;
            for (int step = 0; step < places; ++step) divisor *= 10;
            const std::string whole = std::to_string(size / divisor);

            std::string figures = amount < 0 && size != 0 ? "-" : "";
            for (std::size_t index = 0; index < whole.size(); ++index) {
                if (index > 0 && (whole.size() - index) % 3 == 0) figures += grouping;
                figures += whole[index];
            }
            if (places > 0) {
                std::string digits = std::to_string(size % divisor);
                digits.insert(0, static_cast<std::size_t>(places) - digits.size(), '0');
                figures += point;
                figures += digits;
            }
            mouth.ingest(mouth.arena.copy(figures));
        });

        // Read as written: a separator is text, not something to evaluate.
        mouth.bind("\\separators", [this](Mouth& mouth) {
            grouping = Argument::text(mouth);
            point = Argument::text(mouth);
        });

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Bound the decimal primitives");
    }

}
