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

// The Penalties primitive: breaks forced and forbidden -- \newpage,
// \clearpage and \pagebreak between pages, \\, \newline and \linebreak
// between lines, \nobreak and a tie where no line may end.

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
        const Result result = article("", "one\\newpage two\\clearpage three\\pagebreak four");
        assert((result.clean && result.pages.size() == 4) && "each page break starts a page");
    }
    {
        const Result result = article("", "one\\newpage\\newpage two");
        assert((result.clean && result.pages.size() == 2) && "two in a row make no empty page");
    }
    {
        const Result result = article("", "a\\\\b\\\\*c\\\\[4pt]d\\newline e\\linebreak f\\break g");
        assert((result.clean && holds(result.text, "abcdefg")) && "every line break, with their optional parts");
    }
    {
        const Result result = article("", "Mr.~Smith and a\\nobreak\\ b \\penalty10000 c.");
        assert((result.clean && holds(result.text, "Mr.Smith")) && "a tie and a forbidden break");
    }
    {
        // `\\[20pt]` leaves that much more between its line and the next.
        const auto gap = [](const Result& result) {
            std::vector<float> down;
            for (std::size_t at = result.pdf.find(" Tm"); at != std::string::npos; at = result.pdf.find(" Tm", at + 1)) {
                const std::size_t start = result.pdf.rfind(' ', at - 1) + 1;
                float y = 0.0f;
                std::from_chars(result.pdf.data() + start, result.pdf.data() + at, y);
                down.push_back(y);
            }
            return down.size() >= 2 ? down[1] - down[0] : 0.0f;
        };
        const Result plain = article("", "One\\\\Two");
        const Result spaced = article("", "One\\\\[20pt]Two");
        assert((plain.clean && spaced.clean && std::abs(gap(spaced) - gap(plain) - 20.0f) < 3.0f) &&
               "\\\\[20pt] puts twenty points more under its line");
    }

    return 0;
}
