#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Lists primitive: itemize, enumerate and description, nested, with
// labels of their own, enumitem's keys, and paralist's compact lists.

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
        const Result result = article("", "\\begin{itemize}\\item a \\begin{itemize}\\item b\\end{itemize}"
                                      "\\end{itemize}"
                                          "\\begin{enumerate}\\item one \\begin{enumerate}\\item two\\end{enumerate}"
                                          "\\item three\\end{enumerate}"
                                          "\\begin{description}\\item[term] meaning\\end{description}");
        assert((result.clean) && "every kind of list, nested");
        assert((holds(result.text, "\xE2\x80\xA2" "a")) && "a bullet before each item");
        assert((holds(result.text, "1.one(a)two2.three")) && "numbered, nested ones lettered in parentheses");
        assert((holds(result.text, "termmeaning")) && "a description's term before its meaning");
    }
    {
        const Result result = article("", "\\begin{itemize}\\item[--] dashed \\item[] bare\\end{itemize}");
        assert((result.clean && holds(result.text, "\xE2\x80\x93" "dashedbare")) && "an item's own label, or none");
    }
    {
        const Result result = article("\\usepackage{enumitem}",
                                      "\\begin{enumerate}[label=(\\roman*)]\\item x \\item y\\end{enumerate}"
                                      "\\begin{enumerate}[start=5,noitemsep]\\item five\\end{enumerate}");
        assert((result.clean && holds(result.text, "(i)x(ii)y")) && "enumitem's label key");
        assert((holds(result.text, "5.five")) && "and start");
    }
    {
        const Result result = article("\\usepackage{paralist}",
                                      "\\begin{compactenum}\\item a \\item b\\end{compactenum}"
                                      "\\begin{compactitem}\\item c\\end{compactitem}");
        assert((result.clean && holds(result.text, "1.a2.b") && holds(result.text, "c")) &&
               "paralist's compact lists");
    }
    {
        const Result result = article("\\newcounter{step}",
                                      "\\begin{list}{--}{\\setlength{\\leftmargin}{2em}\\setlength{\\itemsep}{0pt}}"
                                      "\\item one \\item two\\end{list}"
                                      "\\begin{list}{Step \\arabic{step}:}{\\usecounter{step}}"
                                      "\\item first \\item second\\end{list}\\the\\textwidth");
        assert((result.clean && holds(result.text, "\xE2\x80\x93" "one" "\xE2\x80\x93" "two")) &&
               "LaTeX's own list, its items carrying the label given");
        assert((holds(result.text, "Step1:firstStep2:second")) && "one that counts its items with \\usecounter");
        assert((holds(result.text, "345.0pt")) && "its \\leftmargin is the list's, not the page's");
    }
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}\\begin{itemize}\\item a"
                                      "\\end{document}");
        assert((!result.clean && holds(result.errors, "ended by \\end{document}")) && "a list left open is reported");
    }
    {
        const Result result = article("", "\\begin{itemize}\\item<1-> One\\item<2-> Two\\end{itemize}");
        assert((result.clean && holds(result.text, "One") && !holds(result.text, "<")) &&
               "beamer's overlays on an item, read and let go");
    }
    {
        const Result result = article("\\renewcommand{\\labelitemi}{$\\star$}",
                                      "\\begin{itemize}\\item One\\end{itemize}");
        assert((result.clean && holds(result.text, "\xE2\x8B\x86One")) && "a document's own \\labelitemi");
    }
    {
        const Result result = article("\\usepackage{enumitem}\\setlist[enumerate]{label=(\\alph*)}",
                                      "\\begin{enumerate}\\item One\\item Two\\end{enumerate}");
        assert((result.clean && holds(result.text, "(a)One(b)Two")) && "\\setlist's options for every list of a kind");
    }

    return 0;
}
