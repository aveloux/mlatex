#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Citations primitive: \cite of one source or several with a note, a
// bibliography written out, and a .bib file read and set in a style --
// numbered, or by author and year as natbib and biblatex cite.

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
    // --- A bibliography written into the document ----------------------------------
    {
        const Result result = article("", "See \\cite{lamport, knuth} and \\cite[p.~5]{knuth}; also \\cite{short}."
                                          "\\begin{thebibliography}{9}"
                                          "\\bibitem{knuth} Donald Knuth. The TeXbook."
                                          "\\bibitem{lamport} Leslie Lamport. LaTeX."
                                          "\\bibitem[Hop51]{short} Eberhard Hopf. Grundgleichungen."
                                          "\\end{thebibliography}");
        assert((result.clean) && "citations typeset without a report");
        assert((holds(result.text, "See[2,1]and[1,p.5]")) && "several keys, and a note");
        assert((holds(result.text, "also[Hop51].")) && "an entry's own label");
        assert((holds(result.text, "References[1]DonaldKnuth")) && "the list under the class's References heading");
    }

    // --- A .bib file, handed in, in a numbered style -------------------------------
    const std::string bib =
        "@article{knuth84, author = {Donald E. Knuth}, title = {Literate Programming},\n"
        "  journal = {The Computer Journal}, year = {1984}, volume = {27}, pages = {97--111}}\n"
        "@book{lamport94, author = {Leslie Lamport}, title = {{LaTeX}: A Document Preparation System},\n"
        "  publisher = {Addison-Wesley}, year = 1994}\n"
        "@misc{unused, title = {Never cited}, year = 2000}\n";
    latex::Host host;
    host.files["refs.bib"] = bib;
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}Programs \\cite{knuth84} and "
                                      "documents \\cite{lamport94}.\\bibliographystyle{plain}\\bibliography{refs}"
                                      "\\end{document}",
                                      host);
        assert((result.clean) && "a .bib file is read without a report");
        assert((holds(result.text, "Programs[1]anddocuments[2].") ||
                holds(result.text, "Programs[2]anddocuments[1].")) &&
               "each cited entry numbered");
        assert((holds(result.text, "DonaldE.Knuth") && holds(result.text, "Literateprogramming")) &&
               "an article's entry, its title in sentence case as the plain style sets it");
        assert((holds(result.text, "Addison-Wesley")) && "a book's publisher");
        assert((!holds(result.text, "Nevercited")) && "an entry never cited is left out");
    }
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}\\nocite{unused}\\cite{knuth84}"
                                      "\\bibliographystyle{plain}\\bibliography{refs}\\end{document}",
                                      host);
        assert((result.clean && holds(result.text, "Nevercited")) && "\\nocite lists an entry without citing it");
    }
    {
        latex::Host sparse;
        sparse.files["sparse.bib"] = "@misc{lovelace, author = {Ada Lovelace}, title = {Notes}, year = 1843}\n"
                                     "@book{lamport94, author = {Leslie Lamport}, title = {LaTeX},\n"
                                     "  publisher = {Addison-Wesley}, edition = {Second}, year = 1994}\n";
        const Result result = typeset("\\documentclass{article}\\begin{document}\\nocite{*}"
                                      "\\bibliography{sparse}\\end{document}",
                                      sparse);
        assert((result.clean && holds(result.text, "Notes.1843.")) &&
               "a misc with no howpublished leaves no comma before its year");
        assert((holds(result.text, "Addison-Wesley,secondedition,1994.")) &&
               "an edition in lower case after the publisher");
    }
    {
        // @preamble's text goes ahead of the list, as BibTeX writes it into
        // the .bbl: here a command the entries use.
        latex::Host preambled;
        preambled.files["macros.bib"] = "@preamble{ \"\\providecommand{\\TeXbook}{The \\TeX book}\" }\n"
                                        "@book{knuth86, author = {Donald E. Knuth}, title = {\\TeXbook},\n"
                                        "  publisher = {Addison-Wesley}, year = 1986}\n";
        const Result result = typeset("\\documentclass{article}\\begin{document}\\cite{knuth86}."
                                      "\\bibliographystyle{plain}\\bibliography{macros}\\end{document}",
                                      preambled);
        if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
        assert((result.clean && holds(result.text, "TheTEXbook")) && "@preamble defines what the entries use");
    }

    // --- By author and year -------------------------------------------------------
    {
        const Result result = typeset("\\documentclass{article}\\usepackage{natbib}\\begin{document}"
                                      "\\citet{knuth84} showed it \\citep{lamport94}.\\bibliographystyle{plainnat}"
                                      "\\bibliography{refs}\\end{document}",
                                      host);
        assert((result.clean) && "natbib reads without a report");
        assert((holds(result.text, "Knuth(1984)showedit(Lamport,1994).")) &&
               "\\citet and \\citep by author and year");
    }
    {
        const Result result = typeset("\\documentclass{article}\\usepackage{biblatex}\\addbibresource{refs.bib}"
                                      "\\begin{document}\\textcite{knuth84} and \\parencite{lamport94}."
                                      "\\printbibliography\\end{document}",
                                      host);
        assert((result.clean && holds(result.text, "Knuth[1]and[2].") && holds(result.text, "Literateprogramming")) &&
               "biblatex's \\addbibresource, \\textcite, \\parencite and \\printbibliography");
    }
    {
        const Result result = typeset("\\documentclass{article}\\usepackage[style=ieee]{biblatex}\\addbibresource{refs.bib}"
                                      "\\begin{document}\\cite{knuth84}.\\printbibliography[title={Sources}]\\end{document}",
                                      host);
        assert((result.clean && holds(result.text, "[1].") && holds(result.text, "Sources") &&
                !holds(result.text, "References")) &&
               "biblatex's IEEE style, and \\printbibliography's own title");
    }
    {
        const Result result = typeset("\\documentclass{report}\\usepackage{hyperref}\\begin{document}\\cite{knuth84}."
                                      "\\bibliographystyle{plain}\\bibliography{refs}\\end{document}",
                                      host);
        assert((result.clean && holds(result.text, "Bibliography")) && "a report's references a chapter, Bibliography");
        assert((holds(result.pdf, "/Subtype /Link") && holds(result.pdf, "/C [0 1 0 ]")) &&
               "with hyperref, a citation goes to its entry, framed green");
    }
    {
        // What BibTeX wrote beside the document, carried in place of the
        // .bib: the \jobname's .bbl, which for a document handed in from
        // memory is document.bbl.
        latex::Host carried;
        carried.files["document.bbl"] = "\\begin{thebibliography}{1}\\providecommand{\\natexlab}[1]{#1}\n"
                                        "\\bibitem[Knuth(1984)]{knuth}Donald~E. Knuth.\n\\newblock \\emph{The TeXbook}.\n"
                                        "\\end{thebibliography}\n";
        const Result result = typeset("\\documentclass{article}\\usepackage{natbib}\\begin{document}\\citet{knuth}."
                                      "\\bibliographystyle{plainnat}\\bibliography{refs}\\end{document}",
                                      carried);
        assert((result.clean && holds(result.text, "Knuth(1984).") && holds(result.text, "TheTeXbook")) &&
               "a bibliography already made is set when the .bib it names is not there");
    }

    // --- Mistakes -------------------------------------------------------------------
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}\\cite{nosuch}"
                                      "\\bibliographystyle{plain}\\bibliography{missing}\\end{document}");
        assert((!result.clean && holds(result.errors, "missing")) && "a .bib file that is not there is named");
    }

    return 0;
}
