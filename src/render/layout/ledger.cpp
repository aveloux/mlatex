/// @file
/// @brief Ledger implementation: an open-addressed cache of shaped runs.
///
/// The table never grows. A cache that reallocated would be fine -- the
/// glyphs it points at live in the arena -- but a fixed table keeps every
/// lookup to one probe sequence and keeps the memory it uses knowable, which
/// matters more here than the last few hits would.
#include "layout/ledger.hpp"

#include <bit>

namespace render::layout {

    bool Ledger::Key::operator==(const Key& other) const noexcept {
        return font == other.font && size == other.size && text == other.text;
    }

    std::size_t Ledger::digest(const Key& key) noexcept {
        // FNV-1a over the text, then the font and size folded in. The font is
        // a pointer into the arena and the size a float, so both are taken as
        // bits rather than as values.
        std::size_t value = 1469598103934665603ull;
        for (const char letter : key.text) {
            value ^= static_cast<std::size_t>(static_cast<unsigned char>(letter));
            value *= 1099511628211ull;
        }
        value ^= reinterpret_cast<std::uintptr_t>(key.font);
        value *= 1099511628211ull;
        value ^= static_cast<std::size_t>(std::bit_cast<std::uint32_t>(key.size)) << 17;
        return value;
    }

    Ledger::Ledger(memory::Arena& arena, const std::size_t size) noexcept {
        // A power of two so a probe masks instead of dividing.
        this->limit = std::bit_ceil(size > 0 ? size : 2048);
        slots = arena.allocate<Entry>(this->limit);
        for (std::size_t index = 0; index < this->limit; ++index) {
            slots[index].occupied = false;
        }
    }

    memory::Slice<Node*> Ledger::find(const Key& key) const noexcept {
        if (entries == 0) return {};

        const std::size_t start = digest(key) & (limit - 1);
        for (std::size_t step = 0; step < limit; ++step) {
            const Entry& slot = slots[(start + step) & (limit - 1)];
            // An empty slot ends the probe: anything with this key would have
            // been placed here or before.
            if (!slot.occupied) return {};
            if (slot.key == key) return slot.nodes;
        }
        return {};
    }

    void Ledger::insert(const Key& key, const memory::Slice<Node*> nodes) noexcept {
        // A full table stops caching rather than evicting. Evicting would mean
        // re-shaping something a caller may still be holding glyphs from.
        // Full is three quarters, not every slot: past that a lookup that
        // misses has to probe further and further to find the empty slot
        // that ends it, and at the last slot it would probe them all.
        if (entries >= limit - limit / 4) return;

        const std::size_t start = digest(key) & (limit - 1);
        for (std::size_t step = 0; step < limit; ++step) {
            Entry& slot = slots[(start + step) & (limit - 1)];

            if (!slot.occupied) {
                slot = Entry{.key = key, .nodes = nodes, .occupied = true};
                ++entries;
                return;
            }
            if (slot.key == key) {
                slot.nodes = nodes;
                return;
            }
        }
    }

    void Ledger::dispose() noexcept {
        if (entries == 0) return;
        for (std::size_t index = 0; index < limit; ++index) {
            slots[index].occupied = false;
        }
        entries = 0;
    }

}
