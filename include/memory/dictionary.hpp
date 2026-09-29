#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace memory {

    /// @brief A string hash that hashes a view the same way, so a table keyed
    ///        by strings is searched with a view and never builds a string.
    ///
    /// Every name a document asks for -- a label, a counter, a variable, a
    /// package -- arrives as a view into the source or the lexicon. Hashing
    /// it where it lies is what keeps each of those lookups one hash and one
    /// comparison, with nothing allocated to make them.
    struct Hash {
        using is_transparent = void;   ///< Views are searched for as they are.

        /// @brief Hashes a name.
        /// @param text The name, as a string or a view of one.
        /// @return Its hash.
        [[nodiscard]] std::size_t operator()(const std::string_view text) const noexcept {
            return std::hash<std::string_view>{}(text);
        }
    };

    /// @brief Values by name, found in O(1) expected with a view of the name.
    /// @tparam Value What each name stands for.
    template <typename Value>
    using Dictionary = std::unordered_map<std::string, Value, Hash, std::equal_to<>>;

    /// @brief A set of names, searched the same way.
    using Names = std::unordered_set<std::string, Hash, std::equal_to<>>;

}
