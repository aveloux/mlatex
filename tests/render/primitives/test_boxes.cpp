#include "engine.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Boxes primitive: material set as one unit -- \mbox, \makebox and
// \framebox to a width and a side, \fbox and \colorbox, \raisebox,
// \phantom, minipages and \parbox -- TeX's box registers with \setbox,
// \copy, \box and \wd, LaTeX's saved boxes, graphicx's transforms, and the
// framed blocks of framed, mdframed and tcolorbox.

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
    // --- Boxes and saved boxes -----------------------------------------------------
    {
        const Result result = article(
            "\\usepackage{xcolor}\\newsavebox{\\stash}\\newlength{\\gap}",
            "\\mbox{kept} \\fbox{framed} \\framebox[3cm][r]{right} \\colorbox{yellow}{behind} "
            "\\fcolorbox{red}{white}{both} \\raisebox{1ex}{raised} x\\makebox[0pt][l]{over}y "
            "[\\phantom{hidden}] \\sbox{\\stash}{saved}\\usebox{\\stash}\\usebox{\\stash} "
            "\\settowidth{\\gap}{MMMM}\\rule{\\gap}{1pt}\\rule{0.5\\linewidth}{1pt}\\par"
            "\\noindent\\begin{minipage}[t]{0.45\\textwidth}Left column.\\end{minipage}\\hfill"
            "\\begin{minipage}[t]{0.45\\textwidth}Right column.\\end{minipage}\\par"
            "\\parbox{4cm}{A parbox.} Name\\dotfill Value \\hrulefill");
        assert((result.clean) && "the boxes read without a report");
        assert((holds(result.text, "keptframedrightbehindbothraisedxovery[]savedsaved")) &&
               "every box keeps its contents, a phantom sets none, a saved box twice");
        assert((holds(result.text, "Leftcolumn.Rightcolumn.")) && "two minipages side by side");
        assert((holds(result.text, "Aparbox.Name.....") && holds(result.text, "....Value")) &&
               "a parbox, and dots led across to the value as LaTeX's are");
        assert((holds(result.pdf, "1 1 0 rg")) && "a color box is painted behind its text");
    }

    // --- A box narrower than its contents overlaps what follows --------------------------
    {
        const Result result = article("", "a\\rlap{xyz}b");
        // Three letters of ten points' worth back: the b drawn over the x.
        assert((result.clean && holds(result.pdf, "<0004> 1") && holds(result.text, "axyzb")) &&
               "\\rlap sets its contents over what follows, which starts where they did");
    }

    // --- TeX's box registers --------------------------------------------------------
    {
        const Result result = article("", "\\setbox3=\\hbox{boxed}[\\copy3][\\box3][\\box3] "
                                          "\\setbox0\\hbox{measure}\\the\\wd0.");
        assert((result.clean) && "box registers read without a report");
        assert((holds(result.text, "[boxed][boxed][]")) && "\\copy keeps the box, \\box empties the register");
        assert((holds(result.text, "pt.")) && "\\wd reads its width as a length");
    }

    // --- graphicx's transforms ------------------------------------------------------
    {
        const Result result = article("\\usepackage{graphicx}",
                                      "\\rotatebox{90}{up} \\scalebox{2}{big} \\resizebox{2cm}{!}{wide} "
                                      "\\reflectbox{mirror}");
        assert((result.clean && holds(result.text, "upbigwidemirror")) && "each transform keeps its text");
        assert((holds(result.pdf, " cm\n")) && "and draws it through a matrix");
        assert((holds(result.pdf, "0 -1 1 0 ") || holds(result.pdf, "0 1 -1 0 ")) &&
               "a quarter turn is a quarter turn");
    }

    // --- Framed blocks --------------------------------------------------------------
    {
        const Result result = article("\\usepackage{framed,tcolorbox}",
                                      "\\begin{framed}In a frame.\\end{framed}"
                                      "\\begin{tcolorbox}[title=Note]In a colored box.\\end{tcolorbox}");
        assert((result.clean && holds(result.text, "Inaframe.") && holds(result.text, "NoteInacoloredbox.")) &&
               "a framed block and a titled colored box keep their text");
    }

    // --- Measuring ------------------------------------------------------------------
    {
        const Result result = article("\\newlength{\\w}", "\\settowidth{\\w}{}\\the\\w{} "
                                                          "\\setlength{\\w}{\\widthof{MM}}\\ifdim\\w>0pt wide\\fi");
        assert((result.clean && holds(result.text, "0.0pt") && holds(result.text, "wide")) &&
               "nothing is no width, and \\widthof measures");
    }

    // beamer's blocks, their titles in a bar over the body, and its
    // columns side by side -- as blocks and as \column.
    {
        const Result result = article("", "\\begin{block}{Title}Body.\\end{block}"
                                          "\\begin{alertblock}<2->{Alert}Careful.\\end{alertblock}");
        assert((result.clean && holds(result.text, "TitleBody.AlertCareful.")) && "a block's title, then its body");
        assert((holds(result.pdf, "0.2 0.2 0.7 rg") || holds(result.pdf, "0 0 0 rg")) && "in a bar of the block's color");
    }
    {
        const Result result = article("", "\\begin{columns}\\column{0.45\\textwidth}Left.\\column{0.45\\textwidth}"
                                          "Right.\\end{columns}After.");
        assert((result.clean && holds(result.text, "Left.Right.After.")) && "\\column's columns, then the text after");
        assert((result.pages.size() == 1) && "on one line");
    }
    {
        const Result result = article("", "\\begin{columns}[t]\\begin{column}{0.4\\textwidth}One.\\end{column}"
                                          "\\begin{column}{0.4\\textwidth}Two.\\end{column}\\end{columns}");
        assert((result.clean && holds(result.text, "One.Two.")) && "column blocks, hung by their tops");
    }

    {
        const Result result = article("", "\\setbox0\\hbox{a}\\ifvoid0 empty\\else full\\fi\\ \\ifvoid1 none\\fi");
        assert((result.clean && holds(result.text, "fullnone")) && "\\ifvoid asks after a box register");
    }
    {
        const Result result = article("\\usepackage{tcolorbox}\\tcbset{colframe=red}"
                                      "\\newtcolorbox{note}[1]{title=#1, fonttitle=\\bfseries}",
                                      "\\begin{note}{Heads}Body.\\end{note}");
        assert((result.clean && holds(result.text, "HeadsBody.")) && "a box \\newtcolorbox names, its title its argument");
        assert((holds(result.pdf, "1 0 0 rg")) && "in \\tcbset's colors");
    }

    return 0;
}
