/// @file
/// @brief Register primitives: `\\set`, `\\increase`, `\\reduce`, `\\scale`, `\\name`.
#include "syntax/primitives/values.hpp"
#include "syntax/primitives/scanning.hpp"
#include "logger.hpp"

#include <string_view>

#include <format>

namespace syntax::primitives {

    void Values::fault(const Traceback::Type type, const memory::Location location,
                   const std::string_view message) const {
        Logger::log(Logger::Type::Semantics, Logger::Level::Error, message);
        faults.emplace_back(type, location, message);
    }

    Values::Values(Lexicon& names) noexcept {
        integer = names.intern("\\integer");
        length = names.intern("\\length");
        glue = names.intern("\\glue");
    }

    std::optional<semantics::Registers::Type> Values::bank(const Symbol symbol) const noexcept {
        using Type = semantics::Registers::Type;
        if (symbol == integer) return Type::Count;
        if (symbol == length) return Type::Dimension;
        if (symbol == glue) return Type::Glue;
        return std::nullopt;
    }

    void Values::skip(Mouth& mouth, const std::string_view text) {
        while (mouth.lookahead().category == CatCodes::Category::Space) {
            mouth.read();
        }
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (!mouth.lookahead(index).is(text[index])) return;
        }
        for (std::size_t index = 0; index < text.size(); ++index) {
            mouth.read();
        }
    }

    std::optional<semantics::Registers::Target> Values::slot(Mouth& mouth, Context& context) const {
        const Token lead = mouth.read();
        if (lead.category != CatCodes::Category::Escape) {
            fault(Traceback::Type::Register, lead.location,
                         "expected a register reference");
            return std::nullopt;
        }

        // A bound name addresses its slot directly: \set\tally = 7.
        if (const auto bound = context.ledger.target(lead.symbol)) {
            return bound;
        }

        const auto kind = bank(lead.symbol);
        if (!kind) {
            fault(Traceback::Type::Register, lead.location,
                         std::format("{} is not a register", lead.values));
            return std::nullopt;
        }

        prime(mouth);
        const auto index = mouth.integer(context.ledger, integer);
        if (!index || *index < 0 || *index >= static_cast<std::int32_t>(semantics::Registers::slots)) {
            fault(Traceback::Type::Register, lead.location,
                         std::format("{} needs a slot number between 0 and {}",
                                     lead.values, semantics::Registers::slots - 1));
            return std::nullopt;
        }

        return semantics::Registers::Target{*kind, static_cast<std::size_t>(*index)};
    }

    void Values::operator()(Mouth& mouth, Context& context) const {
        using Type = semantics::Registers::Type;

        // Reading a value depends on the bank: an integer slot scans an
        // integer, a dimension or glue slot scans a dimension.
        const auto value = [this, &context](Mouth& mouth, const Type kind) -> std::optional<std::int32_t> {
            // A value may be produced by \evaluate or a macro; Number scans
            // rather than expands, so the stream is settled first.
            prime(mouth);
            if (kind == Type::Count) {
                return mouth.integer(context.ledger, integer);
            }
            return mouth.dimension(context.ledger, integer, length);
        };

        mouth.bind("\\set", [this, &context, value, &mouth](Mouth&) {
            const auto target = slot(mouth, context);
            if (!target) return;

            skip(mouth, "=");
            const auto scanned = value(mouth, target->type);
            if (!scanned) {
                fault(Traceback::Type::Register, mouth.lookahead().location,
                              "\\set needs a value");
                return;
            }

            context.ledger.assign(target->type, target->slot, *scanned, mouth.unglobal() != 0);
        });

        // The three arithmetic primitives differ only in the operation, so
        // they share one body. Arithmetic is done in std::int64_t and clamped,
        // because a register holds exactly 32 bits.
        const auto modify = [this, &context, value](Mouth& mouth, const char operation) {
            const auto target = slot(mouth, context);
            if (!target) return;

            skip(mouth, "by");
            const auto scanned = value(mouth, target->type);
            if (!scanned) {
                fault(Traceback::Type::Register, mouth.lookahead().location,
                              "expected an amount");
                return;
            }

            const auto current = static_cast<std::int64_t>(
                context.ledger.fetch(target->type, target->slot));
            const auto amount = static_cast<std::int64_t>(*scanned);

            std::int64_t result = current;
            switch (operation) {
                case '+': result = current + amount; break;
                case '*': result = current * amount; break;
                case '/':
                    if (amount == 0) {
                        fault(Traceback::Type::Register, mouth.lookahead().location,
                                      "\\reduce by zero");
                        return;
                    }
                    result = current / amount;
                    break;
                default: break;
            }

            constexpr std::int64_t ceiling = 2147483647;
            if (result > ceiling || result < -ceiling) {
                fault(Traceback::Type::Register, mouth.lookahead().location,
                              std::format("register arithmetic overflowed ({})", result));
                result = result > 0 ? ceiling : -ceiling;
            }

            context.ledger.assign(target->type, target->slot,
                                  static_cast<std::int32_t>(result), mouth.unglobal() != 0);
        };

        mouth.bind("\\increase", [modify, &mouth](Mouth&) { modify(mouth, '+'); });
        mouth.bind("\\scale",    [modify, &mouth](Mouth&) { modify(mouth, '*'); });
        mouth.bind("\\reduce",   [modify, &mouth](Mouth&) { modify(mouth, '/'); });

        mouth.bind("\\name", [this, &context, &mouth](Mouth&) {
            const Token alias = mouth.read();
            if (alias.category != CatCodes::Category::Escape) {
                fault(Traceback::Type::Register, alias.location,
                              "\\name needs a control sequence");
                return;
            }

            const auto target = slot(mouth, context);
            if (!target) return;

            context.ledger.bind(alias.symbol, target->type, target->slot);
        });

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound register primitives");
    }

}