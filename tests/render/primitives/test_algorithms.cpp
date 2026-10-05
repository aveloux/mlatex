#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Algorithms primitive: algorithmicx's lines -- \State numbered, each
// indented by the blocks around it, \Statex not -- inside the algorithm
// float, with algpseudocode's words for the blocks.

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
static Result typeset(const std::string_view document, const latex::Host& host = {}) {
    static const std::filesystem::path assets = latex::locate(__FILE__);
    Result result;
    std::ostringstream errors;
    result.clean = latex::typeset(assets, document, result.pdf, host, errors, &result.pages);
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
    const Result result = article(
        "\\usepackage{algorithm,algpseudocode}",
        "\\begin{algorithm}\\caption{Sum}\\begin{algorithmic}[1]"
        "\\Require a list $L$ \\State $s \\gets 0$ \\For{$x \\in L$} \\State $s \\gets s + x$ \\EndFor"
        "\\Statex \\If{$s > 10$} \\State \\Return $s$ \\EndIf"
        "\\end{algorithmic}\\end{algorithm}");
    assert((result.clean) && "an algorithm reads without a report");
    assert((holds(result.text, "Algorithm1Sum")) && "captioned as a float");
    assert((holds(result.text, "Require:alistL")) && "its input");
    assert((holds(result.text, "1:s\xE2\x86\x90" "0") || holds(result.text, "1s\xE2\x86\x90" "0")) &&
           "each \\State numbered");
    assert((holds(result.text, "for") && holds(result.text, "endfor") && holds(result.text, "endif")) &&
           "the blocks opened and closed in words");

    const Result unnumbered = article("\\usepackage{algorithm,algpseudocode}",
                                      "\\begin{algorithmic}\\State one \\State two\\end{algorithmic}");
    assert((unnumbered.clean && holds(unnumbered.text, "onetwo") && !holds(unnumbered.text, "1:")) &&
           "without [1], the lines are not numbered");

    return 0;
}
