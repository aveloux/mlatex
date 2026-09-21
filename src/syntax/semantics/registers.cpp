/// @file
/// @brief Registers implementation: four banks with level-stamped scoping.
///
/// Read the Registers class documentation for the level-stamp scheme. Both
/// assign() overloads and pop() implement it; the rest is bounds checking.
#include "syntax/semantics/registers.hpp"
#include "logger.hpp"

#include <utility>

namespace syntax::semantics {

    void Registers::push() {
        marks.push_back(Mark{entries.size(), records.size()});
        Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                    "Pushed register scope checkpoint at entries index {} tokens index {}",
                    entries.size(), records.size());
    }

    void Registers::pop() {
        if (marks.empty()) {
            Logger::log(Logger::Type::Semantics, Logger::Level::Warning,
                        "Attempted to pop register checkpoint on empty stack");
            return;
        }
        const auto mark = marks.back();
        marks.pop_back();

        using enum Type;

        // Both logs are walked newest-first and then truncated in one resize,
        // rather than popped one record at a time.
        for (std::size_t index = entries.size(); index > mark.entries; --index) {
            const auto [type, slot, value, level] = entries[index - 1];

            const auto plane = static_cast<std::size_t>(type);
            if (levels[plane][slot] == 0) continue;   // globalized since; the record is dead
            levels[plane][slot] = level;

            switch (type) {
                case Count:     counts[slot] = value; break;
                case Dimension: dimensions[slot] = value; break;
                case Glue:      glues[slot] = value; break;
                case Tokens:    break; // token registers are restored from records below
            }
        }
        entries.resize(mark.entries);

        constexpr auto bank = static_cast<std::size_t>(Tokens);
        for (std::size_t index = records.size(); index > mark.tokens; --index) {
            auto& [slot, value, level] = records[index - 1];

            if (levels[bank][slot] == 0) continue;
            levels[bank][slot] = level;
            tokens[slot] = std::move(value);
        }
        records.resize(mark.tokens);

        Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                    "Restored register state back to checkpoint entries {} tokens {}",
                    mark.entries, mark.tokens);
    }

    void Registers::assign(const Type type, const std::size_t index, const std::int32_t value, const bool global) {
        if (index >= slots) {
            Logger::fmt(Logger::Type::Semantics, Logger::Level::Error,
                        "Register index {} out of bounds (maximum {})", index, slots - 1);
            return;
        }

        if (type == Type::Tokens) {
            Logger::log(Logger::Type::Semantics, Logger::Level::Warning,
                        "Token registers cannot take a scalar value; use the vector-typed assign overload");
            return;
        }

        const auto bank = static_cast<std::size_t>(type);

        if (global) {
            levels[bank][index] = 0;
        } else if (const auto depth = static_cast<std::uint32_t>(marks.size());
                   depth != 0 && levels[bank][index] != depth) {
            entries.push_back(Entry{type, index, fetch(type, index), levels[bank][index]});
            levels[bank][index] = depth;
        }

        using enum Type;
        switch (type) {
            case Count:     counts[index] = value; break;
            case Dimension: dimensions[index] = value; break;
            case Glue:      glues[index] = value; break;
            case Tokens:    break; // unreachable, guarded above
        }

        Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                    "Assigned register slot {} to value {} (global: {})", index, value, global);
    }

    void Registers::assign(const std::size_t index, std::vector<Token> value, const bool global) {
        if (index >= slots) {
            Logger::fmt(Logger::Type::Semantics, Logger::Level::Error,
                        "Token register index {} out of bounds (maximum {})", index, slots - 1);
            return;
        }

        constexpr auto bank = static_cast<std::size_t>(Type::Tokens);

        if (global) {
            levels[bank][index] = 0;
        } else if (const auto depth = static_cast<std::uint32_t>(marks.size());
                   depth != 0 && levels[bank][index] != depth) {
            records.push_back(Record{index, tokens[index], levels[bank][index]});
            levels[bank][index] = depth;
        }

        tokens[index] = std::move(value);

        Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                    "Assigned token register slot {} ({} token(s), global: {})",
                    index, tokens[index].size(), global);
    }

    std::int32_t Registers::fetch(const Type type, const std::size_t index) const noexcept {
        if (index >= slots) {
            Logger::fmt(Logger::Type::Semantics, Logger::Level::Error,
                        "Fetch operation requested out-of-bounds register slot {}", index);
            return 0;
        }

        using enum Type;
        switch (type) {
            case Count:     return counts[index];
            case Dimension: return dimensions[index];
            case Glue:      return glues[index];
            case Tokens:
                Logger::log(Logger::Type::Semantics, Logger::Level::Warning,
                            "Token registers have no scalar value; use list()");
                return 0;
        }
        return 0;
    }

    const std::vector<Token>& Registers::list(const std::size_t index) const noexcept {
        static const std::vector<Token> empty{};
        if (index >= slots) {
            Logger::fmt(Logger::Type::Semantics, Logger::Level::Error,
                        "Fetch operation requested out-of-bounds token register slot {}", index);
            return empty;
        }
        return tokens[index];
    }

    void Registers::bind(const Symbol symbol, const Type type, const std::size_t index) {
        if (index >= slots) {
            Logger::fmt(Logger::Type::Semantics, Logger::Level::Error,
                        "Cannot bind symbol {} to out-of-bounds slot {}", symbol, index);
            return;
        }
        aliases[symbol] = Target{type, index};
        Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                    "Bound symbol alias {} to register slot {}", symbol, index);
    }

    void Registers::set(const Symbol symbol, const std::int32_t value, const bool global) {
        const auto match = aliases.find(symbol);
        if (match == aliases.end()) {
            Logger::fmt(Logger::Type::Semantics, Logger::Level::Warning,
                        "Attempted set operation on unbound symbol alias {}", symbol);
            return;
        }
        assign(match->second.type, match->second.slot, value, global);
    }

    std::int32_t Registers::get(const Symbol symbol) const noexcept {
        const auto match = aliases.find(symbol);
        if (match == aliases.end()) {
            Logger::fmt(Logger::Type::Semantics, Logger::Level::Debug,
                        "Symbol {} not bound to any register target", symbol);
            return 0;
        }
        return fetch(match->second.type, match->second.slot);
    }

    bool Registers::bound(const Symbol symbol) const noexcept {
        return aliases.contains(symbol);
    }

    std::optional<Registers::Target> Registers::target(const Symbol symbol) const noexcept {
        const auto match = aliases.find(symbol);
        if (match == aliases.end()) return std::nullopt;
        return match->second;
    }

}