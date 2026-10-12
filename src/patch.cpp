/// @file
/// @brief Patch implementation: std::from_chars for floating point, read
///        through the C library instead.
///
/// strtof and strtod read a zero-terminated string, and the text handed in
/// is a range that need not be one, so it is copied first: a number is a
/// handful of characters, and the copy is what keeps a read from running
/// past @p last into whatever follows it.
#include "patch.hpp"

#include <cstdlib>
#include <string>

namespace patch {
    std::from_chars_result decimal(const char *first, const char *last, float &value) noexcept {
        const std::string held(first, last);
        char *stop = nullptr;
        const float read = std::strtof(held.c_str(), &stop);
        if (stop == held.c_str()) return {.ptr = first, .ec = std::errc::invalid_argument};
        value = read;
        return {.ptr = first + (stop - held.c_str()), .ec = std::errc{}};
    }

    std::from_chars_result decimal(const char *first, const char *last, double &value) noexcept {
        const std::string held(first, last);
        char *stop = nullptr;
        const double read = std::strtod(held.c_str(), &stop);
        if (stop == held.c_str()) return {.ptr = first, .ec = std::errc::invalid_argument};
        value = read;
        return {.ptr = first + (stop - held.c_str()), .ec = std::errc{}};
    }
}
