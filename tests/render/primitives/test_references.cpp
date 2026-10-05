#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The References primitive: \label and what refers to it -- \ref,
// \eqref, \pageref, \nameref -- backwards and forwards, and cleveref's and
// hyperref's references that say what they refer to.

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
        const Result result = article("", "\\section{First}\\label{first} See \\ref{first} and \\ref{later}."
                                          "\\section{Later}\\label{later}");
        assert((result.clean && holds(result.text, "See1and2.")) && "references, backwards and forwards");
    }
    {
        const Result result = article("", "\\begin{equation}x\\label{e}\\end{equation}\\eqref{e} "
                                          "\\section{Named}\\label{s}\\nameref{s} on page \\pageref{s}. "
                                          "\\ref{nosuch}");
        assert((holds(result.text, "(1)") && holds(result.text, "Namedonpage1.")) &&
               "\\eqref, \\nameref and \\pageref");
        assert((holds(result.text, "??")) && "a label never made reads ??");
    }
    {
        const Result result = article("\\usepackage{cleveref}",
                                      "See \\cref{s}, \\Cref{e} and \\cref{later}.\\section{Method}\\label{s}"
                                      "\\begin{equation}E=mc^2\\label{e}\\end{equation}"
                                      "\\begin{itemize}\\item a\\end{itemize}\\section{Later}\\label{later}");
        assert((result.clean && holds(result.text, "Seesection1,Equation(1)andsection2.")) &&
               "each named by cleveref's own names, forward ones included");
        const Result named = article("\\usepackage{cleveref}\\crefname{section}{sec.}{secs.}",
                                     "\\section{A}\\label{a}\\cref{a}");
        assert((holds(named.text, "sec.1")) && "\\crefname renames a kind");
    }
    {
        const Result plain = article("", "\\section{A}\\label{a}\\cref{a} \\Cref{a}");
        assert((holds(plain.text, "section1Section1")) && "without the package, a kind is named by itself");
        const Result autoref = article("\\usepackage{hyperref}", "\\section{A}\\label{a}\\autoref{a}");
        assert((holds(autoref.text, "Section1")) && "hyperref's \\autoref names it too");
    }
    {
        const Result twice = typeset("\\documentclass{article}\\begin{document}\\label{a}\\label{a}\\end{document}");
        assert((holds(twice.errors, "Label `a' multiply defined")) && "a label made twice is reported");
    }
    {
        const Result result = article("", "Before: \\@forward{key}. \\@fulfil{key}{given later}After: \\@forward{key}.");
        assert((result.clean && holds(result.text, "Before:givenlater.After:givenlater.")) &&
               "a text given further on than it is used, written in both places");
    }
    {
        const Result result = article("", "\\@forward{never}.");
        assert((holds(result.text, "??.")) && "one never given reads ??");
    }

    {
        const Result result = article("\\usepackage{hyperref}",
                                      "\\section{One}\\label{one}See \\ref{one} and \\href{https://example.org}{a site}.");
        assert((result.clean && holds(result.text, "See1andasite.")) && "a reference and an address, set as text");
        assert((holds(result.pdf, "/Subtype /Link") && holds(result.pdf, "/URI (https://example.org)") &&
                holds(result.pdf, "/Dest [")) &&
               "and with hyperref, links: to the label's place and to the address");
    }
    {
        const Result result = article("\\usepackage[hidelinks]{hyperref}", "\\section{A}\\label{a}\\ref{a}");
        assert((holds(result.pdf, "/Border [0 0 0]")) && "hidelinks frames none");
    }
    {
        const Result result = article("", "See \\ref{nowhere}.");
        assert((holds(result.errors, "warning: Reference `nowhere' undefined") && holds(result.text, "See??.")) &&
               "a reference whose label never comes, named as LaTeX names it");
    }
    {
        const Result result = article("", "\\ref{a}");
        assert((!holds(result.pdf, "/Subtype /Link")) && "no links without hyperref");
    }

    return 0;
}
