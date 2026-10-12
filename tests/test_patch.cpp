#include "patch.hpp"

#include <cassert>
#include <cmath>
#include <string_view>
#include <system_error>

// What std::from_chars does for a float or a double, done the way every
// platform can: a whole number, a number with text after it, a sign, a
// failure that leaves the value alone, and a range that ends before the
// text it sits in does.

int main() {
    {
        constexpr std::string_view text = "2.5";
        float value = 0.0f;
        const auto [stop, failure] = patch::decimal(text.data(), text.data() + text.size(), value);
        assert((failure == std::errc{} && stop == text.data() + text.size() && value == 2.5f) &&
               "a whole number, read to its end");
    }
    {
        constexpr std::string_view text = "12pt";
        float value = 0.0f;
        const auto [stop, failure] = patch::decimal(text.data(), text.data() + text.size(), value);
        assert((failure == std::errc{} && stop == text.data() + 2 && value == 12.0f) &&
               "a number with a unit after it stops where the unit starts");
    }
    {
        constexpr std::string_view text = "-0.75";
        double value = 0.0;
        const auto [stop, failure] = patch::decimal(text.data(), text.data() + text.size(), value);
        assert((failure == std::errc{} && stop == text.data() + text.size() && value == -0.75) &&
               "a negative double");
    }
    {
        constexpr std::string_view text = "pt";
        float value = 9.0f;
        const auto [stop, failure] = patch::decimal(text.data(), text.data() + text.size(), value);
        assert((failure == std::errc::invalid_argument && stop == text.data() && value == 9.0f) &&
               "no number: an error, the pointer where it started, the value as it was");
    }
    {
        constexpr std::string_view text = "";
        double value = 4.0;
        const auto [stop, failure] = patch::decimal(text.data(), text.data() + text.size(), value);
        assert((failure == std::errc::invalid_argument && stop == text.data() && value == 4.0) &&
               "nothing at all is no number");
    }
    {
        // Twelve, from the first two digits of 123: a range need not end
        // where its text does, and nothing past it is read.
        constexpr std::string_view text = "123";
        float value = 0.0f;
        const auto [stop, failure] = patch::decimal(text.data(), text.data() + 2, value);
        assert((failure == std::errc{} && stop == text.data() + 2 && value == 12.0f) &&
               "a range is read to its own end, not the text's");
    }
    {
        constexpr std::string_view text = "0.1";
        double value = 0.0;
        patch::decimal(text.data(), text.data() + text.size(), value);
        assert((std::abs(value - 0.1) < 1e-15) && "a double kept to a double's precision");
    }
    return 0;
}
