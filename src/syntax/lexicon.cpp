/// @file
/// @brief Lexicon implementation: a direct table for single bytes and an
///        open-addressed FNV-1a table for everything longer.
#include "syntax/lexicon.hpp"

#include <utility>

namespace syntax {

    /// @brief FNV-1a hash of a name.
    static constexpr std::size_t digest(const std::string_view name) noexcept {
        std::size_t value = 14695981039346656037ull;
        for (const char letter : name) {
            value ^= static_cast<unsigned char>(letter);
            value *= 1099511628211ull;
        }
        return value;
    }

    Lexicon::Lexicon(memory::Arena& arena) : arena(arena), names{std::string_view{}}, table(1024, none) {}

    Symbol Lexicon::intern(const std::string_view name) {
        if (name.empty()) return none;

        if (name.size() == 1) {
            Symbol& single = singles[static_cast<unsigned char>(name[0])];
            if (single == none) {
                single = static_cast<Symbol>(names.size());
                names.push_back(arena.copy(name));
            }
            return single;
        }

        std::size_t mask = table.size() - 1;
        for (std::size_t slot = digest(name) & mask;; slot = (slot + 1) & mask) {
            if (table[slot] != none) {
                if (names[table[slot]] == name) return table[slot];
                continue;
            }

            const auto symbol = static_cast<Symbol>(names.size());
            names.push_back(arena.copy(name));
            table[slot] = symbol;

            // Kept at most half full, so a probe sequence stays short.
            if (names.size() * 2 > table.size()) {
                std::vector<Symbol> larger(table.size() * 2, none);
                mask = larger.size() - 1;
                for (const Symbol existing : table) {
                    if (existing == none) continue;
                    std::size_t target = digest(names[existing]) & mask;
                    while (larger[target] != none) target = (target + 1) & mask;
                    larger[target] = existing;
                }
                table = std::move(larger);
            }
            return symbol;
        }
    }

}
