/// @file
/// @brief Counter formatting: `\\arabic`, `\\roman`/`\\Roman`, `\\alph`/`\\Alph`,
///        and numbers in words.
#include "render/primitives/numeral.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
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

    std::string Numeral::words(const int value, const bool ordinal) {
        static constexpr std::array<std::string_view, 20> ones{
            "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
            "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen",
        };
        static constexpr std::array<std::string_view, 10> tens{
            "", "ten", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety",
        };
        static constexpr std::array<std::pair<std::int64_t, std::string_view>, 3> scales{{
            {1'000'000'000, "billion"}, {1'000'000, "million"}, {1'000, "thousand"},
        }};

        // The cardinal, a group of three digits at a time.
        std::string written = [](this const auto& self, const std::int64_t number) -> std::string {
            if (number < 0) return "minus " + self(-number);
            if (number < 20) return std::string(ones[static_cast<std::size_t>(number)]);
            if (number < 100) {
                std::string text(tens[static_cast<std::size_t>(number / 10)]);
                if (number % 10 != 0) text += "-" + std::string(ones[static_cast<std::size_t>(number % 10)]);
                return text;
            }
            if (number < 1000) {
                std::string text = std::string(ones[static_cast<std::size_t>(number / 100)]) + " hundred";
                if (number % 100 != 0) text += " and " + self(number % 100);
                return text;
            }
            for (const auto& [size, name] : scales) {
                if (number < size) continue;
                std::string text = self(number / size) + " " + std::string(name);
                const std::int64_t rest = number % size;
                if (rest != 0) text += (rest < 100 ? " and " : " ") + self(rest);
                return text;
            }
            return {};
        }(value);
        if (!ordinal) return written;

        // The ordinal: the last word's own, `-th` for the rest.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> irregular{{
            {"one", "first"}, {"two", "second"}, {"three", "third"}, {"five", "fifth"},
            {"eight", "eighth"}, {"nine", "ninth"}, {"twelve", "twelfth"},
        }};
        for (const auto& [cardinal, spelled] : irregular) {
            if (written.ends_with(cardinal)) {
                written.replace(written.size() - cardinal.size(), cardinal.size(), spelled);
                return written;
            }
        }
        if (written.ends_with('y')) {
            written.replace(written.size() - 1, 1, "ieth");
            return written;
        }
        return written + "th";
    }

}
