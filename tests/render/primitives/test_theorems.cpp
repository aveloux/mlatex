#include "engine.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Theorems primitive: \newtheorem -- numbered on its own, within a
// section, or sharing another's counter -- its styles, the optional name a
// theorem is given, the proof block and its closing mark.

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
        const Result result = article("\\usepackage{amsthm}\\newtheorem{theorem}{Theorem}[section]"
                                      "\\newtheorem{corollary}[theorem]{Corollary}\\newtheorem*{remark}{Remark}",
                                      "\\section{A}\\begin{theorem}[Energy]First.\\end{theorem}"
                                      "\\begin{corollary}Second.\\end{corollary}\\begin{remark}Unnumbered.\\end{remark}"
                                      "\\begin{proof}Because.\\end{proof}");
        assert((result.clean) && "theorems read without a report");
        assert((holds(result.text, "Theorem1.1(Energy).First.")) && "numbered within its section, with its name");
        assert((holds(result.text, "Corollary1.2.Second.")) && "a corollary sharing the theorem's counter");
        assert((holds(result.text, "Remark.Unnumbered.")) && "a starred one unnumbered");
        assert((holds(result.text, "Proof.Because.\xE2\x96\xA1") || holds(result.text, "Proof.Because.\xE2\x88\x8E")) &&
               "a proof, its mark at the end");
    }
    {
        const Result result = article("\\usepackage{amsthm}\\theoremstyle{definition}"
                                      "\\newtheorem{definition}{Definition}",
                                      "\\begin{definition}Upright.\\end{definition}");
        assert((result.clean && holds(result.text, "Definition1.Upright.")) && "a theorem style");
    }
    return 0;
}
