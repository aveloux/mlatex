#pragma once

#include <charconv>
#include <cstdlib>
#include <string>

/// @brief std::from_chars for float and double, portable everywhere.
///
/// Apple's libc++ marks the floating-point overloads of std::from_chars
/// unavailable before macOS 26 -- an availability attribute baked into the
/// headers themselves, tied to the deployment target rather than to which
/// libc++ actually compiled them, so a build aimed at an older macOS is
/// refused at the call site rather than at run time. These read the same
/// grammar through strtof and strtod, which carry no such attribute, behind
/// the same two-pointers-and-a-reference signature and the same
/// std::from_chars_result, so every caller stays exactly as it was.
///
/// The engine never changes the global locale, so strtof and strtod read
/// `.` as the decimal point here the same way std::from_chars always does;
/// a host embedding the engine that calls std::setlocale itself is the one
/// case this would read differently.
namespace chars {

    /// @brief Reads a float the way std::from_chars would.
    /// @param first Start of the text.
    /// @param last  End of the text.
    /// @param value Receives the value on success; left unchanged otherwise.
    /// @return Where the number ended, and std::errc{} -- or #first again and
    ///         std::errc::invalid_argument when no number was there at all.
    [[nodiscard]] inline std::from_chars_result decimal(
        const char* first, const char* last, float& value) noexcept {
        const std::string held(first, last);
        char* stop = nullptr;
        const float read = std::strtof(held.c_str(), &stop);
        if (stop == held.c_str()) return {first, std::errc::invalid_argument};
        value = read;
        return {first + (stop - held.c_str()), std::errc{}};
    }

    /// @brief Reads a double the way std::from_chars would.
    /// @param first Start of the text.
    /// @param last  End of the text.
    /// @param value Receives the value on success; left unchanged otherwise.
    /// @return Where the number ended, and std::errc{} -- or #first again and
    ///         std::errc::invalid_argument when no number was there at all.
    [[nodiscard]] inline std::from_chars_result decimal(
        const char* first, const char* last, double& value) noexcept {
        const std::string held(first, last);
        char* stop = nullptr;
        const double read = std::strtod(held.c_str(), &stop);
        if (stop == held.c_str()) return {first, std::errc::invalid_argument};
        value = read;
        return {first + (stop - held.c_str()), std::errc{}};
    }

}
