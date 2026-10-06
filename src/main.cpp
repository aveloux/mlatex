/// @file
/// @brief The command line: `latex [options] [document]`.
///
/// Everything the engine does is latex::compose(), in latex.cpp. What is
/// here is only what the command line owns: where the executable is and its
/// assets with it, which document to set and where its PDF goes, what it is
/// made into, and the options. Each option is a row of #options -- its
/// letter, its name, the TeX spelling it also answers to, what it takes and
/// what `--help` says of it -- so adding one is a row there and what it does
/// below. Names are written in words with dashes between, `--job-name`,
/// `--interaction=batch-mode`; TeX's own run-together spellings, `-jobname`,
/// `batchmode`, are taken too, so a script that runs pdflatex runs this the
/// same way. Values and commands a program hands a document are the
/// bindings' (latex::Session, the C library, latex.js), not this.
///
/// @code
/// latex paper                                  # paper.mtex, or paper.tex, into paper.pdf
/// latex --interaction=batch-mode --halt-on-error paper
/// latex --output-directory=out --job-name=final paper.mtex
/// latex -I styles -I figures paper             # what it inputs, from there too
/// latex --watch --open paper                   # set again on every save, the PDF shown
/// latex --time paper                           # how long it took
/// @endcode
#include "latex.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include <windows.h>
    #include <shellapi.h>
#else
    #include <spawn.h>
    #if defined(__APPLE__)
        #include <mach-o/dyld.h>
    #endif
extern char** environ;
#endif

#if !defined(LATEX_VERSION)
    #define LATEX_VERSION "0.1.0"
#endif

/// @brief One option the command line takes, as `--help` lists it.
struct Option {
    char letter{'\0'};             ///< Its letter, `-o`; none when '\0'.
    std::string_view name{};       ///< Its name, `output-directory`.
    std::string_view alias{};      ///< TeX's spelling it answers to as well, `jobname`; empty for none.
    std::string_view value{};      ///< What it takes, `DIR`; `[PARTS]` when it may go without; empty for a switch.
    std::string_view group{};      ///< The heading `--help` lists it under.
    std::string_view summary{};    ///< What `--help` says of it; a line break starts its next line.
};

/// Every option, in the order `--help` lists them. A new one is a row here
/// and a branch where main() reads its name.
static constexpr std::array options{
    Option{'t', "target", "", "TARGET", "Target",
           "What the document is made into: aot, a PDF now\n"
           "(the default); jit, just in time; wasm, in\n"
           "WebAssembly. The last two are taken and do\n"
           "nothing yet."},
    Option{'o', "output-directory", "", "DIR", "Output",
           "Write the PDF into DIR, made if it is not there;\n"
           "beside the document otherwise."},
    Option{'j', "job-name", "jobname", "NAME", "Output", "Name the PDF NAME.pdf, not after the document."},
    Option{'n', "draft-mode", "draftmode", "", "Output", "Read and set the document; write no PDF."},
    Option{'\0', "halt-on-error", "", "", "Output", "Leave no PDF when the document has an error."},
    Option{'O', "open", "", "", "Output", "Show the PDF in the system's viewer once written."},
    Option{'I', "include-directory", "", "DIR", "Input",
           "Look in DIR, after the document's own folder,\n"
           "for what it inputs: \\input, \\include,\n"
           "\\usepackage, \\includegraphics, a bibliography.\n"
           "Give it again for another folder."},
    Option{'\0', "assets", "", "DIR", "Input",
           "Read the fonts and hyphenation patterns from\n"
           "DIR, not from the assets found above this\n"
           "program."},
    Option{'\0', "offline", "", "", "Input",
           "Reach no network: \\httpget, \\httppost and a\n"
           "picture from https:// are errors, and nothing is\n"
           "sent. For a document you did not write."},
    Option{'w', "watch", "", "", "Input",
           "Set the document again each time it is saved,\n"
           "until interrupted."},
    Option{'i', "interaction", "", "MODE", "Messages",
           "batch-mode prints nothing but errors;\n"
           "non-stop-mode, scroll-mode and error-stop-mode\n"
           "print what was done."},
    Option{'q', "quiet", "", "", "Messages", "The same as --interaction=batch-mode."},
    Option{'\0', "file-line-error", "", "", "Messages", "Print each error as file:line:column: message."},
    Option{'T', "time", "", "", "Messages", "Print how long each part of the run took."},
    Option{'\0', "time-statistics", "", "", "Messages",
           "Print every step the engine took, what it made\n"
           "and how long each step took."},
    Option{'h', "help", "", "", "Messages", "Print this help and exit."},
    Option{'v', "version", "", "", "Messages", "Print the version and exit."},
    Option{'d', "debug", "", "[PARTS]", "Diagnostics",
           "Log the engine's steps: every part, or those\n"
           "named -- lexer, mouth, parser, layout, memory,\n"
           "semantics."},
    Option{'\0', "trace", "", "", "Diagnostics", "Log every step, in full."},
    Option{'\0', "log-level", "", "LEVEL", "Diagnostics", "traceback, debug, info, warn, error or silent."},
    Option{'\0', "log-file", "", "FILE", "Diagnostics", "Write the log to FILE."},
    Option{'\0', "no-color", "", "", "Diagnostics", "Log without colour."},
};

