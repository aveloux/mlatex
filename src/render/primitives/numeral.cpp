/// @file
/// @brief Counter formatting: `\\arabic`, `\\roman`/`\\Roman`, `\\alph`/`\\Alph`.
#include "render/primitives/numeral.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <utility>

namespace render::primitives {

    std::string Numeral::arabic(const int value) {
        return std::to_string(value);
    }

    std::string Numeral::roman(int value, const bool upper) {
        if (value <= 0) return {};

        static constexpr std::array<std::pair<int, std::string_view>, 13> table{{
            {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
            {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"},
        }};

        std::string result;
        for (const auto& [amount, numeral] : table) {
            while (value >= amount) {
                result += numeral;
                value -= amount;
            }
        }

        if (upper) {
            std::ranges::transform(result, result.begin(),
                                    [](const char letter) { return static_cast<char>(std::toupper(letter)); });
        }
        return result;
    }

    std::string Numeral::alphabetic(const int value, const bool upper) {
        // Nothing for nothing, as LaTeX's \alph; past the alphabet, its last
        // letter, where LaTeX stops with "Counter too large".
        if (value <= 0) return {};
        const int clamped = std::min(value, 26);
        const char base = upper ? 'A' : 'a';
        return std::string(1, static_cast<char>(base + clamped - 1));
    }

}
