/// @file
/// @brief Arithmetic primitive: recursive-descent evaluation of `\\evaluate`.
///
/// Operands go through Number::integer rather than a private digit scanner, so
/// signs, octal, hex, character codes and register references all behave
/// exactly as they do everywhere else in the language.
#include "syntax/primitives/compute.hpp"
#include "syntax/argument.hpp"
#include "syntax/number.hpp"
#include "logger.hpp"

#include <string_view>

#include <format>
#include <string>
#include <vector>

namespace syntax::primitives {

    Compute::Compute(Lexicon& lexicon) noexcept {
        integer = lexicon.intern("\\integer");
        modulo = lexicon.intern("\\mod");
    }

    Token Compute::lookahead(Pass& pass) {
        while (!pass.cursor.empty() &&
               pass.cursor.lookahead(0).category == CatCodes::Category::Space) {
            pass.cursor.advance();
        }
        return pass.cursor.lookahead(0);
    }

    std::int64_t Compute::bound(Pass& pass, const std::int64_t value) {
        if (value > ceiling || value < -ceiling) {
            // Clamped, not malformed: the clamped value is still the most
            // useful answer, so `sound` is left alone and only the report is
            // suppressed after the first one.
            if (!pass.noted) {
                pass.owner.tracebacks_.emplace_back(Traceback::Type::Register, memory::Location{},
                                  std::format("\\evaluate overflowed ({})", value));
                pass.noted = true;
            }
            return value > 0 ? ceiling : -ceiling;
        }
        return value;
    }

    std::int64_t Compute::factor(Pass& pass) {
        const Token lead = lookahead(pass);

        if (lead.is('(')) {
            if (pass.level >= depth) {
                pass.owner.tracebacks_.emplace_back(Traceback::Type::Syntax, lead.location,
                                  "\\evaluate nesting limit reached");
                pass.sound = false;
                return 0;
            }
            pass.cursor.advance();

            pass.level++;
            const std::int64_t inner = expression(pass);
            pass.level--;

            if (const Token closing = lookahead(pass); closing.is(')')) {
                pass.cursor.advance();
            } else if (pass.sound) {
                pass.owner.tracebacks_.emplace_back(Traceback::Type::Syntax, lead.location,
                                  "\\evaluate is missing a ')'");
                pass.sound = false;
            }
            return inner;
        }

        const auto scanned = Number::integer(pass.cursor, pass.registers);
        if (!scanned) {
            if (pass.sound) {
                pass.owner.tracebacks_.emplace_back(Traceback::Type::Syntax, lead.location,
                                  lead.empty()
                                      ? std::string("\\evaluate ended where a number was expected")
                                      : std::format("\\evaluate expected a number, found '{}'", lead.text));
                pass.sound = false;
            }
            return 0;
        }
        return *scanned;
    }

    std::int64_t Compute::power(Pass& pass) {
        const std::int64_t base = factor(pass);

        if (!lookahead(pass).is('^')) {
            return base;
        }
        pass.cursor.advance();

        // Right associative: 2^3^2 is 2^(3^2).
        const std::int64_t exponent = power(pass);

        if (exponent < 0) {
            if (pass.sound) {
                pass.owner.tracebacks_.emplace_back(Traceback::Type::Syntax, memory::Location{},
                                  "\\evaluate exponent must not be negative");
                pass.sound = false;
            }
            return 0;
        }

        std::int64_t total = 1;
        for (std::int64_t step = 0; step < exponent; ++step) {
            total *= base;
            if (total > ceiling || total < -ceiling) {
                return bound(pass, total);
            }
        }
        return total;
    }

    std::int64_t Compute::term(Pass& pass) {
        std::int64_t left = power(pass);

        while (true) {
            const Token next = lookahead(pass);
            const bool remainder = next.symbol == pass.modulo && pass.modulo != none;
            if (!next.is('*') && !next.is('/') && !remainder) break;

            pass.cursor.advance();
            const std::int64_t right = power(pass);

            if (next.is('*')) {
                left = bound(pass, left * right);
                continue;
            }

            if (right == 0) {
                if (pass.sound) {
                    pass.owner.tracebacks_.emplace_back(Traceback::Type::Syntax, next.location,
                                      remainder ? "\\evaluate takes a remainder by zero"
                                                : "\\evaluate divides by zero");
                    pass.sound = false;
                }
                return 0;
            }

            // Truncates toward zero, matching C++ and most languages, rather
            // than TeX's own rounding.
            left = remainder ? left % right : left / right;
        }

        return left;
    }

    std::int64_t Compute::expression(Pass& pass) {
        std::int64_t left = term(pass);

        while (true) {
            const Token next = lookahead(pass);
            if (!next.is('+') && !next.is('-')) break;

            pass.cursor.advance();
            const std::int64_t right = term(pass);
            left = bound(pass, next.is('+') ? left + right : left - right);
        }

        return left;
    }

    void Compute::operator()(Mouth& mouth, Context& context) const {
        mouth.bind("\\evaluate", [this, &context, &mouth](Mouth&) {
            // Expanded first, as TeX's own \numexpr is, so a macro or a
            // conditional inside the expression reads as what it comes to --
            // `\evaluate{\month + 12}` and `\evaluate{\ifnum\month<3 13\else 1\fi}`
            // alike -- while a register stays a register to be read. What it
            // came to goes back in braces to be read as one argument, so the
            // expression arrives as a plain token run and a Cursor over it
            // behaves like any stream.
            const std::string expanded = Argument::expanded(mouth);
            mouth.ingest(mouth.arena().copy("{" + expanded + "}"));
            Cursor stream(mouth.argument({}, 0));

            Pass pass{stream, context.registers, integer, *this, modulo};
            const std::int64_t value = expression(pass);

            if (pass.sound && !lookahead(pass).empty()) {
                tracebacks_.emplace_back(Traceback::Type::Syntax, lookahead(pass).location,
                              std::format("\\evaluate has trailing '{}'", lookahead(pass).text));
            }

            // Inject the result as ordinary digit tokens, so \evaluate reads
            // as a number everywhere a number is written.
            const std::string digits = std::format("{}", pass.sound ? value : 0);

            std::vector<Token> produced;
            produced.reserve(digits.size());
            for (const char symbol : digits) {
                const Symbol bound = mouth.lexicon().intern(std::string_view(&symbol, 1));
                produced.push_back(Token{bound, CatCodes::Category::Other, memory::Location{},
                                         mouth.lexicon().resolve(bound)});
            }
            mouth.stream().inject(produced);
        });

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound arithmetic primitives");
    }

}