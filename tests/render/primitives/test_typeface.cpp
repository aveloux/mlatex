#include "engine.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Typeface primitive: the faces a document chooses itself from the
// ones the engine ships -- \textfont for the text, \mathfont for formulas,
// and TeX's \font, which names a face at a size -- and a family that is not
// there reported by name.

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
        const Result result = article("", "\\textfont{NewCMSans10-Regular}Sans from here.");
        assert((result.clean && holds(result.text, "Sansfromhere.") && holds(result.pdf, "+NewCMSans10-Regular")) &&
               "\\textfont sets what follows in another face");
    }
    {
        const Result result = article("", "\\font\\big=NewCM10-Bold at 20pt \\big Big.");
        assert((result.clean && holds(result.text, "Big.") && holds(result.pdf, " 20 Tf")) &&
               "\\font names a face at a size, and the name selects it");
    }
    {
        const Result result = article("", "\\mathfont{NewCMMath-Regular}$x+y$");
        assert((result.clean && holds(result.text, "x+y")) && "\\mathfont sets the formulas' face");
    }
    {
        const Result result = article("\\usepackage{fontspec}\\setmainfont{TeX Gyre Termes}[Ligatures=TeX]"
                                      "\\setmathfont{STIX Two Math}",
                                      "Times from here, $x+y$.");
        assert((result.clean && result.errors.empty() && holds(result.pdf, "+STIXTwoText-Regular") &&
                holds(result.pdf, "+STIXTwoMath-Regular")) &&
               "a face named as the system names it is set in the nearest one carried");
        const Result unknown = article("\\usepackage{fontspec}\\setmainfont{Comic Sans MS}", "Kept.");
        assert((unknown.clean && holds(unknown.errors, "warning: fontspec: no face near 'Comic Sans MS'") &&
                holds(unknown.pdf, "+LMRoman10-Regular")) &&
               "one with nothing near it keeps the text's face, and says so");
    }
    {
        const Result result = article("", "\\textfont{no-such-family}x");
        assert((holds(result.errors, "No font family named 'no-such-family'")) &&
               "a family that is not there is named");
    }
    return 0;
}
