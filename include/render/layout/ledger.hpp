#pragma once

#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "typography/font.hpp"

#include <string_view>

namespace render::layout {

    class Ledger {
    public:
        struct Key {
            const typography::Font* font{nullptr};
            std::string_view text{};
            float size{0.0f};

            [[nodiscard]] bool operator==(const Key& target) const noexcept;
        };

        struct Entry {
            Key key{};
            memory::Slice<Node*> nodes{};
            bool occupied{false};
        };

        explicit Ledger(memory::Arena& arena, std::size_t limit = 2048) noexcept;

        [[nodiscard]] memory::Slice<Node*> find(const Key& key) const noexcept;
        void insert(const Key& key, memory::Slice<Node*> nodes) noexcept;
        void dispose() noexcept;

    private:
        [[nodiscard]] static std::size_t hash(const Key& key) noexcept;

        memory::Arena& arena;
        std::size_t limit{0};
        std::size_t count{0};
        memory::Slice<Entry> slots{};
    };

}