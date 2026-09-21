/// @file
/// @brief Logger implementation: filtering, formatting and sinks.
///
/// The filter state lives in atomics so that check() can be called from
/// noexcept paths without taking the mutex -- log() takes it a moment later,
/// and std::mutex is not recursive. Every entry point here swallows its own
/// failures: Cursor::advance and Registers::fetch are noexcept and call
/// straight into this file, so a throwing std::format would mean terminate().
#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <format>
#include <iostream>

namespace {

    [[nodiscard]] std::string_view tint(const Logger::Level value) noexcept {
        switch (value) {
            case Logger::Level::Traceback:   return "\x1b[90m";
            case Logger::Level::Debug:       return "\x1b[36m";
            case Logger::Level::Informative: return "\x1b[32m";
            case Logger::Level::Warning:     return "\x1b[33m";
            case Logger::Level::Error:       return "\x1b[31m";
            default:                         return "";
        }
    }

    constexpr std::string_view plain = "\x1b[0m";

}

void Logger::init(const int count, char** arguments) {
    types(Type::None);
    level(Level::Error);
    color(true);

    bool selective = false;

    for (int index = 1; index < count; ++index) {
        const std::string_view option(arguments[index]);

        if (option == "--debug" || option == "-d") {
            types(Type::All);
            level(Level::Debug);
        } else if (option == "--trace") {
            types(Type::All);
            level(Level::Traceback);
        } else if (option.starts_with("--debug=")) {
            if (!selective) {
                types(Type::None);
                selective = true;
            }
            const std::string_view value = option.substr(8);
            if (value.contains("lexer"))     enable(Type::Lexer);
            if (value.contains("mouth"))     enable(Type::Mouth);
            if (value.contains("parser"))    enable(Type::Parser);
            if (value.contains("layout"))    enable(Type::Layout);
            if (value.contains("memory"))    enable(Type::Memory);
            if (value.contains("semantics")) enable(Type::Semantics);
            if (value.contains("all"))       enable(Type::All);
            level(Level::Debug);
        } else if (option.starts_with("--log-level=")) {
            const std::string_view value = option.substr(12);
            if (value == "traceback")   level(Level::Traceback);
            else if (value == "debug")  level(Level::Debug);
            else if (value == "info")   level(Level::Informative);
            else if (value == "warn")   level(Level::Warning);
            else if (value == "error")  level(Level::Error);
            else if (value == "silent") level(Level::Silent);
        } else if (option.starts_with("--log-file=")) {
            file(std::string(option.substr(11)));
        } else if (option == "--no-color") {
            color(false);
        }
    }
}

void Logger::types(const Type target) noexcept {
    mask.store(target, std::memory_order_relaxed);
}

void Logger::enable(const Type target) noexcept {
    Type current = mask.load(std::memory_order_relaxed);
    while (!mask.compare_exchange_weak(current, current | target,
                                       std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

void Logger::disable(const Type target) noexcept {
    Type current = mask.load(std::memory_order_relaxed);
    while (!mask.compare_exchange_weak(current, current & ~target,
                                       std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

void Logger::level(const Level value) noexcept {
    threshold.store(value, std::memory_order_relaxed);
}

void Logger::color(const bool flag) noexcept {
    ansi.store(flag, std::memory_order_relaxed);
}

void Logger::file(const std::string& path) {
    const std::lock_guard guard(mutex);
    if (stream.is_open()) {
        stream.close();
    }
    stream.open(path, std::ios::out | std::ios::app);
}

void Logger::close() {
    const std::lock_guard guard(mutex);
    if (stream.is_open()) {
        stream.close();
    }
}

bool Logger::check(const Type target, const Level value) noexcept {
    if (static_cast<std::uint8_t>(value) < static_cast<std::uint8_t>(threshold.load(std::memory_order_relaxed))) {
        return false;
    }
    return static_cast<std::uint32_t>(mask.load(std::memory_order_relaxed) & target) != 0;
}

void Logger::log(const Type target, const Level value, const std::string_view text,
                 const std::source_location& location) noexcept {
    if (!check(target, value)) return;

    try {
        const auto now = std::chrono::system_clock::now();
        const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
        std::tm parts{};
    #if defined(_WIN32)
        localtime_s(&parts, &seconds);
    #else
        localtime_r(&seconds, &parts);
    #endif

        const std::string stamp = std::format("{:02}:{:02}:{:02}", parts.tm_hour, parts.tm_min, parts.tm_sec);
        const std::string_view tag = name(target);
        const std::string_view rank = name(value);

        const bool colored = ansi.load(std::memory_order_relaxed);
        const std::string output = std::format("[{}] [{}] [{}] {}\n", stamp, tag, rank, text);

        const std::lock_guard guard(mutex);

        if (colored) {
            std::clog << tint(value) << output << plain << std::flush;
        } else {
            std::clog << output << std::flush;
        }

        if (stream.is_open()) {
            stream << output;
            if (value >= Level::Warning) {
                stream << std::format("    at {}:{} ({})\n",
                                      location.file_name(), location.line(), location.function_name());
            }
            stream.flush();
        }
    } catch (...) {
    }
}

std::string_view Logger::name(const Type target) noexcept {
    switch (target) {
        case Type::Lexer:     return "Lexer";
        case Type::Mouth:     return "Mouth";
        case Type::Parser:    return "Parser";
        case Type::Layout:    return "Layout";
        case Type::Memory:    return "Memory";
        case Type::Semantics: return "Semantics";
        case Type::All:       return "All";
        default:              return "General";
    }
}

std::string_view Logger::name(const Level value) noexcept {
    switch (value) {
        case Level::Traceback:   return "Traceback";
        case Level::Debug:       return "Debug";
        case Level::Informative: return "Informative";
        case Level::Warning:     return "Warning";
        case Level::Error:       return "Error";
        default:                 return "Log";
    }
}