#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Requests primitive: \httpget and \httppost, text fetched from the
// network where the document stands. Nothing here reaches the network: a
// port nothing listens on is refused at once, and the refusal reported by
// the address asked for.

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
        const Result result = article("", "Before \\httpget{http://127.0.0.1:1/nothing} after.");
        assert((!result.clean && holds(result.errors, "\\httpget could not reach 'http://127.0.0.1:1/nothing'")) &&
               "a request that cannot be made is reported by its address");
        assert((holds(result.text, "Beforeafter.")) && "and the text around it is set all the same");
    }
    {
        const Result result = article("", "\\httppost{http://127.0.0.1:1/form}{name=value}");
        assert((holds(result.errors, "\\httppost could not reach 'http://127.0.0.1:1/form'")) && "and so is a post");
    }
    return 0;
}
