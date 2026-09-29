/// @file
/// @brief Unicodes implementation: math symbol name to code point.
///
/// One gperf-generated table, queried through a stack buffer, so a lookup
/// allocates nothing.
#include "syntax/expression/unicodes.hpp"
#include "lookup.hpp"

#include <cstring>
#include <string>

namespace syntax::expression {

    /// Longest name the stack buffer can hold, NUL included.
    ///
    /// This is a buffer size, not a claim about the table: the heap path
    /// below keeps a longer name correct, it just does not keep it fast.
    /// Every math symbol name is far shorter than this, so that path is
    /// unreachable in practice. gperf's generated matcher finishes with
    /// strcmp unless the table was built with --compare-lengths, so the
    /// candidate has to be NUL-terminated either way.
    static constexpr std::size_t longest = 63;

    const std::uint32_t Unicodes::invalid = 0xFFFD;

    Unicodes::Unicodes() = default;

    std::optional<Unicodes::Symbol> Unicodes::query(const std::string_view name) const noexcept {
        if (name.empty()) return std::nullopt;

        if (name.size() <= longest) {
            char text[longest + 1];
            std::memcpy(text, name.data(), name.size());
            text[name.size()] = '\0';

            if (const auto* entry = Lookup::query(text, static_cast<unsigned int>(name.size()))) {
                return Symbol{entry->codepoint, static_cast<Category>(entry->category)};
            }
            return std::nullopt;
        }

        // Longer than the buffer. Correct, just not allocation-free.
        const std::string heap(name);
        if (const auto* entry = Lookup::query(heap.c_str(), static_cast<unsigned int>(heap.size()))) {
            return Symbol{entry->codepoint, static_cast<Category>(entry->category)};
        }

        return std::nullopt;
    }

}
