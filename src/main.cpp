/// @file
/// @brief The command line: `latex [options] [document]`.
///
/// Everything the engine does is latex::compose(), in latex.cpp. What is
/// here is only what the command line owns: where the executable is and its
/// assets with it, which document to set and where its PDF goes, what it is
/// made into, and the options -- TeX's own where TeX has one, written with
/// one dash or two as TeX takes them, so a script that runs pdflatex runs
/// this the same way. Values and commands a program hands a document are
/// the bindings' (latex::Session, the C library, latex.js), not this.
///
/// @code
/// latex paper                                  # paper.mtex, or paper.tex, into paper.pdf
/// latex -interaction=batchmode -halt-on-error paper
/// latex --output-directory=out --jobname=final paper.mtex
/// latex -I styles -I figures paper             # what it inputs, from there too
/// latex --target=jit paper                     # just in time: not built yet
/// @endcode
#include "latex.hpp"
#include "logger.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

#if !defined(LATEX_VERSION)
    #define LATEX_VERSION "0.1.0"
#endif

/// What `latex --help` prints.
static constexpr std::string_view help = R"(Usage: latex [options] [document]

Typesets a document into a PDF. A document named without an extension is
looked for as NAME.mtex, then NAME.tex; with none named, the engine's own
sample, build/main.mtex, is set.

Target:
  -t, --target=TARGET          What the document is made into:
                                 aot   ahead of time: a PDF, now. The default.
                                 jit   just in time. Not built yet: accepted,
                                       and nothing is done.
                                 wasm  in WebAssembly. Not built into this
                                       program yet: accepted, and nothing is
                                       done.

Output:
  -o, --output-directory=DIR   Write the PDF into DIR, made if it is not there;
                               beside the document otherwise.
  -j, --jobname=NAME           Name the PDF NAME.pdf, not after the document.
      --draftmode              Read and set the document; write no PDF.
      --halt-on-error          Leave no PDF when the document has an error.

Input:
  -I, --include-directory=DIR  Look in DIR, after the document's own folder,
                               for what it inputs: \input, \include,
                               \usepackage, \includegraphics, a bibliography.
                               Give it again for another folder.
      --assets=DIR             Read the fonts and hyphenation patterns from
                               DIR, not from the assets found above this
                               program.

Messages:
  -i, --interaction=MODE       batchmode prints nothing but errors;
                               nonstopmode, scrollmode and errorstopmode print
                               what was done.
  -q, --quiet                  The same as --interaction=batchmode.
      --file-line-error        Print each error as file:line:column: message.
      --time-statistics        Print what the engine did and how long each
                               step took.
  -h, --help                   Print this help and exit.
  -v, --version                Print the version and exit.

Diagnostics:
  -d, --debug[=PARTS]          Log the engine's steps: every part, or those
                               named -- lexer, mouth, parser, layout, memory,
                               semantics.
      --trace                  Log every step, in full.
      --log-level=LEVEL        traceback, debug, info, warn, error or silent.
      --log-file=FILE          Write the log to FILE.
      --no-color               Log without colour.

Options start with one dash or two, as TeX's do: -interaction=batchmode is
--interaction=batchmode. A value follows its option after `=` or as the next
argument; `--` ends the options. The exit status is 0 when the document set
cleanly, 1 when it had an error, and 2 when the command line did.
)";

