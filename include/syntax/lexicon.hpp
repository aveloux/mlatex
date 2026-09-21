#pragma once

#include "memory/arena.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace syntax {

    /// Interned name. Comparing two Symbols is how the engine compares
    /// control sequences; the text behind them is only for diagnostics.
    using Symbol = std::uint32_t;

    /// The symbol no name interns to. Doubles as the end-of-stream marker.
    inline constexpr Symbol kInvalidSymbol = 0;

    /// @brief Interning table for control sequence and character names.
    ///
    /// Every distinct name is stored once, in the arena, and addressed by a
    /// dense Symbol. Text handed out as std::string_view points into that
    /// arena, so the arena must outlive the Lexicon and must never relocate.
    ///
    /// @warning Non-copyable: it holds an arena reference, and copying it
    ///          would silently produce a second table whose symbols do not
    ///          agree with the first's.
    class Lexicon {
    public:
        struct Hash {
            using is_transparent = void;
            std::size_t operator()(const std::string_view sv) const noexcept {
                return std::hash<std::string_view>{}(sv);
            }
        };

        /// @brief Builds an empty table.
        /// @param storage Allocator for name storage; must outlive this object.
        explicit Lexicon(memory::Arena& storage) : arena(storage) {
            names.emplace_back("");
        }

        Lexicon(const Lexicon&) = delete;
        Lexicon& operator=(const Lexicon&) = delete;

        /// @brief Interns a name.
        /// @param name Text to intern.
        /// @return Its symbol, or kInvalidSymbol for empty text. Interning the
        ///         same text twice returns the same symbol.
        /// @complexity O(1) average; copies the text on first sight only.
        Symbol intern(const std::string_view name) {
            if (name.empty()) return kInvalidSymbol;

            if (const auto found = lookup.find(name); found != lookup.end()) {
                return found->second;
            }

            const std::string_view copy = arena.copy(name);
            const auto symbol = static_cast<Symbol>(names.size());
            lookup.emplace(copy, symbol);
            names.push_back(copy);
            return symbol;
        }

        /// @brief Recovers the text behind a symbol.
        /// @param symbol Symbol to resolve.
        /// @return Its text, valid as long as the arena lives, or empty for an
        ///         unknown symbol.
        /// @complexity O(1).
        [[nodiscard]] std::string_view resolve(const Symbol symbol) const noexcept {
            if (symbol < names.size()) {
                return names[symbol];
            }
            return {};
        }

        /// @brief How many names are interned, counting kInvalidSymbol.
        [[nodiscard]] std::size_t size() const noexcept { return names.size(); }

        /// @brief Identity that stays unique for the life of the process.
        ///
        /// Caches keyed on a Lexicon must compare this as well as the address:
        /// destroying one table and constructing another at the same address
        /// is routine, and an address-only key would go on serving symbols
        /// from the dead table and string_views into its freed arena.
        ///
        /// @complexity O(1).
        [[nodiscard]] std::uint64_t id() const noexcept { return mark; }

    private:
        [[nodiscard]] static std::uint64_t next() noexcept {
            static std::atomic<std::uint64_t> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        memory::Arena& arena;
        std::unordered_map<std::string_view, Symbol, Hash, std::equal_to<>> lookup{};
        std::vector<std::string_view> names{};
        std::uint64_t mark = next();   ///< unique for the life of the process
    };

}