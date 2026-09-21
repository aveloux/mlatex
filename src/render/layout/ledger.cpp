#include "layout/ledger.hpp"

#include <functional>

namespace render::layout {

    bool Ledger::Key::operator==(const Key& target) const noexcept {
        return font == target.font && text == target.text && size == target.size;
    }

    Ledger::Ledger(memory::Arena& arena, const std::size_t limit) noexcept
        : arena(arena), limit(limit) {
        slots = arena.allocate<Entry>(limit);
        for (std::size_t step = 0; step < limit; ++step) {
            slots[step].occupied = false;
        }
    }

    memory::Slice<Node*> Ledger::find(const Key& key) const noexcept {
        if (limit == 0 || count == 0) return {};

        const std::size_t slot = hash(key) % limit;
        for (std::size_t step = 0; step < limit; ++step) {
            const auto&[key_, nodes, occupied] = slots[(slot + step) % limit];
            if (!occupied) return {};
            if (key_ == key) return nodes;
        }
        return {};
    }

    void Ledger::insert(const Key& key, const memory::Slice<Node*> nodes) noexcept {
        if (limit == 0) return;

        const std::size_t slot = hash(key) % limit;
        for (std::size_t step = 0; step < limit; ++step) {
            auto&[key_, nodes_, occupied] = slots[(slot + step) % limit];
            if (!occupied) {
                key_ = key;
                nodes_ = nodes;
                occupied = true;
                ++count;
                return;
            }
            if (key_ == key) {
                nodes_ = nodes;
                return;
            }
        }

        slots[slot] = Entry{ .key = key, .nodes = nodes, .occupied = true };
    }

    void Ledger::dispose() noexcept {
        for (std::size_t step = 0; step < limit; ++step) {
            slots[step].occupied = false;
        }
        count = 0;
    }

    std::size_t Ledger::hash(const Key& key) noexcept {
        const std::size_t head = std::hash<const void*>{}(key.font);
        const std::size_t body = std::hash<std::string_view>{}(key.text);
        const std::size_t tail = std::hash<float>{}(key.size);
        return head ^ body << 1 ^ tail << 2;
    }

}