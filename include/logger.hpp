#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

/// @brief The engine's own reports: what each subsystem is doing, as it
///        does it.
///
/// Everything is static, because there is one console and one log file per
/// process however many documents it makes. Reports are filtered twice: by
/// subsystem, so a run can follow the expander alone, and by level, so a
/// release build drops everything below #floor when it is compiled.
class Logger {
public:
    enum class Type : std::uint32_t {
        None      = 0,
        Lexer     = 1u << 0,
        Mouth     = 1u << 1,
        Parser    = 1u << 2,
        Layout    = 1u << 3,
        Memory    = 1u << 4,
        Semantics = 1u << 5,
        All       = 0x3Fu
    };

    enum class Level : std::uint8_t {
        Traceback   = 0,
        Debug       = 1,
        Informative = 2,
        Warning     = 3,
        Error       = 4,
        Silent      = 5
    };

    /// @brief The least severe level this build keeps at all.
    ///
    /// A release build strips the engine's tracing: a call below this level
    /// returns before anything is formatted, and since the level is a
    /// constant at every call site, an optimiser that inlines log() drops the
    /// call outright. `--debug` and `--trace` therefore report only from a
    /// debug build, which is the only build anyone reads them in. A release
    /// build is told by NDEBUG, or by LATEX_RELEASE, which the build gives
    /// everything linked against a release engine -- the tests among them,
    /// which undefine NDEBUG for their asserts -- so every part of one
    /// program keeps the same floor.
#if defined(NDEBUG) || defined(LATEX_RELEASE)
    static constexpr Level floor = Level::Informative;
#else
    static constexpr Level floor = Level::Traceback;
#endif

    /// @brief Sets the logger up from the command line's own options.
    ///
    /// `--debug` and `--trace` report everything at that level, `--debug=`
    /// followed by any of `lexer`, `mouth`, `parser`, `layout`, `memory` and
    /// `semantics` reports only those, `--log-level=` sets the least level
    /// reported, `--log-file=` copies every report to a file as well, and
    /// `--no-color` leaves the colors out. Anything else is left alone, so
    /// the driver reads the same arguments afterwards.
    ///
    /// @param count     How many arguments there are, `argc`.
    /// @param arguments The arguments, `argv`.
    static void compose(int count, char** arguments);

    /// @brief Reports these subsystems, and no others.
    static void types(Type target) noexcept;
    /// @brief Reports these subsystems as well.
    static void enable(Type target) noexcept;
    /// @brief Reports nothing below this level.
    static void level(Level value) noexcept;
    /// @brief Colors each report by its level, or leaves it plain.
    static void color(bool flag) noexcept;
    /// @brief Copies every report to a file, appending to it.
    static void file(const std::string& path);
    /// @brief Closes the file reports are copied to, if there is one.
    static void dispose();

    /// @brief Would a report of this subsystem, at this level, be written?
    ///
    /// Lock-free, so it may be asked from anywhere, a noexcept path included.
    [[nodiscard]] static bool check(Type target, Level value) noexcept;

    /// @brief A report's pattern, and where in the engine it was written.
    ///
    /// Made from the string literal a call is written with -- checked
    /// against its arguments when the engine is compiled, exactly as
    /// std::format checks one -- and remembering the call's own place, which
    /// a report copied to a file names for a warning or an error.
    template <typename... Args>
    struct Pattern {
        /// @brief Reads a literal pattern, and where it was written.
        /// @param text  The pattern.
        /// @param where The call's place; left to its default.
        template <typename Text>
        consteval Pattern(const Text& text, const std::source_location where = std::source_location::current())
            : format(text), location(where) {}

        std::format_string<Args...> format;   ///< The pattern, checked.
        std::source_location location;       ///< Where it was written.
    };

    /// @brief Reports something, formatted the way std::format formats.
    ///
    /// @code
    /// Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Reading '{}'", key);
    /// Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound page primitives");
    /// @endcode
    ///
    /// Everything happens here, inline, until something is actually written:
    /// the level is a constant at every call, so a report below #floor is
    /// dropped when the engine is compiled -- its text and its arguments
    /// with it -- and one the run does not ask for costs one check.
    ///
    /// @param target  The subsystem reporting.
    /// @param value   How severe it is.
    /// @param pattern The report, as std::format takes one.
    /// @param args    What its placeholders stand for.
    template <typename... Args>
    static void log(const Type target, const Level value, const Pattern<std::type_identity_t<Args>...> pattern,
                    Args&&... args) noexcept {
        if constexpr (floor != Level::Traceback) {
            if (value < floor) return;
        }
        if (!check(target, value)) return;
        try {
            write(target, value, std::vformat(pattern.format.get(), std::make_format_args(args...)),
                  pattern.location);
        } catch (...) {
        }
    }

    /// @brief A subsystem's name, as a report prints it.
    [[nodiscard]] static std::string_view name(Type target) noexcept;
    /// @brief A level's name, as a report prints it.
    [[nodiscard]] static std::string_view name(Level value) noexcept;

private:
    /// @brief Writes one report that check() has already let through.
    /// @param target   The subsystem reporting.
    /// @param value    How severe it is.
    /// @param text     The report, formatted.
    /// @param location Where it was written.
    static void write(Type target, Level value, std::string_view text,
                      const std::source_location& location) noexcept;

    inline static std::mutex mutex{};
    inline static std::atomic<Type> mask{Type::None};          // read by check() without the lock
    inline static std::atomic<Level> threshold{Level::Error};
    inline static std::atomic<bool> ansi{true};
    inline static std::ofstream stream{};
};

[[nodiscard]] constexpr Logger::Type operator|(const Logger::Type left, const Logger::Type right) noexcept {
    return static_cast<Logger::Type>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr Logger::Type operator&(const Logger::Type left, const Logger::Type right) noexcept {
    return static_cast<Logger::Type>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}

constexpr Logger::Type& operator|=(Logger::Type& left, const Logger::Type right) noexcept {
    left = left | right;
    return left;
}
