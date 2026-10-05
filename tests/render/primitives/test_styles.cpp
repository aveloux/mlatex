#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Styles primitive: the faces text is set in -- bold, italic, slanted,
// small capitals, sans, typewriter, emphasis that turns upright inside
// italic -- as commands and as switches, and every size from \tiny to
// \Huge, and NFSS's own \fontsize and \selectfont.

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

/// How many times a text says something.
static std::size_t count(const std::string_view text, const std::string_view part) {
    std::size_t found = 0;
    for (std::size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size())) {
        ++found;
    }
    return found;
}

int main() {
    {
        const Result result = article("", "\\textbf{bold} \\textit{italic} \\texttt{mono} \\textsc{caps} "
                                          "\\textsf{sans} \\textsl{slant} \\emph{stress} \\textup{up} \\textmd{md} "
                                          "\\textrm{rm} \\textnormal{normal}");
        assert((result.clean && holds(result.text, "bolditalicmonocapssansslantstressupmdrmnormal")) &&
               "every style, small capitals copied as the letters typed");
        assert((count(result.pdf, "/BaseFont") >= 6) && "each in a face of its own");
    }
    {
        const Result result = article("", "{\\bfseries bold \\itshape both} {\\ttfamily mono} {\\scshape caps} "
                                          "\\textit{outer \\emph{inner}}");
        assert((result.clean && holds(result.text, "boldbothmonocapsouterinner")) &&
               "the switches, and emphasis in italic");
    }
    {
        const Result result = article("", "{\\tiny a}{\\scriptsize b}{\\footnotesize c}{\\small d}{\\normalsize e}"
                                          "{\\large f}{\\Large g}{\\LARGE h}{\\huge i}{\\Huge j}");
        assert((result.clean && holds(result.text, "abcdefghij")) && "every size");
        assert((holds(result.pdf, " 5 Tf") && holds(result.pdf, " 24.88 Tf")) &&
               "from \\tiny's five points to \\Huge's");
    }
    {
        const Result result = article("", "{\\fontsize{14}{17}\\selectfont sized} after");
        assert((result.clean && holds(result.pdf, " 14 Tf")) && "\\fontsize takes effect at \\selectfont");
    }
    {
        const Result result = article("", "\\bfseries All bold. \\normalfont Plain again.");
        assert((result.clean && holds(result.text, "Allbold.Plainagain.")) &&
               "a switch at the top level holds until undone");
    }
    {
        const Result result = article("\\renewcommand{\\familydefault}{\\sfdefault}", "Sans, and \\textbf{bold}.");
        assert((result.clean && holds(result.pdf, "LMSans10-Regular") && holds(result.pdf, "LMSans10-Bold") &&
                !holds(result.pdf, "LMRoman10-Regular")) &&
               "\\familydefault makes the normal face sans, and every cut of it");
    }

    return 0;
}
