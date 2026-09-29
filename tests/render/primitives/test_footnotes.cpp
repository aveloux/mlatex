#include "engine.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Footnotes primitive: a note marked in the text and set at the foot of
// the page that calls it -- numbered, or marked and written apart -- a
// title's \thanks, and a note in the margin.

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
    {
        const Result result = article("", "Text.\\footnote{A note.} More.\\footnote{Another.}");
        assert((result.clean && holds(result.text, "Text.1More.2")) && "each note is numbered where it is called");
        assert((holds(result.text, "1Anote.2Another.")) && "and set at the foot, in order");
    }
    {
        const Result result = article("", "Marked\\footnotemark{} here.\\footnotetext{Written apart.}");
        assert((result.clean && holds(result.text, "Marked1here.") && holds(result.text, "1Writtenapart.")) &&
               "a mark and its text written apart");
    }
    {
        std::string filler;
        for (int index = 0; index < 40; ++index) filler += "Line after line of text to fill the page. ";
        const Result result = article("", filler + "Late.\\footnote{On the page that calls it.} " + filler);
        bool together = false;
        for (const std::string& page : result.pages) {
            together = together || (holds(page, "Late.1") && holds(page, "1Onthepagethatcallsit."));
        }
        assert((result.clean && together) && "a note lands on the page its call does");
    }
    {
        const Result result = article("", "Text.\\marginpar{In the margin.} On.");
        assert((result.clean && holds(result.text, "Inthemargin.")) && "a margin note is set");
    }
    {
        const Result result = article("\\title{T\\thanks{Supported.}}\\author{A}", "\\maketitle Body.");
        assert((result.clean && holds(result.text, "Supported.")) && "a title's \\thanks is a note");
    }
    return 0;
}
