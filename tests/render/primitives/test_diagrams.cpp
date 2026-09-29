#include "engine.hpp"
#include "render/primitives/diagrams.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Diagrams primitive: tikz-cd's, xy-pic's and amscd's diagrams, quantikz's
// and Qcircuit's circuits, and forest's and qtree's trees, each drawn as the
// TikZ picture it is.

/// One document as the engine set it: whether it reported nothing, what it
/// reported, its PDF, and every page's text together, the spaces and line
/// ends taken out.
struct Result {
    bool clean{false};
    std::string errors{};
    std::string pdf{};
    std::string text{};
};

/// Typesets a whole document, as written.
static Result typeset(const std::string_view document) {
    static const std::filesystem::path assets = engine::locate(__FILE__);
    Result result;
    std::ostringstream errors;
    std::vector<std::string> pages;
    result.clean = engine::typeset(assets, document, result.pdf, {}, errors, &pages);
    result.errors = errors.str();
    for (std::string& page : pages) {
        std::erase_if(page, [](const char letter) { return letter == ' ' || letter == '\n'; });
        result.text += page;
    }
    return result;
}

/// Typesets a body with packages loaded, and says what was reported when
/// anything was.
static Result article(const std::string_view packages, const std::string_view body) {
    Result result = typeset("\\documentclass{article}\\usepackage{" + std::string(packages) + "}\\begin{document}" +
                            std::string(body) + "\\end{document}");
    if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
    return result;
}

/// Whether a text holds another.
static bool holds(const std::string_view text, const std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

/// How many times a text holds another.
static std::size_t count(const std::string_view text, const std::string_view part) {
    std::size_t found = 0;
    for (std::size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size())) ++found;
    return found;
}

/// The heights text is set at on the page, down from its top.
static std::vector<float> heights(const std::string_view pdf) {
    std::vector<float> found;
    for (std::size_t at = pdf.find(" Tm"); at != std::string_view::npos; at = pdf.find(" Tm", at + 1)) {
        const std::size_t start = pdf.rfind(' ', at - 1) + 1;
        float y = 0.0f;
        std::from_chars(pdf.data() + start, pdf.data() + at, y);
        found.push_back(y);
    }
    return found;
}

int main() {
    {
        // tikz-cd, displayed: its objects, its arrows as strokes, its labels,
        // a hook, a dashed arrow, and a label across its line.
        const Result result = article("tikz-cd", "\\[\\begin{tikzcd} A \\arrow[r, \"f\"] \\arrow[d, \"g\"'] & "
                                                 "B \\arrow[d, hook] \\\\ C \\arrow[r, \"h\"', dashed] "
                                                 "\\arrow[ur, \"k\" description] & D \\end{tikzcd}\\]");
        assert((result.clean && result.errors.empty()) && "a tikz-cd diagram, displayed");
        for (const std::string_view part : {"A", "B", "C", "D", "f", "g", "h", "k"}) {
            assert(holds(result.text, part) && "each object and each label set");
        }
        assert((count(result.pdf, " l S") > 24) && "its arrows drawn: lines, tips and a hook");
        assert((holds(result.pdf, "] 0 d") && holds(result.pdf, "1 1 1 rg")) && "one dashed, one label on a white ground");

        // An arrow to a cell that is not there is a warning, drawn without.
        const Result lost = article("tikz-cd", "\\[\\begin{tikzcd} A \\arrow[rr] & B \\end{tikzcd}\\]");
        assert((lost.clean && holds(lost.errors, "goes to no object")) && "an arrow to nothing is named");

        // Inside a formula of other things: the grid of its objects.
        const Result inside = article("tikz-cd", "$x = \\begin{tikzcd} A \\arrow[r] & B \\end{tikzcd}$");
        assert((inside.clean && holds(inside.text, "A") && holds(inside.text, "B")) && "its objects in a formula");
    }
    {
        // Numbered, the number stands level with the diagram's middle, as
        // tikz-cd sets a diagram on the formula's axis.
        const Result result = article("tikz-cd", "\\begin{equation}\\begin{tikzcd} A \\arrow[d] \\\\ B \\\\ C "
                                                 "\\end{tikzcd}\\end{equation}");
        assert((result.clean && holds(result.text, "(1)")) && "a numbered diagram");
        std::vector<float> ys = heights(result.pdf);
        std::erase_if(ys, [](const float y) { return y > 700.0f; });   // the folio
        std::ranges::sort(ys);
        std::vector<float> levels;
        for (const float y : ys) {
            if (levels.empty() || y - levels.back() > 2.0f) levels.push_back(y);
        }
        assert((levels.size() == 3) && "three rows of text: the number level with the middle one");
    }
    {
        // xy-pic: labels above and below, a dashed arrow, the spacing asked for.
        const Result result = article("xy", "\\[\\xymatrix@C=3pc{A \\ar[r]^f \\ar[d]_g & B \\ar[d]^h \\\\ "
                                            "C \\ar@{-->}[r]_k & D}\\]");
        assert((result.clean && result.errors.empty()) && "an xy-pic diagram");
        for (const std::string_view part : {"A", "B", "C", "D", "f", "g", "h", "k"}) {
            assert(holds(result.text, part) && "each object and label set");
        }
        assert((holds(result.pdf, "] 0 d")) && "its dashed arrow dashed");
    }
    {
        // amscd: arrows written between objects and under them.
        const Result result = article("amscd", "\\[\\begin{CD} A @>f>> B \\\\ @VgVV @VVhV \\\\ C @>>k> D \\end{CD}\\]");
        assert((result.clean && result.errors.empty()) && "an amscd diagram");
        assert((holds(result.text, "A") && holds(result.text, "f") && holds(result.text, "k")) && "its objects and labels");
        assert((count(result.pdf, " l S") >= 12) && "its four arrows drawn");
    }
    {
        // A circuit: its labels, a gate, a control joined to its target.
        const Result result = article("quantikz", "\\begin{quantikz} \\lstick{$\\ket{0}$} & \\gate{H} & \\ctrl{1} & "
                                                  "\\meter{} \\\\ \\lstick{$\\ket{0}$} & \\qw & \\targ{} & \\qw "
                                                  "\\end{quantikz}");
        assert((result.clean && result.errors.empty()) && "a quantikz circuit");
        assert((holds(result.text, "H") && count(result.text, "0") >= 2) && "its gate and its labels");
        assert((count(result.pdf, " l S") > 40) && "its wires, boxes, circle and meter drawn");

        const Result old = article("qcircuit", "\\Qcircuit @C=1em @R=.7em { & \\gate{X} & \\ctrl{1} & \\qw \\\\ "
                                               "& \\qw & \\targ & \\qw }");
        assert((old.clean && holds(old.text, "X")) && "a Qcircuit circuit");
    }
    {
        // Trees: forest's brackets and qtree's, every label set and every
        // child joined to its parent.
        const Result forest = article("forest", "\\begin{forest} [S [NP [the] [cat]] [VP [sat]]] \\end{forest}");
        assert((forest.clean && forest.errors.empty()) && "a forest tree");
        for (const std::string_view part : {"S", "NP", "VP", "the", "cat", "sat"}) {
            assert(holds(forest.text, part) && "each label set");
        }
        assert((count(forest.pdf, " l S") == 5) && "a line from each of its five children to its parent");

        const Result qtree = article("qtree", "\\Tree [.S [.NP the cat ] [.VP sat ] ]");
        assert((qtree.clean && holds(qtree.text, "the") && holds(qtree.text, "cat")) && "a qtree tree, bare words leaves");
        assert((count(qtree.pdf, " l S") == 5) && "each joined");
    }
    return 0;
}
