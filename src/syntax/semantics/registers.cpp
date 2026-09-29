/// @file
/// @brief Registers implementation: three banks with level-stamped scoping.
#include "syntax/semantics/registers.hpp"

namespace syntax::semantics {

    void Registers::pop() {
        if (marks.empty()) return;
        for (std::size_t index = records.size(); index > marks.back(); --index) {
            const Record& record = records[index - 1];
            const auto bank = static_cast<std::size_t>(record.type);
            if (levels[bank][record.slot] == 0) continue;   // made global since
            levels[bank][record.slot] = record.level;
            values[bank][record.slot] = record.value;
        }
        records.resize(marks.back());
        marks.pop_back();
    }

    void Registers::set(const Type type, const std::size_t slot, const std::int32_t value, const bool global) {
        if (slot >= slots) return;
        const auto bank = static_cast<std::size_t>(type);
        if (global) {
            levels[bank][slot] = 0;
        } else if (const auto depth = static_cast<std::uint32_t>(marks.size()); depth != 0 && levels[bank][slot] != depth) {
            records.push_back(Record{type, static_cast<std::uint8_t>(slot), values[bank][slot], levels[bank][slot]});
            levels[bank][slot] = depth;
        }
        values[bank][slot] = value;
    }

    void Registers::bind(const Symbol symbol, const Type type, const std::optional<std::size_t> slot) {
        if (slot && *slot >= slots) return;
        if (symbol >= names.size()) names.resize(symbol + 1);
        names[symbol] = slot ? Name{type, static_cast<std::uint8_t>(*slot), 2} : Name{type, 0, 1};
    }

    std::optional<Registers::Type> Registers::bank(const Symbol symbol) const noexcept {
        if (symbol >= names.size() || names[symbol].role != 1) return std::nullopt;
        return names[symbol].type;
    }

    std::optional<Registers::Target> Registers::target(const Symbol symbol) const noexcept {
        if (symbol >= names.size() || names[symbol].role != 2) return std::nullopt;
        return Target{names[symbol].type, names[symbol].slot};
    }

}
