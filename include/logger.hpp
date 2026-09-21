#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>

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

    static void init(int count, char** arguments);

    static void types(Type target) noexcept;
    static void enable(Type target) noexcept;
    static void disable(Type target) noexcept;
    static void level(Level value) noexcept;
    static void color(bool flag) noexcept;
    static void file(const std::string& path);
    static void close();

    [[nodiscard]] static bool check(Type target, Level value) noexcept;

    static void log(Type target, Level value, std::string_view text,
                    const std::source_location& location = std::source_location::current()) noexcept;

    template <typename... Args>
    static void fmt(const Type target, const Level value,
                    const std::format_string<Args...> pattern, Args&&... args) noexcept {
        if (!check(target, value)) return;
        try {
            log(target, value, std::vformat(pattern.get(), std::make_format_args(args...)));
        } catch (...) {
        }
    }

    [[nodiscard]] static std::string_view name(Type target) noexcept;
    [[nodiscard]] static std::string_view name(Level value) noexcept;

private:
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

[[nodiscard]] constexpr Logger::Type operator~(const Logger::Type value) noexcept {
    return static_cast<Logger::Type>(~static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(Logger::Type::All));
}

constexpr Logger::Type& operator|=(Logger::Type& left, const Logger::Type right) noexcept {
    left = left | right;
    return left;
}

constexpr Logger::Type& operator&=(Logger::Type& left, const Logger::Type right) noexcept {
    left = left & right;
    return left;
}