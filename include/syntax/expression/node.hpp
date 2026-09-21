#pragma once

#include "syntax/expression/unicodes.hpp"
#include "memory/slice.hpp"

#include <cstdint>
#include <string_view>

namespace syntax::expression {

    struct Node {
        enum class Style : std::uint8_t {
            Inline, ///< In-text embedded math mode (\\(... \\), $...$)
            Display ///< Standalone block-level equation (\\[... \\], $$...$$)
        };

        enum class Type : std::uint8_t {
            Variable, ///< Identifiers, numbers, and basic symbols (e.g., x, 2)
            Binary,   ///< Infix operators with left/right operands (e.g., +, -)
            Unary,    ///< Single-operand prefix or postfix operators (e.g., -x, n!)
            Group,    ///< Delimited sub-expressions (e.g., {...}, (...))
            Script,   ///< Base node with attached sub/superscripts (e.g., x_i^2)
            Fraction, ///< Numerator and denominator pairs (e.g., \\frac{a}{b})
            Radical,  ///< Square or n-th root expressions (e.g., \\sqrt[n]{x})
            Accent,   ///< Diacritics placed over symbols (e.g., \\hat{x}, \\vec{v})
            Sequence  ///< Horizontal list of sequential nodes (Math List)
        };

        Type type = Type::Variable;
        Unicodes::Category category = Unicodes::Category::Ordinary;
        Style style = Style::Inline;
        std::uint32_t codepoint = 0;
        std::string_view value{};

        char open = 0;    ///< 0 for a brace group, which groups without printing
        char close = 0;

        Node* left = nullptr;
        Node* right = nullptr;
        Node* subscript = nullptr;
        Node* superscript = nullptr;
        memory::Slice<Node*> arguments{};
    };

}