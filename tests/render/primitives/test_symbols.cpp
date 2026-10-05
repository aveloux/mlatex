#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Symbols primitive: the characters a document names rather than types
// -- the text symbols, the accents over letters, the letters of other
// alphabets, TeX's and LaTeX's logos -- and the characters the lexer would
// otherwise take for instructions, escaped.

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
        const Result result = article("", "\\ldots\\ \\textbullet\\ \\S\\ \\dag\\ \\copyright\\ \\textregistered\\ "
                                          "\\texttrademark\\ \\textdegree\\ \\texteuro\\ \\pounds");
        assert((result.clean && holds(result.text, "\xE2\x80\xA6\xE2\x80\xA2\xC2\xA7\xE2\x80\xA0\xC2\xA9\xC2\xAE")) &&
               "the text symbols");
    }
    {
        const Result result = article("", "\\'e \\`e \\^e \\\"e \\~n \\c{c} \\v{c} \\H{o} \\r{a}");
        assert((result.clean &&
               holds(result.text, "\xC3\xA9\xC3\xA8\xC3\xAA\xC3\xAB\xC3\xB1\xC3\xA7\xC4\x8D\xC5\x91\xC3\xA5")) &&
               "accented letters, as their own characters");
    }
    {
        const Result result = article("", "\\ae\\ \\AE\\ \\oe\\ \\o\\ \\ss\\ \\l\\ \\aa");
        assert((result.clean && holds(result.text, "\xC3\xA6\xC3\x86\xC5\x93\xC3\xB8\xC3\x9F\xC5\x82\xC3\xA5")) &&
               "other alphabets' letters");
    }
    {
        const Result result = article("", "\\TeX\\ and \\LaTeX");
        assert((result.clean && holds(result.text, "TEXandLATEX")) && "the logos, copied as their letters, as pdfLaTeX's are");
    }
    {
        const Result result = article("", "\\% \\& \\# \\$ \\_ \\{ \\} \\textbackslash");
        assert((result.clean && holds(result.text, "%&#$_{}\\")) && "the characters a document has to escape");
    }
    {
        const Result result = article("\\usepackage{mhchem}", "\\ce{H2O} and \\ce{2H2 + O2 -> 2H2O}");
        assert((result.clean && holds(result.text, "H2O") && holds(result.text, "\xE2\x86\x92")) &&
               "a chemical formula, its reaction's arrow");
    }
    return 0;
}
