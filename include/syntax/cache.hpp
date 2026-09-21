#pragma once

#include "syntax/lexicon.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace syntax {

    /// @brief Per-thread single-character interning cache.
    ///
    /// Every ASCII byte of source would otherwise go through a hash lookup in
    /// Lexicon::intern. This collapses the common case to an array index.
    struct Cache {
        const Lexicon* lexicon = nullptr;
        std::uint64_t generation = 0;   ///< guards against a reused address
        std::array<std::pair<Symbol, std::string_view>, 256> entries{};

        Cache() {
            entries.fill({kInvalidSymbol, {}});
        }
    };

    /// The cache itself. Thread-local, because Lexicon is not synchronised.
    inline thread_local Cache cache;

    /// @brief Interns a slice, going through the cache for single characters.
    /// @param lexicon Table to intern into.
    /// @param slice   Text to intern.
    /// @return Its symbol and the interned text.
    /// @complexity O(1) for a single character after first sight; otherwise
    ///             the cost of Lexicon::intern.
    inline std::pair<Symbol, std::string_view> entry(Lexicon& lexicon, const std::string_view slice) {
        if (slice.size() != 1) {
            const Symbol bound = lexicon.intern(slice);
            return {bound, lexicon.resolve(bound)};
        }

        const auto index = static_cast<unsigned char>(slice[0]);

        if (cache.lexicon != &lexicon || cache.generation != lexicon.id()) {
            cache.lexicon = &lexicon;
            cache.generation = lexicon.id();
            cache.entries.fill({kInvalidSymbol, {}});
        }

        if (cache.entries[index].first == kInvalidSymbol) {
            const Symbol bound = lexicon.intern(slice);
            cache.entries[index] = {bound, lexicon.resolve(bound)};
        }

        return cache.entries[index];
    }

}