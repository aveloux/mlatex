#pragma once

#include "layout/ledger.hpp"
#include "layout/paragraph.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "syntax/expression/node.hpp"
#include "typography/font.hpp"
#include "typography/hyphenator.hpp"
#include "typography/shaper.hpp"

#include <cstdint>
#include <string_view>

namespace render::layout {

    class Document {
    public:
        struct Configuration {
            float width{612.0f};
            float height{792.0f};
            float left{72.0f};
            float right{72.0f};
            float top{72.0f};
            float bottom{72.0f};
            float leading{14.0f};
        };

        struct Element {
            enum class Type : std::uint8_t {
                Paragraph,
                Expression,
                Directive
            };

            Type type{Type::Paragraph};
            Paragraph* paragraph{nullptr};
            const syntax::expression::Node* expression{nullptr};
            const typography::Font* font{nullptr};
            Node* node{nullptr};
            Element* next{nullptr};
        };

        Document(
            memory::Arena& arena,
            memory::Arena& scratch,
            typography::Shaper& shaper
        ) noexcept;

        Document(
            memory::Arena& arena,
            memory::Arena& scratch,
            typography::Shaper& shaper,
            const Configuration& configuration_
        ) noexcept;

        Paragraph* append(
            std::string_view text,
            const typography::Font& font,
            float size
        ) noexcept;

        void append(
            const syntax::expression::Node* expression,
            const typography::Font& font
        ) noexcept;

        void append(Node* node) noexcept;

        void hyphenate(const typography::Hyphenator& value) noexcept;

        void layout() noexcept;

        [[nodiscard]] memory::Slice<Element*> elements() const noexcept;
        [[nodiscard]] memory::Slice<Paragraph*> paragraphs() const noexcept;
        [[nodiscard]] const Configuration& configuration() const noexcept;
        [[nodiscard]] Configuration& configuration() noexcept;

    private:
        memory::Arena& arena;
        memory::Arena& scratch;
        typography::Shaper& shaper;
        Ledger ledger;
        Configuration configuration_{};
        const typography::Hyphenator* hyphenator{nullptr};

        Element* head{nullptr};
        Element* tail{nullptr};
        std::size_t count{0};
    };

}