#include "memory/arena.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>

// The arena hands out memory by moving a pointer, grows by whole blocks, and
// gives it all back at once. Everything it hands out has to be aligned for
// its type and stay where it was put, however much is asked for afterwards.

/// @brief Something with an alignment of its own and a constructor to run.
struct alignas(16) Wide {
    std::int64_t value;
    std::int64_t spare{0};
    explicit Wide(const std::int64_t start) : value(start) {}
};

int main() {
    memory::Arena arena(1024);

    // Copies.
    constexpr std::string_view text = "\\documentclass{article}\n\\begin{document}\nHello\n\\end{document}";
    const std::string_view copy = arena.copy(text);
    assert((copy == text) && "a copy holds the same text");
    assert((copy.data() != text.data()) && "in memory of its own");
    assert((arena.copy("").empty()) && "an empty copy is empty");

    // Slices.
    const auto numbers = arena.allocate<int>(4);
    assert((numbers.size() == 4) && "a slice has the length asked for");
    for (std::size_t index = 0; index < numbers.size(); ++index) numbers[index] = static_cast<int>(index * 10);
    assert((numbers[3] == 30) && "and holds what is written to it");
    assert((arena.allocate<int>(0).empty()) && "nothing asked for is nothing");

    // Alignment and construction.
    Wide* wide = arena.compose<Wide>(7);
    assert((wide->value == 7) && "compose runs the constructor");
    assert((reinterpret_cast<std::uintptr_t>(wide) % alignof(Wide) == 0) && "and aligns for the type");

    // Growth: far past the first block, and nothing earlier moves.
    const std::string_view first = arena.copy("stays put");
    for (int round = 0; round < 1000; ++round) {
        const auto block = arena.allocate<std::uint64_t>(64);
        block[63] = static_cast<std::uint64_t>(round);
    }
    assert((first == "stays put") && "memory handed out stays where it was");

    // One request larger than any block so far.
    const auto large = arena.allocate<std::byte>(1u << 20);
    assert((large.size() == (1u << 20)) && "a request larger than a block is met");
    large[large.size() - 1] = std::byte{1};

    return 0;
}
