#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Sections primitive: every heading level, numbered or starred, short
// titles, the appendix's letters, and the table of contents, the lists of
// figures and tables, each filled with its pages from a second pass.

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
        const Result result = typeset("\\documentclass{report}\\begin{document}\\part{P}\\chapter{C}\\section{S}"
                                      "\\subsection{T}\\subsubsection{U}\\paragraph{V} text\\end{document}");
        assert((result.clean && result.pages.size() == 2 && holds(result.pages[0], "PartIP")) &&
               "a part, its number on a line above its title");
        assert((result.pages.size() == 2 && result.pages[1].starts_with("Chapter1C")) &&
               "a chapter on a new page, `Chapter 1` above its title");
        assert((holds(result.text, "1.1S1.1.1TUVtext")) &&
               "sections numbered within it, down to the report class's depth: a subsubsection unnumbered");
    }
    {
        const Result result = typeset("\\documentclass{book}\\begin{document}\\chapter{Intro}Text.\\appendix"
                                      "\\chapter{Proofs}More.\\end{document}");
        assert((result.clean && result.pages.size() == 2 && result.pages[1].starts_with("AppendixAProofs")) &&
               "after \\appendix, a chapter is an Appendix, lettered");
    }
    {
        const Result result = article("", "\\section*{Unnumbered}\\section[Short]{A much longer title}");
        assert((result.clean && holds(result.text, "Unnumbered1Amuchlongertitle")) &&
               "a starred heading has no number");
    }
    {
        const Result result = article("", "\\section{Body}\\appendix\\section{Proofs}\\subsection{Lemma}");
        assert((result.clean && holds(result.text, "AProofsA.1Lemma")) && "an appendix is lettered");
    }
    {
        const Result result = article("\\usepackage{lipsum}",
                                      "\\tableofcontents\\section{Introduction}\\label{sec:intro}\\lipsum[1-4]"
                                      "\\subsection{Background}\\lipsum[5-8]\\section{Method}\\lipsum[1-9]"
                                      "\\section{End}");
        assert((result.clean && result.pages.size() >= 3) && "a document runs to several pages");
        assert((holds(result.text, "Contents1Introduction11.1Background")) &&
               "the contents list each heading with its page");
        assert((holds(result.text, "2Method2") || holds(result.text, "2Method3")) &&
               "a later heading with its later page");
        assert((!holds(result.text, "??")) && "no page left unresolved");
    }
    {
        const Result result = article("", "\\listoffigures\\listoftables\\begin{figure}[h]\\caption[Short]{A figure}"
                                          "\\end{figure}\\begin{table}[h]\\caption{A table}\\end{table}");
        assert((result.clean && holds(result.text, "ListofFigures1Short.") &&
               holds(result.text, "ListofTables1Atable.")) &&
               "the lists of figures and tables, a short caption in place of the long, dots led to the page");
        assert((holds(result.text, "Figure1:AfigureTable1:Atable")) && "and the long captions on the floats");
    }
    {
        const Result result = article("",
                                      "\\setcounter{tocdepth}{1}\\tableofcontents\\section{Kept}\\subsection{Left}");
        assert((result.clean && holds(result.text, "Contents1Kept11Kept1.1Left")) &&
               "tocdepth leaves deeper headings out of the contents");
    }
    {
        const Result result = article("", "\\tableofcontents\\addcontentsline{toc}{section}{Written in}\\section{A}");
        assert((result.clean && holds(result.text, "Writtenin1")) && "\\addcontentsline adds a line of its own");
    }
    {
        const Result result = article("\\usepackage{titlesec}\\usepackage{xcolor}"
                                      "\\titleformat{\\section}{\\large\\bfseries\\color{blue}}{\\thesection}{1em}{}[\\titlerule]",
                                      "\\section{Blue}Text.");
        assert((result.clean && holds(result.text, "1BlueText.")) && "titlesec's format, its after code read");
        assert((holds(result.pdf, "0 0 1 rg")) && "in its color, the rule under it too");
    }
    {
        const Result result = typeset("\\documentclass{book}\\begin{document}\\frontmatter\\tableofcontents"
                                      "\\chapter{Preface}\\mainmatter\\chapter{One}\\end{document}");
        if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
        assert((result.clean && result.pages.size() == 3 && holds(result.text, "Prefaceii") &&
                holds(result.text, "1One1")) &&
               "a front matter's chapter unnumbered and listed, on a page in roman; the main matter's numbered");
    }

    {
        const Result result = article("\\usepackage{makeidx}\\makeindex",
                                      "Word\\index{zebra}\\index{apple}\\index{fruit!pear}\\newpage "
                                      "More\\index{apple|textbf}\\index{bear|see{zebra}}\\printindex");
        assert((result.clean && holds(result.text, "Index")) && "the index under its heading");
        assert((holds(result.text, "apple,1,2") && holds(result.text, "fruitpear,1") &&
                holds(result.text, "bear,seezebra") && holds(result.text, "zebra,1")) &&
               "sorted as makeindex sorts, a subentry under its entry, its pages after it");
    }
    {
        const Result result = article("\\usepackage{hyperref}", "\\section{One}\\subsection{Two}");
        assert((holds(result.pdf, "/Type /Outlines") && holds(result.pdf, "(1 One)") && holds(result.pdf, "(1.1 Two)")) &&
               "with hyperref, a bookmark for every heading, one under the other");
    }
    {
        const Result result = article("", "\\begin{acks}We thank you.\\end{acks}");
        assert((result.clean && holds(result.text, "AcknowledgmentsWethankyou.")) && "acmart's acknowledgments");
    }

    return 0;
}
