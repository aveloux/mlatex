#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Paragraphs primitive: how paragraphs are shaped -- indented or flush,
// justified, ragged or centred, set in from their margins -- and the blocks
// that shape them: center, flushleft, flushright, ragged2e's, and the
// quotation blocks.

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
        const Result result = article("", "\\noindent flush\\par\\indent in \\\\ broken\\par After.");
        assert((result.clean && holds(result.text, "flushinbrokenAfter.")) && "paragraph controls");
    }
    {
        const Result result = article("", "\\begin{center}c\\end{center}\\begin{flushleft}l\\end{flushleft}"
                                          "\\begin{flushright}r\\end{flushright}\\begin{justify}j\\end{justify}");
        assert((result.clean && holds(result.text, "clrj")) && "the alignments");
        // A centred line starts past the middle of the column's left half.
        assert((holds(result.pdf, " 0 0 -1 30") || holds(result.pdf, " 0 0 -1 29")) &&
               "a centred line sits in the middle");
    }
    {
        const Result result = article("", "\\begin{quote}q\\end{quote}\\begin{quotation}qq\\end{quotation}"
                                          "\\begin{verse}a \\\\ b\\end{verse}");
        assert((result.clean && holds(result.text, "qqqab")) && "quotations and verse");
    }
    {
        const Result result = article("", "{\\centering centred\\par}{\\raggedright ragged\\par}{\\raggedleft left"
                                      "\\par}"
                                          "\\leftskip=2em indented\\par");
        assert((result.clean && holds(result.text, "centredraggedleftindented")) && "the switches, and \\leftskip");
    }
    {
        const Result result = article("", "\\begin{abstract}What it says.\\end{abstract}");
        assert((result.clean && holds(result.text, "AbstractWhatitsays.")) && "the abstract under its name");
    }
    return 0;
}