/// @brief Typesets the document the command line names.
/// @param count     How many arguments there are, the program's own name first.
/// @param arguments The arguments, as the system handed them over.
/// @return 0 when the document set cleanly, 1 when it had an error, 2 when
///         the command line did.
int main(int count, char* arguments[]) {
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
    std::string jobname;               // --jobname
    bool quiet = false;                // --interaction=batchmode, --quiet
    bool halting = false;              // --halt-on-error
    bool draft = false;                // --draftmode
    bool placed = false;               // --file-line-error
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

        // An option, `-name` or `--name`, and `-n` for the ones that have a
        // letter of their own; its value after `=`, or the next argument.
        const bool doubled = argument.starts_with("--");
        std::string_view name = argument.substr(doubled ? 2 : 1);
        std::optional<std::string_view> value;
        if (const std::size_t equals = name.find('='); equals != std::string_view::npos) {
            value = name.substr(equals + 1);
            name = name.substr(0, equals);
        }
        if (!doubled && name.size() == 1) {
            switch (name.front()) {
                case 't': name = "target"; break;
                case 'o': name = "output-directory"; break;
                case 'j': name = "jobname"; break;
                case 'I': name = "include-directory"; break;
                case 'i': name = "interaction"; break;
                case 'q': name = "quiet"; break;
                case 'h': name = "help"; break;
                case 'v': name = "version"; break;
                case 'd': name = "debug"; break;
                default: break;
            }
        }
        const auto take = [&]() -> std::optional<std::string_view> {
            if (value) return value;
            if (index + 1 < count && arguments[index + 1]) return std::string_view(arguments[++index]);
            std::cerr << "latex: " << argument << " needs a value; see latex --help\n";
            return std::nullopt;
        };

        if (name == "help") {
            std::cout << help;
            return 0;
        }
        if (name == "version") {
#if defined(__clang__)
            const std::string compiler = "Clang " + std::to_string(__clang_major__) + '.' +
                                         std::to_string(__clang_minor__) + '.' + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
            const std::string compiler = "GCC " + std::to_string(__GNUC__) + '.' + std::to_string(__GNUC_MINOR__) +
                                         '.' + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
            const std::string compiler = "MSVC " + std::to_string(_MSC_VER);
#else
            const std::string compiler = "an unknown compiler";
#endif
#if defined(_WIN32)
            constexpr std::string_view system = "windows";
#elif defined(__APPLE__)
            constexpr std::string_view system = "macos";
#else
            constexpr std::string_view system = "linux";
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
        if (name == "target") {
            const auto given = take();
            if (!given) return 2;
            if (*given != "aot" && *given != "jit" && *given != "wasm") {
                std::cerr << "latex: --target is aot, jit or wasm, not " << *given << '\n';
                return 2;
            }
            target = *given;
        } else if (name == "output-directory") {
            const auto given = take();
            if (!given) return 2;
            directory = *given;
        } else if (name == "jobname") {
            const auto given = take();
            if (!given) return 2;
            jobname = *given;
        } else if (name == "include-directory") {
            const auto given = take();
            if (!given) return 2;
            if (!std::filesystem::is_directory(*given, failure)) {
                std::cerr << "latex: no folder " << *given << " for --include-directory\n";
                return 2;
            }
            host.directories.emplace_back(*given);
        } else if (name == "assets") {
            const auto given = take();
            if (!given) return 2;
            assets = *given;
            if (!std::filesystem::is_directory(assets / "fonts", failure)) {
                std::cerr << "latex: " << assets.string() << " holds no fonts folder; --assets names the engine's "
                          << "assets, as the source tree's assets folder is\n";
                return 2;
            }
        } else if (name == "interaction") {
            const auto given = take();
            if (!given) return 2;
            if (*given == "batchmode") {
                quiet = true;
            } else if (*given == "nonstopmode" || *given == "scrollmode" || *given == "errorstopmode") {
                quiet = false;
            } else {
                std::cerr << "latex: --interaction is batchmode, nonstopmode, scrollmode or errorstopmode, not "
                          << *given << '\n';
                return 2;
            }
        } else if (name == "quiet") {
            quiet = true;
        } else if (name == "halt-on-error") {
            halting = true;
        } else if (name == "draftmode") {
            draft = true;
        } else if (name == "file-line-error") {
            placed = true;
        } else if (name == "time-statistics") {
            statistics = true;
        } else if (name == "debug" || name == "trace" || name == "log-level" || name == "log-file" ||
                   name == "no-color") {
            // The logger's own, read already.
        } else {
            std::cerr << "latex: no option " << argument << "; see latex --help\n";
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

    // The document: as named, or with .mtex or .tex after a name given
    // without one, as TeX finds `paper` as paper.tex; with none named, the
    // sample beside the build.
    if (source.empty()) {
        source = assets.parent_path() / "build" / "main.mtex";
    } else if (!source.has_extension() && !std::filesystem::exists(source, failure)) {
        for (const std::string_view extension : {".mtex", ".tex"}) {
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
        destination = folder / ((jobname.empty() ? source.stem().string() : jobname) + ".pdf");
    }

    if (!quiet) std::cout << "This is latex " << LATEX_VERSION << " (" << target << ").\n" << source.string() << '\n';

    // Each error with its file before it, as TeX's -file-line-error writes
    // it, when that was asked for: the engine's own say only line and column.
    std::ostringstream held;
    std::vector<std::string> pages;
    const bool ok = latex::compose(assets, source, destination, host, statistics ? &std::cout : nullptr,
                                   placed ? static_cast<std::ostream&>(held) : std::cerr, &pages);
    if (placed) {
        std::istringstream lines(held.str());
        for (std::string line; std::getline(lines, line);) {
            const bool numbered = !line.empty() && line.front() >= '0' && line.front() <= '9';
            std::cerr << (numbered ? source.filename().string() + ':' : std::string()) << line << '\n';
        }
    }

    // A document with an error leaves no PDF behind it, when that was asked;
    // otherwise what was written is said as TeX says it.
    if (!ok && halting && !destination.empty()) {
        std::filesystem::remove(destination, failure);
        std::cerr << "latex: no PDF written: the document has an error, and --halt-on-error was given\n";
    } else if (!quiet) {
        if (destination.empty()) {
            std::cout << "No PDF written: --draftmode.\n";
        } else if (const std::uintmax_t bytes = std::filesystem::file_size(destination, failure); !failure) {
            std::cout << "Output written on " << destination.string() << " (" << pages.size()
                      << (pages.size() == 1 ? " page, " : " pages, ") << bytes << " bytes).\n";
        }
    }

    return ok ? 0 : 1;
}
