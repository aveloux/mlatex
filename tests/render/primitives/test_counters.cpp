#include "engine.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Counters primitive: LaTeX's counters -- made, stepped, set and added
// to, printed in every style, reset by the counter they are numbered
// within -- and the ones the classes keep: sections, equations, figures.

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
    {
        const Result result = article("", "\\newcounter{n}\\stepcounter{n}A:\\arabic{n} \\roman{n} \\Roman{n} "
                                          "\\alph{n} \\Alph{n} \\fnsymbol{n}. \\setcounter{n}{2021}B:\\arabic{n} "
                                          "\\Roman{n}. \\addtocounter{n}{-2020}C:\\value{n}.");
        assert((result.clean) && "counters in every style");
        assert((holds(result.text, "A:1iIaA*.")) && "one, written every way");
        assert((holds(result.text, "B:2021MMXXI.")) && "a large number in Roman capitals");
        assert((holds(result.text, "C:1.")) && "\\addtocounter and \\value");
    }
    {
        const Result result = article("\\newcounter{step}[section]",
                                      "\\section{A}\\stepcounter{step}\\stepcounter{step}\\thestep "
                                      "\\section{B}\\stepcounter{step}\\thestep");
        assert((result.clean && holds(result.text, "1A2") && holds(result.text, "2B1")) &&
               "a counter numbered within another starts again with it");
    }
    {
        const Result result = article("\\numberwithin{equation}{section}",
                                      "\\section{A}\\begin{equation}x\\end{equation}"
                                      "\\section{B}\\begin{equation}y\\end{equation}");
        assert((result.clean && holds(result.text, "(1.1)") && holds(result.text, "(2.1)")) &&
               "\\numberwithin numbers equations by their section");
    }
    {
        const Result result = article("", "\\setcounter{secnumdepth}{0}\\section{Unnumbered}");
        assert((result.clean && !holds(result.text, "1Unnumbered")) && "secnumdepth turns numbering off");
    }
    return 0;
}
