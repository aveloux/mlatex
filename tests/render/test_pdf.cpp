#include "engine.hpp"

#include <cassert>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The PDF writer: a file a reader opens -- a header, a trailer, and a
// cross-reference table whose every entry points at the object it names --
// each page its own object, each face embedded once, cut to the glyphs
// used and named for what it is, with the map that lets its text be
// copied, and the same document written the same every time.

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

/// A one-pixel PNG, for a picture handed in from memory.
static std::string dot() {
    static constexpr unsigned char bytes[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x64, 0x60, 0xF8, 0x5F,
        0x0F, 0x00, 0x02, 0x87, 0x01, 0x80, 0xEB, 0x47, 0xBA, 0x92, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    return std::string(reinterpret_cast<const char*>(bytes), sizeof bytes);
}

/// @brief Is this a PDF a reader can open: its every cross-reference right?
static bool valid(const std::string& pdf) {
    if (!pdf.starts_with("%PDF-1.") || !pdf.ends_with("%%EOF\n")) return false;

    const std::size_t pointer = pdf.rfind("startxref\n");
    if (pointer == std::string::npos) return false;
    std::size_t table = 0;
    if (std::from_chars(pdf.data() + pointer + 10, pdf.data() + pdf.size(), table).ec != std::errc{}) return false;
    if (pdf.compare(table, 7, "xref\n0 ") != 0) return false;

    std::size_t entries = 0;
    const auto [end, failure] = std::from_chars(pdf.data() + table + 7, pdf.data() + pdf.size(), entries);
    if (failure != std::errc{} || entries < 2) return false;

    // Twenty bytes an entry, the first being the free list's head.
    const std::size_t first = static_cast<std::size_t>(end - pdf.data()) + 1 + 20;
    for (std::size_t object = 1; object < entries; ++object) {
        std::size_t offset = 0;
        const char* entry = pdf.data() + first + (object - 1) * 20;
        if (std::from_chars(entry, entry + 10, offset).ec != std::errc{}) return false;
        const std::string head = std::to_string(object) + " 0 obj";
        if (pdf.compare(offset, head.size(), head) != 0) return false;
    }
    return true;
}

int main() {
    {
        const Result result = article("", "Hello, world. \\textbf{Bold.} $x^2$");
        assert((result.clean && valid(result.pdf)) && "a PDF a reader can open");
        assert((count(result.pdf, "/Type /Page\n") == 1) && "of one page");
        assert((count(result.pdf, "/FontFile3") == 3) &&
               "the body face, its bold and the maths face, each embedded once");
        assert((holds(result.pdf, "+LMRoman10-Regular") && holds(result.pdf, "+NewCMMath-Regular")) &&
               "each named for what it is, behind a subset's tag");
        assert((count(result.pdf, "/ToUnicode") == 3) && "each with the map that lets its text be copied");
        assert((result.text == "Hello,world.Bold.x21") && "and read back, word for word");
    }
    {
        std::string words;
        for (int index = 0; index < 1500; ++index) words += "incomprehensibilities ";
        const Result result = article("", words);
        assert((result.clean && valid(result.pdf) && count(result.pdf, "/Type /Page\n") > 1) &&
               "a long run of text flows onto more pages, each an object of its own");
    }
    {
        engine::Host host;
        host.files["dot.png"] = dot();
        const Result result = typeset("\\documentclass{article}\\usepackage{graphicx}\\begin{document}"
                                      "\\includegraphics{dot.png}\\end{document}",
                                      host);
        assert((result.clean && valid(result.pdf) && holds(result.pdf, "/Subtype /Image") &&
               holds(result.pdf, "/FlateDecode")) && "a picture embedded, compressed");
    }
    {
        const Result once = article("", "The same words.");
        const Result again = article("", "The same words.");
        assert((once.pdf == again.pdf) && "the same document is written the same, byte for byte");
    }
    {
        const Result result = typeset("\\documentclass[a4paper]{article}\\begin{document}x\\end{document}");
        assert((holds(result.pdf, "/MediaBox [0 0 595 842]")) && "each page as large as its sheet");
    }
    {
        const Result result = typeset("\\DocumentMetadata{pdfstandard=A-2b}\\documentclass{article}"
                                      "\\usepackage{hyperref}\\hypersetup{pdftitle={A Title}, pdfauthor={A. Writer}}"
                                      "\\begin{document}Archived.\\end{document}");
        assert((result.clean && valid(result.pdf)) && "a PDF/A document is a valid PDF");
        assert((holds(result.pdf, "/OutputIntents") && holds(result.pdf, "<pdfaid:part>2</pdfaid:part>") &&
                holds(result.pdf, "/Metadata") && holds(result.pdf, "/ID [<")) &&
               "PDF/A-2b: its XMP, its output intent and its identifier");
        assert((holds(result.pdf, "/Title (A Title)") && holds(result.pdf, "/Author (A. Writer)") &&
                holds(result.pdf, "A Title</rdf:li>")) &&
               "the title and author hyperref gave, in the file's information and its XMP alike");
    }

    return 0;
}
