/// @file
/// @brief Driver: finds the document and the assets, then runs the engine.
///
/// Everything the engine actually does is engine::compose(), in engine.cpp.
/// What is here is only what the CLI itself owns: where the executable is,
/// where its assets are relative to that, and which argument names the
/// document to set.
#include "engine.hpp"
#include "logger.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

int main(int count, char* arguments[]) {
    Logger::compose(count, arguments);

    // Where this executable is on disk: the assets are found relative to the
    // engine, not to whatever directory it happened to be started from.
    // Asking the operating system is exact; `argv[0]` is a fallback for the
    // platforms that will not say, and the working directory the last resort.
    std::filesystem::path binary;
    std::error_code failure;
#if defined(_WIN32)
    for (std::wstring buffer(MAX_PATH, L'\0'); binary.empty();) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) break;
        if (written < buffer.size()) {
            buffer.resize(written);
            binary = std::filesystem::path(buffer);
        } else {
            buffer.resize(buffer.size() * 2);
        }
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (std::string buffer(size, '\0'); _NSGetExecutablePath(buffer.data(), &size) == 0) {
        if (auto resolved = std::filesystem::canonical(buffer.c_str(), failure); !failure) binary = resolved;
    }
#else
    if (auto resolved = std::filesystem::read_symlink("/proc/self/exe", failure); !failure) binary = resolved;
#endif
    if (binary.empty() && count > 0 && arguments[0] && *arguments[0]) {
        if (auto resolved = std::filesystem::absolute(arguments[0], failure); !failure) binary = resolved;
    }
    if (binary.empty()) binary = std::filesystem::current_path();
    const std::filesystem::path assets = engine::locate(binary);

    if (assets.empty()) {
        std::cerr << "No assets directory found above " << binary.string() << '\n';
        return 1;
    }

    // The first argument that is not an option is the document to set; the
    // logger's options belong to the logger, which has already read them.
    // Without one, the document beside the build is set, which is what makes
    // running the engine with no arguments do something useful.
    //
    // `--set=name=value` hands the document a value it reads with
    // \variable{name} -- the same thing a program calling the engine does
    // through the foreign-function library, from a shell script instead.
    std::filesystem::path source = assets.parent_path() / "build" / "main.mtex";
    bool named = false;
    engine::Host host;
    for (int index = 1; index < count; ++index) {
        if (!arguments[index]) continue;
        const std::string_view argument(arguments[index]);

        if (argument.starts_with("--set=")) {
            const std::string_view pair = argument.substr(6);
            const std::size_t equals = pair.find('=');
            if (equals == std::string_view::npos || equals == 0) {
                std::cerr << "Expected --set=name=value, not " << argument << '\n';
                return 1;
            }
            host.variables.emplace_back(std::string(pair.substr(0, equals)), std::string(pair.substr(equals + 1)));
            continue;
        }
        if (!named && !argument.starts_with('-')) {
            source = argument;
            named = true;
        }
    }

    std::filesystem::path destination = source;
    destination.replace_extension(".pdf");

    const bool ok = engine::compose(assets, source, destination, host);

    Logger::close();
    return ok ? 0 : 1;
}
