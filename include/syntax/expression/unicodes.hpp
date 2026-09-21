#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace syntax::expression {

    class Unicodes {
    public:
        enum class Category : std::uint8_t {
            Ordinary,    ///< Standard math symbols, variables, and digits (e.g., x, 1)
            Operator,    ///< Prefix operators and functions (e.g., \\sum, \\sin)
            Binary,      ///< Binary operations (e.g., +, \\times)
            Relation,    ///< Comparison operators (e.g., =, \\le)
            Opening,     ///< Left delimiters (e.g., (, [)
            Closing,     ///< Right delimiters (e.g., ), ])
            Punctuation, ///< Punctuation marks (e.g., ,, ;)
            Inner,       ///< Enclosed structures like fractions
            Accent       ///< Math diacritics (e.g., \\hat, \\vec)
        };

        struct Symbol {
            std::uint32_t codepoint;
            Category category;
        };

        static const std::uint32_t invalid;

        Unicodes();

        void compose(std::string_view name, std::uint32_t codepoint, Category category);
        void dispose(std::string_view name);
        [[nodiscard]] std::optional<Symbol> query(std::string_view name) const noexcept;

    private:
        struct Hash {
            using is_transparent = void;
            std::size_t operator()(const std::string_view value) const noexcept {
                return std::hash<std::string_view>{}(value);
            }
        };

        std::unordered_map<std::string, Symbol, Hash, std::equal_to<>> overrides;
    };

}