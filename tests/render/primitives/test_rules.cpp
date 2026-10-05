#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Rules primitive: \rule of a width and height, raised or lowered by
// its optional part, TeX's \hrule and \vrule with their keywords, and
// \underline.

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
        const Result result = article("", "a\\rule{20pt}{4pt}b");
        assert((result.clean && holds(result.pdf, " 20 4 re f")) && "a rule of a width and a height");
    }
    {
        const Result result = article("", "a\\rule[2pt]{20pt}{4pt}b\\rule[-3pt]{10pt}{1pt}");
        assert((result.clean && holds(result.pdf, " 20 4 re f") && holds(result.pdf, " 10 1 re f")) &&
               "raised or lowered, it keeps its size");
    }
    {
        const Result result = article("\\newlength{\\w}\\setlength{\\w}{30pt}", "\\rule{\\w}{1pt}\\rule{0.5\\w}{1pt}");
        assert((result.clean && holds(result.pdf, " 30 1 re f") && holds(result.pdf, " 15 1 re f")) &&
               "measured by a length, or a part of one");
    }
    {
        const Result result = article("", "\\hrule\\hrule height 2pt\\vrule width 3pt height 8pt depth 2pt\\par");
        assert((result.clean && holds(result.pdf, " 345 0.4 re f") && holds(result.pdf, " 345 2 re f") &&
               holds(result.pdf, " 3 10 re f")) && "\\hrule runs across the column, "
                                 "\\vrule as tall as asked, with TeX's keywords");
    }
    {
        const Result result = article("", "\\underline{under}");
        assert((result.clean && holds(result.text, "under") && holds(result.pdf, " re f")) && "a word underlined");
    }
    {
        const Result result = article("\\usepackage{xcolor}", "\\textcolor{blue}{\\rule{1em}{1em}}");
        assert((result.clean && holds(result.pdf, "0 0 1 rg")) && "a rule in the color of the text around it");
    }

    return 0;
}
