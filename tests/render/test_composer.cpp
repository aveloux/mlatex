#include "engine.hpp"

#include <cassert>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The composer: each page drawn into its content stream -- runs of text
// placed and kerned, colors filled, rules as rectangles, boxes drawn
// through their transforms, the page painted before anything goes on it,
// its number in its foot, a margin note in the margin -- with no number
// ever written as -0.

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

/// How many times a text says something.
static std::size_t count(const std::string_view text, const std::string_view part) {
    std::size_t found = 0;
    for (std::size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size())) {
        ++found;
    }
    return found;
}

int main() {
    {
        const Result result = article("", "Text.");
        assert((result.clean && holds(result.pdf, "BT /F0 10 Tf 1 0 0 -1 ") && holds(result.pdf, "] TJ ET")) &&
               "text is drawn in runs, each placed and set in its face");
        assert((holds(result.pdf, "1 0 0 -1 0 792 cm")) && "on a page flipped once, so the layout's down is down");
    }
    {
        const Result result = article("", "\\textcolor{red}{red} black \\rule{20pt}{4pt}");
        assert((holds(result.pdf, "1 0 0 rg") && holds(result.pdf, "0 0 0 rg")) &&
               "a color filled, and black again after");
        assert((holds(result.pdf, " 20 4 re f")) && "a rule drawn as a filled rectangle");
    }
    {
        const Result result = article("\\usepackage{graphicx}", "\\rotatebox{90}{up}");
        assert((holds(result.pdf, "q 0 -1 1 0 ") && holds(result.pdf, "Q\n")) && "a box drawn through its transform");
        assert((!holds(result.pdf, " -0 ") && !holds(result.pdf, "q -0")) && "and no number is ever written -0");
    }
    {
        const Result result = article("\\pagecolor{yellow}", "Painted.");
        const std::size_t paint = result.pdf.find("1 1 0 rg");
        const std::size_t text = result.pdf.find("BT ");
        assert((paint != std::string::npos && paint < text) &&
               "a colored page is painted before anything goes on it");
    }
    {
        const Result result = article("", "Page \\thepage.");
        assert((holds(result.pdf, " 704.5 Tm")) && "the page's number in its foot");
    }
    {
        const Result result = article("", "Text.\\marginpar{Aside.} More.");
        // Level with the line it was written in, so drawn in that line's run:
        // a jump right past the column's edge -- over 250 points, as a
        // thousandths-of-an-em kern less than -25000 -- and back.
        bool jumped = false;
        for (std::size_t at = result.pdf.find("> -"); at != std::string::npos; at = result.pdf.find("> -", at + 1)) {
            long amount = 0;
            std::from_chars(result.pdf.data() + at + 2, result.pdf.data() + result.pdf.size(), amount);
            jumped = jumped || amount < -25000;
        }
        assert((result.clean && holds(result.text, "Text.Aside.More.") && jumped) &&
               "a margin note in the right margin");
    }
    {
        const Result result = article("", "Name\\dotfill Value");
        assert((result.clean && count(result.text, ".") > 20) &&
               "leaders drawn as the dots they are, along their glue");
    }
    return 0;
}
