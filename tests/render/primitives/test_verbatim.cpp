#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Verbatim primitive: text set exactly as typed -- \verb, the verbatim
// block, listings with their captions and line numbers, minted's block and
// \mintinline -- whatever it holds, and the comment block, which sets
// nothing.

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
        const Result result = article("\\usepackage{listings}\\usepackage{verbatim}\\lstset{numbers=left}",
                                      "Call \\verb|f(x_1, $y$) % no| and \\lstinline{g()}."
                                      "\\begin{verbatim}\n  a_b = \\c;   % kept\n\\end{verbatim}"
                                      "\\begin{lstlisting}[caption={A loop}, label=lst:loop, frame=single]\n"
                                      "for x in y:\n\tpass\n\\end{lstlisting}"
                                      "See Listing~\\ref{lst:loop}."
                                      "\\begin{comment}\nNever \\undefined.\n\\end{comment}Done.");
        assert((result.clean) && "verbatim text reads without a report, whatever it holds");
        assert((holds(result.text, "f(x_1,$y$)%no")) && "\\verb keeps every character");
        assert((holds(result.text, "g()")) && "\\lstinline too");
        assert((holds(result.text, "a_b=\\c;%kept")) && "a verbatim block keeps its commands and its comment");
        assert((holds(result.text, "Listing1:Aloop1forxiny:2pass")) &&
               "a listing captioned above, its lines numbered");
        assert((holds(result.text, "SeeListing1.")) && "and referred to by its label");
        assert((!holds(result.text, "Never") && holds(result.text, "Done.")) && "a comment block sets nothing");
    }
    {
        const Result result = article("\\usepackage{minted}",
                                      "\\begin{minted}{python}\ndef f(x): return x % 2\n\\end{minted}"
                                      "Inline \\mintinline{c}{int *p = &x;} code.");
        assert((result.clean && holds(result.text, "deff(x):returnx%2")) &&
               "minted's block, the language read and let go");
        assert((holds(result.text, "Inlineint*p=&x;code.")) && "and \\mintinline");
        assert((holds(result.pdf, "NewCMMono10") || holds(result.pdf, "Mono")) && "in the typewriter face");
    }
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}\\verb\\end{document}");
        assert((!result.clean) && "\\verb with nothing to delimit is reported");
    }
    {
        const Result result = typeset(
            "\\begin{filecontents*}[overwrite]{inline.bib}\n"
            "@book{knuth, author={Donald E. Knuth}, title={The TeXbook}, publisher={Addison-Wesley}, year={1984}}\n"
            "\\end{filecontents*}\n"
            "\\documentclass{article}\n\\begin{filecontents}{chapter.tex}\nFrom the chapter.\n\\end{filecontents}\n"
            "\\begin{document}Before. \\input{chapter} As \\cite{knuth}."
            "\\bibliographystyle{plain}\\bibliography{inline}\\end{document}");
        assert((result.clean && holds(result.text, "Before.Fromthechapter.As[1].")) &&
               "a file a document carries is kept, set nowhere, and read where it is named");
        assert((holds(result.text, "DonaldE.Knuth.TheTeXbook.")) && "a .bib among them");
    }
    {
        const Result result = article("", "a\\begin{CCSXML}<ccs2012><concept>x</concept></ccs2012>\\end{CCSXML}b");
        assert((result.clean && holds(result.text, "ab") && !holds(result.text, "ccs")) &&
               "acmart's CCSXML, read and let go");
    }

    return 0;
}
