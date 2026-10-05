#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Floats primitive: figures and tables captioned, numbered and listed,
// moved whole to a later page when they do not fit and never split, [H]
// set where written, and a figure made of lettered subfigures.

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

/// @brief Enough plain text to fill most of a page, so what comes after it
///        has to decide where it goes.
static std::string filler(const int paragraphs) {
    std::string text;
    for (int index = 0; index < paragraphs; ++index) {
        text += "Words that fill the page and say nothing in particular, set again and again so that "
                "the column runs down towards its foot before anything else arrives.\n\n";
    }
    return text;
}

int main() {
    {
        const Result result = article("", "\\begin{figure}[h]\\centering\\rule{2cm}{1cm}\\caption{A square.}"
                                          "\\label{f}\\end{figure}\\begin{table}[h]\\caption{A table.}\\end{table}"
                                          "See \\ref{f}.");
        assert((result.clean && holds(result.text, "Figure1:Asquare.") && holds(result.text, "Table1:Atable.")) &&
               "figures and tables are captioned and numbered apart");
        assert((holds(result.text, "See1.")) && "and referred to by their labels");
    }
    {
        const Result result = article("", filler(15) + "\\begin{figure}[htbp]\\centering\\rule{100pt}{300pt}"
                                                       "\\caption{Held back.}\\end{figure}\n\nCarried on beneath.");
        assert((result.clean && result.pages.size() == 2) && "a float that does not fit makes a second page");
        if (result.pages.size() == 2) {
            assert((holds(result.pages[0], "Carriedonbeneath.") && !holds(result.pages[0], "Figure1")) &&
                   "the text after it fills the page it left");
            assert((holds(result.pages[1], "Figure1:Heldback.")) && "and it heads the next page, whole");
        }
    }
    {
        const Result result = article("", filler(15) + "\\begin{figure}[H]\\centering\\rule{100pt}{300pt}"
                                                       "\\caption{Kept.}\\end{figure}\n\nAfter it.");
        assert((result.clean && result.pages.size() == 2 && !holds(result.pages[0], "Afterit.") &&
               holds(result.pages[1], "Figure1:Kept.Afterit.")) &&
               "[H] is set where written, on the next page, the text after it after it");
    }
    {
        const Result result = article(
            "\\usepackage{subcaption}",
            "\\begin{figure}[h]\\centering"
            "\\begin{subfigure}[b]{0.4\\textwidth}\\centering\\rule{2cm}{1cm}\\caption{Left}\\label{fig:a}"
            "\\end{subfigure}"
            "\\hfill"
            "\\begin{subfigure}[b]{0.4\\textwidth}\\centering\\rule{2cm}{1cm}\\caption{Right}\\label{fig:b}"
            "\\end{subfigure}"
            "\\caption{Both}\\label{fig:both}\\end{figure}"
            "See \\ref{fig:a}, \\ref{fig:b} and \\ref{fig:both}.");
        assert((result.clean && holds(result.text, "(a)Left(b)RightFigure1:Both")) &&
               "subfigures lettered, the figure after");
        assert((holds(result.text, "See1a,1band1.")) &&
               "a subfigure's reference is the figure's number and its letter");
    }
    {
        const Result result = article("\\usepackage{wrapfig,rotating}",
                                      "\\begin{wrapfigure}{r}{3cm}\\rule{2cm}{1cm}\\caption{Wrapped.}\\end{wrapfigure}"
                                      "\\begin{sidewaystable}\\caption{Turned.}\\end{sidewaystable}");
        assert((result.clean && holds(result.text, "Figure1:Wrapped.") && holds(result.text, "Table1:Turned.")) &&
               "wrapped and sideways floats are floats like any other");
    }
    {
        const Result result = article("", "\\begin{teaserfigure}\\caption{Teaser}\\end{teaserfigure}");
        assert((result.clean && holds(result.text, "Figure1:Teaser")) && "acmart's teaser, a figure");
    }

    return 0;
}
