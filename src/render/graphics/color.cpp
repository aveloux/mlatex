/// @file
/// @brief Color implementation: every format this engine reads into the
///        0-1 form PDF wants.
#include "render/graphics/color.hpp"

#include <array>
#include <charconv>
#include <cstdint>

namespace render::graphics {

    /// @brief Reads one byte as two hex digits.
    /// @param text   Two-character slice to decode.
    /// @param value  Receives the byte on success.
    /// @return True when both characters were valid hex digits.
    [[nodiscard]] static bool byte(const std::string_view text, std::uint8_t& value) noexcept {
        unsigned parsed = 0;
        const auto [stop, failure] =
            std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
        if (failure != std::errc{} || stop != text.data() + text.size()) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    }

    /// @brief Trims spaces from both ends.
    /// @param text Text to trim.
    [[nodiscard]] static std::string_view trim(std::string_view text) noexcept {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        return text;
    }

    /// @brief A color by the name xcolor gives it.
    struct Named {
        std::string_view name;
        Color color;
    };

    static constexpr std::array<Named, 20> table{{
        {"black", black}, {"white", white}, {"red", red}, {"green", green},
        {"blue", blue}, {"cyan", cyan}, {"magenta", magenta}, {"yellow", yellow},
        {"gray", gray}, {"grey", gray}, {"darkgray", darkgray}, {"lightgray", lightgray},
        {"brown", brown}, {"lime", lime}, {"olive", olive}, {"orange", orange},
        {"pink", pink}, {"purple", purple}, {"teal", teal}, {"violet", violet},
    }};

    Color Color::hex(std::string_view text) noexcept {
        if (text.starts_with('#')) text.remove_prefix(1);
        if (text.size() != 6 && text.size() != 8) return black;

        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;
        if (!byte(text.substr(0, 2), r) || !byte(text.substr(2, 2), g) || !byte(text.substr(4, 2), b) ||
            (text.size() == 8 && !byte(text.substr(6, 2), a))) {
            return black;
        }

        return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f};
    }

    Color Color::parse(std::string_view text) noexcept {
        text = trim(text);

        const bool alpha = text.starts_with("rgba(");
        if (alpha || text.starts_with("rgb(")) {
            if (!text.ends_with(')')) return black;
            text.remove_prefix(alpha ? 5 : 4);
            text.remove_suffix(1);

            // `a,b,c` between the parentheses, each part trimmed; three or
            // four of them, and each wanted one a plain decimal number.
            std::array<std::string_view, 4> parts{};
            std::size_t count = 0;
            while (count < parts.size()) {
                const std::size_t comma = text.find(',');
                parts[count++] = trim(comma == std::string_view::npos ? text : text.substr(0, comma));
                if (comma == std::string_view::npos) break;
                text.remove_prefix(comma + 1);
            }
            if (count != 3 && count != 4) return black;

            std::array<float, 4> channel{0.0f, 0.0f, 0.0f, 1.0f};
            const std::size_t wanted = alpha ? 4 : 3;
            for (std::size_t index = 0; index < wanted; ++index) {
                const std::string_view part = parts[index];
                if (part.empty()) return black;
                const auto [stop, failure] = std::from_chars(part.data(), part.data() + part.size(), channel[index]);
                if (failure != std::errc{} || stop != part.data() + part.size()) return black;
            }

            return {channel[0] / 255.0f, channel[1] / 255.0f, channel[2] / 255.0f, channel[3]};
        }

        for (const auto& [name, color] : table) {
            if (text == name) return color;
        }

        return hex(text);
    }

}
