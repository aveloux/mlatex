#pragma once

#include <charconv>

/// @brief What a standard library gets wrong on one platform, put right for
///        every platform the same way.
///
/// Apple's libc++ marks the floating-point overloads of std::from_chars
/// unavailable before macOS 26 -- an availability attribute baked into the
/// headers themselves, tied to the deployment target rather than to which
/// libc++ actually compiled them, so a build aimed at an older macOS is
/// refused at the call site rather than at run time. decimal() reads the
/// same grammar through strtof and strtod, which carry no such attribute,
/// behind the same two-pointers-and-a-reference signature and the same
/// std::from_chars_result, so every caller stays exactly as it was. Like
/// std::from_chars, the result may be let go: a caller that set a default
/// first and keeps it when nothing reads is using it as intended.
///
/// The engine never changes the global locale, so strtof and strtod read
/// `.` as the decimal point here the same way std::from_chars always does;
/// a host embedding the engine that calls std::setlocale itself is the one
/// case this would read differently.
namespace patch {

    /// @brief Reads a float the way std::from_chars would.
    /// @param first Start of the text.
    /// @param last  End of the text; nothing at or past it is read.
    /// @param value Receives the value on success; left unchanged otherwise.
    /// @return Where the number ended, and std::errc{} -- or @p first again
    ///         and std::errc::invalid_argument when no number was there at all.
    std::from_chars_result decimal(const char* first, const char* last, float& value) noexcept;

    /// @brief Reads a double the way std::from_chars would.
    /// @param first Start of the text.
    /// @param last  End of the text; nothing at or past it is read.
    /// @param value Receives the value on success; left unchanged otherwise.
    /// @return Where the number ended, and std::errc{} -- or @p first again
    ///         and std::errc::invalid_argument when no number was there at all.
    std::from_chars_result decimal(const char* first, const char* last, double& value) noexcept;

}
