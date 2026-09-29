#include "engine.hpp"

#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Groups primitive: a brace group, \bgroup and \egroup, and
// \begingroup and \endgroup -- each a scope whose face, color, definitions
// and paragraph shape end with it.

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
        const Result result = article("", "{\\bfseries bold} plain \\bgroup\\itshape slanted\\egroup\\ plain "
                                          "\\begingroup\\ttfamily mono\\endgroup\\ plain.");
        assert((result.clean && holds(result.text, "boldplainslantedplainmonoplain.")) && "every kind of group");
        assert((count(result.pdf, "/BaseFont") >= 4) &&
               "each face set only inside its group, the text after upright");
    }
    {
        const Result result = article("", "{\\def\\inner{in}\\inner} \\ifdefined\\inner kept\\else gone\\fi.");
        assert((result.clean && holds(result.text, "ingone.")) && "a definition ends with its group");
    }
    {
        const Result result = article("", "{\\color{red} red} black.");
        assert((result.clean && holds(result.pdf, "1 0 0 rg") && holds(result.pdf, "0 0 0 rg")) &&
               "a color ends with its group");
    }
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}{open\\end{document}");
        assert((!result.clean) && "a group left open is reported");
    }
    {
        // Where each run of text starts across the page, as its matrix says.
        const Result result = article("", "{\\raggedleft Right.\\par}Left.");
        std::vector<float> across;
        for (std::size_t at = result.pdf.find(" Tm"); at != std::string::npos; at = result.pdf.find(" Tm", at + 1)) {
            const std::size_t y = result.pdf.rfind(' ', at - 1);
            const std::size_t x = result.pdf.rfind(' ', y - 1) + 1;
            float value = 0.0f;
            std::from_chars(result.pdf.data() + x, result.pdf.data() + y, value);
            across.push_back(value);
        }
        assert((result.clean && across.size() >= 2 && across[0] > 300.0f) && "flush right inside its group");
        assert((across.size() >= 2 && std::abs(across[1] - (133.5f + 15.0f)) < 0.5f) &&
               "and the paragraph after it back at the margin, indented");
    }
    return 0;
}