/// The interaction modes, each in words and as TeX runs it together.
static constexpr std::array<std::pair<std::string_view, std::string_view>, 4> modes{{
    {"batch-mode", "batchmode"},
    {"non-stop-mode", "nonstopmode"},
    {"scroll-mode", "scrollmode"},
    {"error-stop-mode", "errorstopmode"},
}};

/// @brief Typesets the document the command line names.
/// @param count     How many arguments there are, the program's own name first.
/// @param arguments The arguments, as the system handed them over.
/// @return 0 when the document set cleanly, 1 when it had an error, 2 when
///         the command line did.
int main(int count, char* arguments[]) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point begun = Clock::now();
    Logger::compose(count, arguments);

    // The logger's file, closed on every way out.
    const struct Ending {
        ~Ending() { Logger::dispose(); }
    } ending;

    // What the command line asks for.
    std::string_view target = "aot";   // --target
    std::filesystem::path source;      // the document, when one is named
    std::filesystem::path directory;   // --output-directory
    std::filesystem::path assets;      // --assets
    std::string job;                   // --job-name
    bool quiet = false;                // --interaction=batch-mode, --quiet
    bool halting = false;              // --halt-on-error
    bool draft = false;                // --draft-mode
    bool opening = false;              // --open
    bool watching = false;             // --watch
    bool placed = false;               // --file-line-error
    bool timed = false;                // --time
    bool statistics = false;           // --time-statistics
    bool ended = false;                // past `--`
    latex::Host host;
    std::error_code failure;

    for (int index = 1; index < count; ++index) {
        const std::string_view argument = arguments[index] ? arguments[index] : "";
        if (argument.empty()) continue;

        if (!ended && argument == "--") {
            ended = true;
            continue;
        }

        // The document: the first argument that is not an option.
        if (ended || argument == "-" || !argument.starts_with('-')) {
            if (!source.empty()) {
                std::cerr << "latex: one document at a time: " << source.string() << " and " << argument << '\n';
                return 2;
            }
            source = argument;
            continue;
        }

        // An option, `-name` or `--name` -- or TeX's spelling of it -- and
        // `-n` for one with a letter of its own; its value after `=`, or the
        // next argument.
        const bool doubled = argument.starts_with("--");
        std::string_view name = argument.substr(doubled ? 2 : 1);
        std::optional<std::string_view> value;
        if (const std::size_t equals = name.find('='); equals != std::string_view::npos) {
            value = name.substr(equals + 1);
            name = name.substr(0, equals);
        }
        const Option* option = nullptr;
        for (const Option& each : options) {
            const bool lettered = !doubled && name.size() == 1 && each.letter == name.front();
            if (lettered || each.name == name || (!each.alias.empty() && each.alias == name)) {
                option = &each;
                break;
            }
        }
        if (!option) {
            std::cerr << "latex: no option " << argument << "; see latex --help\n";
            return 2;
        }
        const auto take = [&]() -> std::optional<std::string_view> {
            if (value) return value;
            if (index + 1 < count && arguments[index + 1]) return std::string_view(arguments[++index]);
            std::cerr << "latex: " << argument << " needs a value; see latex --help\n";
            return std::nullopt;
        };
        const std::string_view called = option->name;

        if (called == "help") {
            // The options under their headings, each name and value in a
            // column of their own and what it does beside them.
            std::cout << "Usage: latex [options] [document]\n\n"
                         "Typesets a document into a PDF. A document named without an extension is\n"
                         "looked for as NAME.mtex, then NAME.tex, then NAME.bib -- a bibliography,\n"
                         "set as a list of every entry it holds; with none named, the engine's own\n"
                         "sample, build/main.mtex, is set.\n";
            std::string_view heading;
            for (const Option& each : options) {
                if (each.group != heading) {
                    heading = each.group;
                    std::cout << '\n' << heading << ":\n";
                }
                std::string written = each.letter ? std::format("  -{}, --{}", each.letter, each.name)
                                                  : std::format("      --{}", each.name);
                if (each.value.starts_with('[')) {
                    written += std::format("[={}]", each.value.substr(1, each.value.size() - 2));
                } else if (!each.value.empty()) {
                    written += std::format("={}", each.value);
                }
                std::string_view rest = each.summary;
                for (bool first = true; !rest.empty(); first = false) {
                    const std::size_t end = rest.find('\n');
                    std::cout << std::format("{:<31}{}\n", first ? written : std::string{}, rest.substr(0, end));
                    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
                }
            }
            std::cout << "\nOptions start with one dash or two: -interaction=batch-mode is\n"
                         "--interaction=batch-mode, and TeX's own spellings -- -jobname, -draftmode,\n"
                         "batchmode -- are taken too. A value follows its option after `=` or as the\n"
                         "next argument; `--` ends the options. The exit status is 0 when the document\n"
                         "set cleanly, 1 when it had an error, and 2 when the command line did.\n";
            return 0;
        }
        if (called == "version") {
#if defined(__clang__)
            const std::string compiler = std::format("Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
            const std::string compiler = std::format("GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
            const std::string compiler = std::format("MSVC {}", _MSC_VER);
#else
            const std::string compiler = "an unknown compiler";
#endif
#if defined(_WIN32)
            constexpr std::string_view system = "Windows";
#elif defined(__APPLE__)
            constexpr std::string_view system = "macOS";
#else
            constexpr std::string_view system = "Linux";
#endif
#if defined(LATEX_RELEASE)
            constexpr std::string_view build = "release";
#else
            constexpr std::string_view build = "debug";
#endif
            // C++26 is 202400 until the standard is out; past C++23 it is 26.
            constexpr int standard = __cplusplus > 202302L ? 26 : __cplusplus > 202002L ? 23 : 20;
            std::cout << "latex " << LATEX_VERSION << '\n'
                      << "Typesets LaTeX into PDF: one program, no TeX installation behind it.\n"
                      << "Built with " << compiler << " for " << system << ", C++" << standard << ", " << build
                      << ".\n";
            return 0;
        }

        // The rest set what the run does.
        if (called == "target") {
            const auto given = take();
            if (!given) return 2;
            if (*given != "aot" && *given != "jit" && *given != "wasm") {
                std::cerr << "latex: --target is aot, jit or wasm, not " << *given << '\n';
                return 2;
            }
            target = *given;
        } else if (called == "output-directory") {
            const auto given = take();
            if (!given) return 2;
            directory = *given;
        } else if (called == "job-name") {
            const auto given = take();
            if (!given) return 2;
            job = *given;
        } else if (called == "include-directory") {
            const auto given = take();
            if (!given) return 2;
            if (!std::filesystem::is_directory(*given, failure)) {
                std::cerr << "latex: no folder " << *given << " for --include-directory\n";
                return 2;
            }
            host.directories.emplace_back(*given);
        } else if (called == "assets") {
            const auto given = take();
            if (!given) return 2;
            assets = *given;
            if (!std::filesystem::is_directory(assets / "fonts", failure)) {
                std::cerr << "latex: " << assets.string() << " holds no fonts folder; --assets names the engine's "
                          << "assets, as the source tree's assets folder is\n";
                return 2;
            }
        } else if (called == "interaction") {
            const auto given = take();
            if (!given) return 2;
            const auto mode = std::ranges::find_if(modes, [&given](const auto& pair) {
                return pair.first == *given || pair.second == *given;
            });
            if (mode == modes.end()) {
                std::cerr << "latex: --interaction is batch-mode, non-stop-mode, scroll-mode or error-stop-mode, not "
                          << *given << '\n';
                return 2;
            }
            quiet = mode == modes.begin();
        } else if (called == "quiet") {
            quiet = true;
        } else if (called == "halt-on-error") {
            halting = true;
        } else if (called == "draft-mode") {
            draft = true;
        } else if (called == "open") {
            opening = true;
        } else if (called == "offline") {
            host.offline = true;
        } else if (called == "watch") {
            watching = true;
        } else if (called == "file-line-error") {
            placed = true;
        } else if (called == "time") {
            timed = true;
        } else if (called == "time-statistics") {
            statistics = true;
        } else if (!option->value.empty() && !option->value.starts_with('[') && !value) {
            // The logger's own are read already, and only with their value
            // after `=`.
            std::cerr << "latex: --" << called << " takes its value after `=`: --" << called << '=' << option->value
                      << '\n';
            return 2;
        }
    }

    // Just in time and WebAssembly are where those targets go once they are
    // built: each is taken, and nothing is done yet.
    if (target != "aot") {
        if (!quiet) std::cout << "latex: --target=" << target << " is not built yet; nothing was typeset.\n";
        return 0;
    }

    // Where the engine's assets are: as the command line says, or found from
    // where this executable is on disk -- relative to the engine, not to
    // whatever directory it happened to be started from. Asking the
    // operating system is exact; `argv[0]` is a fallback for the platforms
    // that will not say, and the working directory the last resort.
    if (assets.empty()) {
        std::filesystem::path binary;
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
        assets = latex::locate(binary);
        if (assets.empty()) {
            std::cerr << "latex: no assets directory found above " << binary.string() << "; name one with --assets\n";
            return 1;
        }
    }

    // The document: as named, or with .mtex, .tex or .bib after a name given
    // without one, as TeX finds `paper` as paper.tex; with none named, the
    // sample beside the build.
    if (source.empty()) {
        source = assets.parent_path() / "build" / "main.mtex";
    } else if (!source.has_extension() && !std::filesystem::exists(source, failure)) {
        for (const std::string_view extension : {".mtex", ".tex", ".bib"}) {
            if (std::filesystem::path named = std::filesystem::path(source).replace_extension(extension);
                std::filesystem::exists(named, failure)) {
                source = named;
                break;
            }
        }
    }
    if (!std::filesystem::exists(source, failure)) {
        std::cerr << "latex: no document at " << source.string() << '\n';
        return 1;
    }

    // Where its PDF goes: beside it, or into the output directory, named
    // after it or after the job.
    std::filesystem::path destination;
    if (!draft) {
        const std::filesystem::path folder = directory.empty() ? source.parent_path() : directory;
        if (!directory.empty()) std::filesystem::create_directories(directory, failure);
        destination = folder / ((job.empty() ? source.stem().string() : job) + ".pdf");
    }
    const Clock::time_point found = Clock::now();

    if (!quiet) std::cout << "This is latex " << LATEX_VERSION << ".\n";

    // One run: the document set and its PDF written, each error with its
    // file before it when -file-line-error asks, the PDF left out when
    // --halt-on-error asks, what was written said as TeX says it, and how
    // long it all took when --time asks.
    const auto run = [&](const Clock::time_point since) {
        if (!quiet) std::cout << source.string() << '\n';
        const Clock::time_point started = Clock::now();
        std::ostringstream held;
        std::vector<std::string> pages;
        const bool ok = latex::compose(assets, source, destination, host, statistics ? &std::cout : nullptr,
                                       placed ? static_cast<std::ostream&>(held) : std::cerr, &pages);
        const Clock::time_point finished = Clock::now();
        if (placed) {
            std::istringstream lines(held.str());
            for (std::string line; std::getline(lines, line);) {
                const bool numbered = !line.empty() && line.front() >= '0' && line.front() <= '9';
                std::cerr << (numbered ? source.filename().string() + ':' : std::string()) << line << '\n';
            }
        }

        if (!ok && halting && !destination.empty()) {
            std::filesystem::remove(destination, failure);
            std::cerr << "latex: no PDF written: the document has an error, and --halt-on-error was given\n";
        } else if (!quiet) {
            if (destination.empty()) {
                std::cout << "No PDF written: --draft-mode.\n";
            } else if (const std::uintmax_t bytes = std::filesystem::file_size(destination, failure); !failure) {
                std::cout << "Output written on " << destination.string() << " (" << pages.size()
                          << (pages.size() == 1 ? " page, " : " pages, ") << bytes << " bytes).\n";
            }
        }

        if (timed) {
            const auto milliseconds = [](const Clock::duration span) {
                return std::chrono::duration<double, std::milli>(span).count();
            };
            std::cout << "Time\n";
            if (since < found) {
                std::cout << std::format("  {:<36}{:>10.1f} ms\n", "Finding the document and its assets",
                                         milliseconds(found - since));
            }
            std::cout << std::format("  {:<36}{:>10.1f} ms\n", "Setting it and writing its PDF",
                                     milliseconds(finished - started))
                      << std::format("  {:<36}{:>10.1f} ms\n", "In all", milliseconds(finished - since));
        }
        return ok;
    };

    bool ok = run(begun);

    // Shown once written, in whatever the system opens a PDF with -- never
    // through a shell, so no file name is read as a command.
    if (opening && !destination.empty() && std::filesystem::exists(destination, failure)) {
#if defined(_WIN32)
        ShellExecuteW(nullptr, L"open", destination.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    #if defined(__APPLE__)
        std::string viewer = "open";
    #else
        std::string viewer = "xdg-open";
    #endif
        std::string path = destination.string();
        std::array<char*, 3> words{viewer.data(), path.data(), nullptr};
        pid_t process = 0;
        posix_spawnp(&process, viewer.c_str(), nullptr, nullptr, words.data(), environ);
#endif
    }

    // Watched: set again each time the document is saved, its time looked at
    // four times a second, until the program is interrupted.
    if (watching) {
        if (!quiet) std::cout << "Watching " << source.string() << "; interrupt to stop.\n";
        std::filesystem::file_time_type seen = std::filesystem::last_write_time(source, failure);
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const std::filesystem::file_time_type now = std::filesystem::last_write_time(source, failure);
            if (failure || now == seen) continue;
            seen = now;
            ok = run(Clock::now());
        }
    }

    return ok ? 0 : 1;
}
