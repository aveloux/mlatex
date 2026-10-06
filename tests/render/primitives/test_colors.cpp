#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Colors primitive: text in color -- by name, by xcolor's mixtures, by
// a model and its numbers -- colors defined and derived, the page painted,
// and a color that cannot be what it says reported.

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
        const Result result = article("", "\\textcolor{red}{red} \\textcolor{#00FF00}{green} "
                                          "\\textcolor{rgb(0,0,255)}{blue}");
        assert((result.clean && holds(result.text, "redgreenblue")) && "colored text");
        assert((holds(result.pdf, "1 0 0 rg") && holds(result.pdf, "0 1 0 rg") && holds(result.pdf, "0 0 1 rg")) &&
               "each filled in its own color");
    }
    {
        const Result result = article("\\definecolor{brand}{HTML}{FF0000}\\colorlet{pale}{brand!50}",
                                      "\\textcolor{brand}{a}\\textcolor{pale}{b}\\textcolor{red!50!blue}{c}"
                                      "\\textcolor[rgb]{0,0.5,0}{d}");
        assert((result.clean) && "named and mixed colors");
        assert((holds(result.pdf, "1 0.5 0.5 rg")) && "half of a color on white is pale");
        assert((holds(result.pdf, "0.5 0 0.5 rg")) && "half red on blue is purple");
        assert((holds(result.pdf, "0 0.5 0 rg")) && "a model and its numbers");
    }
    {
        // Text a command sets -- a sign, an accented letter -- is in the
        // colour in use, as typed text is.
        const Result result = article("", "\\textcolor{red}{\\%}");
        assert((result.clean && holds(result.text, "%") && holds(result.pdf, "1 0 0 rg")) &&
               "a command's text in the colour in use");
    }
    {
        // A colour chosen inside a box's argument stays inside the box: the
        // text after each turns black again before the next colour.
        const Result result = article("\\pagestyle{empty}", "\\mbox{\\color{red}inside} outside "
                                                            "\\textbf{\\color{blue}bold} after");
        const std::size_t red = result.pdf.find("1 0 0 rg");
        const std::size_t blue = result.pdf.find("0 0 1 rg");
        assert((result.clean && red != std::string::npos && blue != std::string::npos) && "each in its colour");
        assert((result.pdf.find("0 0 0 rg", red) < blue && result.pdf.find("0 0 0 rg", blue) != std::string::npos) &&
               "and what follows each box black again");
    }
    {
        const Result result = article("", "{\\color{blue} blue \\normalcolor black} after");
        assert((result.clean && holds(result.pdf, "0 0 1 rg")) && "\\color colors what follows in its group");
    }
    {
        const Result result = article("\\pagecolor{yellow}", "x");
        assert((result.clean && holds(result.pdf, "1 1 0 rg")) && "\\pagecolor paints the page");
    }
    {
        const Result result = typeset("\\documentclass{article}\\definecolor{bad}{rgb}{2,0,0}"
                                      "\\definecolor{odd}{pantone}{123}\\begin{document}\\textcolor{nosuch}{x}"
                                      "\\end{document}");
        assert((!result.clean) && "a color outside its model is an error");
        assert((holds(result.errors, "\\definecolor{bad}")) && "the out-of-range color is named");
        assert((holds(result.errors, "\\definecolor{odd}")) && "the unknown model is named");
        assert((!holds(result.errors, "nosuch")) && "an unknown color name is black, not an error");
    }
    return 0;
}
