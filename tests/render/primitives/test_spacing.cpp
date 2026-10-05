#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Spacing primitive: space a document writes out -- \hskip and \vskip
// with their stretch and shrink, \kern -- and space fixed by its name, from
// a thin space to \hfill, with the glue a register holds carried along.

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
    {
        const Result result = article("", "\\smallskip a \\medskip b \\bigskip c \\vskip 12pt d \\hskip 1em e "
                                          "\\hfill f \\quad g \\qquad h \\, i \\enskip j \\kern 3pt k \\hfil l "
                                          "\\vfill m \\hspace{2em} n \\vspace{1ex} o \\hspace*{1pt} p");
        assert((result.clean && holds(result.text, "abcdefghijklmnop")) && "every kind of space, the words all kept");
    }
    {
        const Result result = article("", "\\hskip 0pt plus 1fill Right\\par");
        assert((result.clean && holds(result.text, "Right")) && "glue that fills pushes a word to the margin");
        assert((holds(result.pdf, " 0 0 -1 4")) && "far to the right of the column's left edge");
    }
    {
        const Result result = article("\\newskip\\gap \\gap=0pt plus 1fill", "\\hskip\\gap Right\\par");
        assert((result.clean && holds(result.pdf, " 0 0 -1 4")) && "a glue register's stretch comes with it");
    }
    {
        const Result result = article("", "a\\hspace{-1em}b");
        assert((result.clean && holds(result.text, "ab")) && "a negative space draws letters together");
    }
    return 0;
}
