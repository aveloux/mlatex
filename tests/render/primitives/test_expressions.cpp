#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Expressions primitive: formulas in the text and on lines of their
// own -- inline, displayed, numbered, aligned -- with their fences,
// matrices, accents and alphabets, and a paragraph that survives a formula
// too wide for its line.

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
        const Result result = article("", "$x^2 + y_1 = \\frac{a}{b}$ and $\\sqrt{x} + \\sqrt[3]{y}$ and \\(z\\).");
        assert((result.clean && holds(result.text, "x2+y1=") && holds(result.text, "and")) && "inline formulas");
    }
    {
        const Result result = article("", "\\[ \\sum_{i=1}^{n} i = \\frac{n(n+1)}{2} \\]"
                                          "\\begin{equation} E = mc^2 \\label{e}\\end{equation}"
                                          "\\begin{equation*} a \\end{equation*} See (\\ref{e}).");
        assert((result.clean && holds(result.text, "E=mc2(1)") && holds(result.text, "See(1).")) &&
               "displayed formulas, a numbered one referred to");
    }
    {
        const Result result = article("\\usepackage{amsmath}",
                                      "\\begin{align} a &= b \\\\ c &= d \\notag \\\\ e &= f \\end{align}"
                                      "\\begin{get*} g \\end{get*}");
        assert((result.clean && holds(result.text, "a=b(1)") && holds(result.text, "e=f(2)") &&
               !holds(result.text, "(3)")) && "an align numbers each row, but one marked \\notag");
    }
    {
        const Result result = article("", "\\[ \\left( \\frac{a}{b} \\right) \\left[ x \\right] \\left\\{ y \\right\\} "
                                          "\\pmatrix{a & b \\\\ c & d} \\cases{a & b \\\\ c & d} \\]"
                                          "$\\hat{a} \\bar{b} \\vec{c} \\overline{fg} \\mathbb{R} \\mathcal{C} "
                                          "\\alpha \\le \\infty$");
        assert((result.clean && holds(result.text, "(ab)[x]{y}")) && "fences of every kind, a fraction inside one");
        assert((holds(result.text, "(abcd){abcd")) && "a matrix in its parentheses, and cases behind a brace");
        assert((holds(result.text, "fgRC\xCE\xB1\xE2\x89\xA4\xE2\x88\x9E")) &&
               "accents over their letters, the alphabets, and the symbols by name");
    }
    {
        const Result result = article("", "$a\\,b\\;c\\!{}_{2}\\quad d$");
        assert((result.clean) && "a space in a formula takes no argument, even before braces");
    }
    {
        std::string letters(120, 'x');
        const Result result = article("", "Before the formula $" + letters + "$ and after it, the words go on.");
        assert((result.clean) && "an overfull line is not an error");
        assert((holds(result.text, "Beforetheformula") && holds(result.text, "andafterit,thewordsgoon.")) &&
               "the words either side of a formula wider than the column are kept");
    }
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}$\\frac{a}$\\end{document}");
        assert((!result.clean) && "a fraction short of its denominator is reported");
    }
    {
        const Result result = article("\\usepackage{xcolor}", "\\textcolor{red}{$x$}");
        assert((result.clean && holds(result.pdf, "1 0 0 rg")) && "a formula in colored text takes its color");
    }
    {
        const Result result = article("", "{\\tiny $x$} and {\\Huge $x$}");
        assert((result.clean && holds(result.pdf, " 5 Tf") && holds(result.pdf, " 24.88 Tf")) &&
               "a formula set at the size of the text around it");
    }

    {
        const Result result = article("", "\\catcode`\\$=12 It costs $5.");
        assert((result.clean && holds(result.text, "Itcosts$5.")) && "a dollar made ordinary is a dollar");
    }

    return 0;
}
