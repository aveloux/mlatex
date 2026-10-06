#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Page primitive: the class and its options, the sheet and margins
// geometry sets, the page's number and style -- plain, empty, headings and
// fancyhdr's heads and feet -- and the columns: [twocolumn], \twocolumn,
// \onecolumn and multicol's block.

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

/// @brief Enough text to run down more than one column.
static std::string filler(const int paragraphs) {
    std::string text;
    for (int index = 0; index < paragraphs; ++index) {
        text += "Words that fill the column and say nothing in particular, set again and again so that "
                "the column runs down towards its foot before anything else arrives. ";
        text += "Paragraph " + std::to_string(index) + ".\n\n";
    }
    return text;
}

int main() {
    // --- Classes and the sheet ------------------------------------------------------------
    {
        const Result classed = typeset("\\documentclass[a4paper,11pt]{article}\\begin{document}x\\end{document}");
        assert((classed.clean && holds(classed.pdf, "/MediaBox [0 0 595 842]")) && "the class's a4paper option");
        const Result turned = typeset("\\documentclass[a4paper,landscape]{article}\\begin{document}x\\end{document}");
        assert((holds(turned.pdf, "/MediaBox [0 0 842 595]")) && "landscape turns the sheet");
        const Result report = typeset("\\documentclass[letterpaper]{report}\\begin{document}x\\end{document}");
        assert((report.clean && holds(report.pdf, "/MediaBox [0 0 612 792]")) && "the report class, on letter paper");
        const Result book = typeset("\\documentclass{book}\\begin{document}x\\end{document}");
        assert((book.clean) && "the book class");
        const Result measured = typeset("\\documentclass{article}\\usepackage[a5paper,margin=1cm]{geometry}"
                                        "\\begin{document}x\\end{document}");
        assert((measured.clean && holds(measured.pdf, "/MediaBox [0 0 420 595]")) && "geometry's paper option");
        const Result called = typeset("\\documentclass{article}\\usepackage{geometry}"
                                      "\\geometry{paperwidth=300pt,paperheight=400pt}\\begin{document}x"
                                      "\\end{document}");
        assert((holds(called.pdf, "/MediaBox [0 0 300 400]")) && "\\geometry sets the sheet from its keys");
        const Result unknown = typeset("\\documentclass{nosuch}\\begin{document}x\\end{document}");
        assert((unknown.clean && holds(unknown.errors, "warning: File `nosuch.cls' not found")) &&
               "an unknown class is a warning that names it, and the document is set as article");
        const Result ieee = typeset("\\documentclass[conference]{IEEEtran}\\begin{document}\\section{One}"
                                    "\\the\\columnwidth\\end{document}");
        assert((ieee.clean && ieee.errors.empty() && holds(ieee.text, "I.One")) &&
               "a class the engine carries a module for reads it: IEEEtran numbers sections in Roman, `I.`");
        const Result revtex = typeset("\\documentclass[aps,reprint]{revtex4-2}\\author{A}\\affiliation{Here}"
                                      "\\begin{document}\\maketitle x\\end{document}");
        assert((revtex.clean && holds(revtex.text, "Here")) && "revtex's affiliations are set under the authors");
        const Result width = article("", "\\the\\textwidth");
        assert((holds(width.text, "345.0pt")) && "the article class's text width at ten points");
    }

    // --- The page's number, and its style ------------------------------------------------
    {
        const Result plain = article("", "First.\\newpage Second.");
        assert((plain.clean && plain.pages.size() == 2 && plain.pages[0].ends_with("1") &&
               plain.pages[1].ends_with("2")) &&
               "each page carries its own number");
        assert((holds(plain.pdf, " 704.5 Tm")) && "in the foot, 30pt below the article class's text block");

        const Result empty = article("\\pagestyle{empty}", "First.");
        assert((empty.clean && empty.text == "First.") && "an empty page carries nothing");
        const Result title = article("", "Title.\\thispagestyle{empty}\\newpage Body.");
        assert((title.pages.size() == 2 && title.pages[0] == "Title." && title.pages[1] == "Body.2") &&
               "\\thispagestyle holds for its own page alone");
        const Result roman = article("",
                                     "\\pagenumbering{roman}One.\\newpage Two.\\newpage\\pagenumbering{arabic}Three.");
        assert((roman.pages.size() == 3 && roman.pages[0].ends_with("i") && roman.pages[1].ends_with("ii") &&
               roman.pages[2].ends_with("1")) && "\\pagenumbering changes the style and counts from one again");
        const Result running = article("", "This is page \\thepage.");
        assert((running.clean && running.text.starts_with("Thisispage1.")) && "\\thepage in the text");
        const Result wrong = article("\\pagestyle{nosuch}", "x");
        assert((holds(wrong.errors, "Unknown page style 'nosuch'")) && "an unknown style is named");
    }
    {
        const Result fancy = article("\\usepackage{fancyhdr}\\pagestyle{fancy}\\fancyhead[L]{Acme}"
                                     "\\fancyhead[R]{Report}"
                                     "\\fancyfoot[C]{Page \\thepage}",
                                     "Body.");
        assert((fancy.clean && holds(fancy.text, "Acme") && holds(fancy.text, "Report") &&
               holds(fancy.text, "Page1")) &&
               "a fancy page carries its head and its foot, number and all");
        assert((holds(fancy.pdf, " 99.5 Tm")) && "the head 25pt above the text block");
        assert((holds(fancy.pdf, "0.4 re f")) && "with fancyhdr's rule under it");
        const Result ruled = article("\\usepackage{fancyhdr}\\pagestyle{fancy}\\renewcommand{\\headrulewidth}{0pt}",
                                     "Body.");
        assert((!holds(ruled.pdf, "0.4 re f")) && "a rule set to nothing is not drawn");
        const Result old = article("\\usepackage{fancyhdr}\\pagestyle{fancy}\\lhead{Left}\\rfoot{Right}", "x");
        assert((old.clean && holds(old.text, "Left") && holds(old.text, "Right")) && "fancyhdr's first spelling");
    }

    // --- Columns -------------------------------------------------------------------------
    {
        const Result result = typeset("\\documentclass[twocolumn]{article}\\title{Across}\\author{A}"
                                      "\\begin{document}\\maketitle " + filler(12) + "\\end{document}");
        assert((result.clean) && "a two-column paper reads without a report");
        assert((holds(result.text, "Across")) && "its title");
        // The second column of the first page starts where the first's does,
        // beside it: the same height on the page, far to the right.
        assert((holds(result.pdf, " 0 0 -1 3") || holds(result.pdf, " 0 0 -1 4")) &&
               "text set in the right half of the page");
        const Result width = typeset("\\documentclass[twocolumn]{article}\\begin{document}\\the\\columnwidth{} "
                                     "\\the\\textwidth\\end{document}");
        assert((holds(width.text, "200.0pt410.0pt")) &&
               "the class's column width and text width, as size10.clo has them");
    }
    {
        const Result result = article("\\usepackage{multicol}",
                                      "Before.\\begin{multicols}{2}[\\section{Heading}]" + filler(3) +
                                          "\\end{multicols}After, across the page.");
        assert((result.clean && holds(result.text, "Before.1Heading") && holds(result.text, "After,acrossthepage.")) &&
               "a multicols block, its heading across it, the text after across the page again");
        assert((result.pages.size() == 1) && "balanced, so it leaves the rest of the page for what follows");
    }
    {
        const Result result = article("",
                                      "One column.\\twocolumn[\\section{Spanning}]Two columns.\\onecolumn One again.");
        assert((result.clean && result.pages.size() == 3) && "\\twocolumn and \\onecolumn each start a new page");
        assert((result.pages.size() == 3 && holds(result.pages[1], "1SpanningTwocolumns.")) &&
               "\\twocolumn's heading set across the columns under it");
    }
    {
        const Result result = article("", "One.\\setcounter{page}{7}\\newpage Two.");
        assert((result.clean && result.pages.size() == 2 && holds(result.pages[0], "One.7") &&
                holds(result.pages[1], "Two.8")) &&
               "\\setcounter{page} numbers the page it lands on, and the next counts on");
    }

    // beamer's frames: a page for each slide, its title at its head, with no
    // page numbers and no page left empty between them; what a slide does
    // not show is not on it.
    {
        const Result result = typeset("\\documentclass{beamer}\\title{Talk}\\author{Me}\\begin{document}"
                                      "\\frame{\\titlepage}"
                                      "\\section{Start}"
                                      "\\begin{frame}{Motivation}\\begin{itemize}\\item<1-> First\\item<2-> Second"
                                      "\\end{itemize}\\pause After.\\end{frame}"
                                      "\\begin{frame}[t]\\frametitle{Code}\\only<2>{Shown.}\\end{frame}"
                                      "\\end{document}");
        if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
        assert((result.clean && result.pages.size() == 5) && "a page for each slide, none between");
        assert((result.pages.size() == 5 && holds(result.pages[0], "TalkMe") && holds(result.pages[1], "Motivation") &&
                holds(result.pages[1], "First") && !holds(result.pages[1], "Second") && !holds(result.pages[1], "After") &&
                holds(result.pages[2], "SecondAfter.")) &&
               "the title page, then a frame's slides: an item and a pause's text from the second");
        assert((result.pages.size() == 5 && result.pages[3] == "Code" && result.pages[4] == "CodeShown.") &&
               "and \\only's text on its slide alone");
    }
    {
        // Steps from a list's default overlay, \uncover, \alt and an
        // environment of beamer's own, each on its slides.
        const Result result = typeset("\\documentclass{beamer}\\begin{document}"
                                      "\\begin{frame}\\begin{itemize}[<+->]\\item A\\item B\\end{itemize}"
                                      "\\uncover<2>{U}\\alt<1>{X}{Y}\\begin{onlyenv}<2>Z\\end{onlyenv}\\end{frame}"
                                      "\\end{document}");
        if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
        assert((result.clean && result.pages.size() == 2) && "two steps, two slides");
        assert((result.pages.size() == 2 && holds(result.pages[0], "A") && !holds(result.pages[0], "B") &&
                holds(result.pages[0], "X") && !holds(result.pages[0], "U") && !holds(result.pages[0], "Z")) &&
               "the first: its first item, \\alt's first text");
        assert((result.pages.size() == 2 && holds(result.pages[1], "B") && holds(result.pages[1], "UY") &&
                holds(result.pages[1], "Z")) &&
               "the second: every item, what \\uncover holds, \\alt's other text, the environment's");
    }

    // The letter class: the addresses, the date, the salutation, the
    // closing and the signature, on a page of its own.
    {
        const Result result = typeset("\\documentclass{letter}\\address{Here}\\signature{Me}\\date{Today}"
                                      "\\begin{document}\\begin{letter}{You}\\opening{Dear you,}Body."
                                      "\\closing{Yours,}\\encl{A copy}\\end{letter}\\end{document}");
        if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
        assert((result.clean && result.pages.size() == 1 &&
                holds(result.text, "HereTodayYouDearyou,Body.Yours,Meencl:Acopy")) &&
               "a letter, in the order the class sets it");
    }

    {
        const Result result = article("", "\\begin{titlepage}A Title\\end{titlepage}Body.");
        assert((result.clean && result.pages.size() == 2 && result.pages[0] == "ATitle" &&
                holds(result.pages[1], "Body.1")) &&
               "a title page of its own, unnumbered, and the pages after counted from one");
    }

    return 0;
}
