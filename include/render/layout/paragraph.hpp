#pragma once

#include "layout/ledger.hpp"
#include "layout/node.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "typography/font.hpp"
#include "typography/hyphenator.hpp"
#include "typography/shaper.hpp"

#include <string_view>

namespace render::layout {

    class Paragraph {
    public:
        Paragraph(
            memory::Arena& arena,
            std::string_view text,
            const typography::Font& font,
            float size
        ) noexcept;

        void assign(std::string_view text) noexcept;
        void touch() noexcept;

        [[nodiscard]] bool dirty() const noexcept;
        [[nodiscard]] float height() const noexcept;
        [[nodiscard]] float offset() const noexcept;
        void offset(float value) noexcept;
        [[nodiscard]] Node* node() const noexcept;

        float layout(
            const typography::Shaper& shaper,
            Ledger& ledger,
            memory::Arena& scratch,
            float width,
            float leading,
            const typography::Hyphenator* hyphenator = nullptr
        ) noexcept;

    private:
        memory::Arena& arena;
        std::string_view content{};
        const typography::Font* face{nullptr};
        float scale{0.0f};
        bool stale{true};
        Node* tree{nullptr};
        float tall{0.0f};
        float shift{0.0f};
    };

}