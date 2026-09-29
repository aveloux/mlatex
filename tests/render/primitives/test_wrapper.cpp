#include "engine.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The render Wrapper: every render primitive folded into one callable that
// binds them all into a parser, and one list of what any of them reported,
// merged on demand -- seen here through a document that trips several.

/// One document as the engine set it: whether it reported nothing, what it
/// reported, its PDF, and each page's text -- and every page's together --
/// with the spaces and line ends taken out, which is what a check compares.
struct Result {
    bool clean{false};
    std::string errors{};
    std::string pdf{};
    std::vector<std::string> pages{};
    std::string text{};
};

/// Typesets a whole document, as written, with what a program hands in.
static Result typeset(const std::string_view document, const engine::Host& host = {}) {
    static const std::filesystem::path assets = engine::locate(__FILE__);
    Result result;
    std::ostringstream errors;
    result.clean = engine::typeset(assets, document, result.pdf, host, errors, &result.pages);
    result.errors = errors.str();
    for (std::string& page : result.pages) {
        std::erase_if(page, [](const char letter) { return letter == ' ' || letter == '\n'; });
        result.text += page;
    }
    return result;
}

/// Typesets a body in an article, after a preamble, and says what was
/// reported when anything was.
static Result article(const std::string_view preamble, const std::string_view body) {
    Result result = typeset("\\documentclass{article}" + std::string(preamble) + "\\begin{document}" +
                            std::string(body) + "\\end{document}");
    if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
    return result;
}

/// Whether a text holds another.
static bool holds(const std::string_view text, const std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

int main() {
    const Result result = typeset("\\documentclass{article}\\begin{document}"
                                  "\\pagestyle{nosuch}\\textfont{no-such-family}\\stepcounter{nosuch}"
                                  "\\includegraphics{nosuch.png}\\end{document}");
    assert((!result.clean) && "a document with mistakes in several primitives is reported");
    assert((holds(result.errors, "Unknown page style 'nosuch'")) && "the page primitive's");
    assert((holds(result.errors, "No font family named 'no-such-family'")) && "the typeface primitive's");
    assert((holds(result.errors, "No counter 'nosuch' defined")) && "the counters primitive's");
    assert((holds(result.errors, "could not read 'nosuch.png'")) &&
           "and the illustrations primitive's, in one report");

    const Result clean = article("", "Every primitive bound, nothing reported.");
    assert((clean.clean && clean.errors.empty()) && "a document with no mistakes reports nothing");

    return 0;
}
