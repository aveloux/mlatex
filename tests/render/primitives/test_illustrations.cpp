#include "latex.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Illustrations primitive: \includegraphics sized every way, pictures
// handed in from memory, the picture block's lines, projected lines, and
// TikZ's paths -- and a picture that makes room for all of its drawing.

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

/// Each straight stroke a PDF draws, `x y m` then `x y l S`, as how far it
/// goes across and how far up, in points.
static std::vector<std::pair<float, float>> strokes(const std::string_view pdf) {
    std::vector<std::pair<float, float>> found;
    for (std::size_t at = pdf.find(" l S"); at != std::string_view::npos; at = pdf.find(" l S", at + 1)) {
        const std::size_t move = pdf.rfind(" m\n", at);
        if (move == std::string_view::npos) continue;
        float numbers[4]{};
        const std::size_t begin = pdf.rfind('\n', move - 1) + 1;
        const std::string_view written = pdf.substr(begin, at - begin);
        const char* cursor = written.data();
        for (float& number : numbers) {
            while (cursor < written.data() + written.size() && (*cursor == ' ' || *cursor == '\n' || *cursor == 'm')) ++cursor;
            cursor = std::from_chars(cursor, written.data() + written.size(), number).ptr;
        }
        found.emplace_back(numbers[2] - numbers[0], numbers[1] - numbers[3]);
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

int main() {
    // --- Pictures ------------------------------------------------------------------
    {
        latex::Host host;
        host.files["dot.png"] = dot();
        const Result result = typeset("\\documentclass{article}\\usepackage{graphicx}\\begin{document}"
                                      "\\includegraphics[width=2cm]{dot.png}"
                                      "\\includegraphics[height=1cm,keepaspectratio]{dot.png}"
                                      "\\includegraphics[scale=3]{dot.png}\\includegraphics[width=0.5\\linewidth]{dot}"
                                      "\\end{document}",
                                      host);
        assert((result.clean) && "pictures sized every way, one found without its extension");
        assert((count(result.pdf, "/Subtype /Image") == 1) && "one picture, embedded once however often drawn");
        assert((holds(result.pdf, " Do")) && "and drawn");

        const Result wrong = typeset("\\documentclass{article}\\usepackage{graphicx}\\begin{document}"
                                     "\\includegraphics[angle=90]{dot.png}\\end{document}",
                                     host);
        assert((wrong.clean && holds(wrong.errors, "warning: \\includegraphics: the 'angle' key is not supported")) &&
               "a key it cannot honour is a warning that names it");

        // PostScript and SVG are drawn as a PDF's page is, each a form.
        latex::Host drawn;
        drawn.files["chart.eps"] = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 50\n0 0 100 50 rectfill\n";
        drawn.files["shape.svg"] = "<svg width=\"80\" height=\"40\"><circle cx=\"40\" cy=\"20\" r=\"10\"/></svg>";
        const Result vector = typeset("\\documentclass{article}\\usepackage{graphicx}\\usepackage{svg}\\begin{document}"
                                      "\\includegraphics[width=4cm]{chart}\\includesvg[width=3cm]{shape}\\end{document}",
                                      drawn);
        assert((vector.clean && vector.errors.empty()) && "an EPS and an SVG picture, found without their extensions");
        assert((count(vector.pdf, "/Subtype /Form") == 2 && holds(vector.pdf, "/BBox [0 0 100 50]") &&
                holds(vector.pdf, "/BBox [0 0 60 30]")) &&
               "each drawn as a form, its box its own");

        // A form it does not draw is framed, and said so.
        latex::Host unknown;
        unknown.files["chart.ps"] = "not a picture in any form";
        const Result kept = typeset("\\documentclass{article}\\usepackage{graphicx}\\begin{document}"
                                    "\\includegraphics[width=4cm]{chart}\\end{document}",
                                    unknown);
        assert((kept.clean && holds(kept.errors, "'chart' is in a form this engine does not draw; its place is kept")) &&
               "a picture in a form it does not draw is found, and its place kept with a warning");
        assert((count(kept.pdf, "/Subtype /Image") == 0 && holds(kept.pdf, " re")) && "framed, and empty");
    }
    {
        // Offline, a picture from the network is refused before a request
        // is made; one handed in under the same kind of name is not.
        latex::Host offline;
        offline.offline = true;
        const Result result = typeset("\\documentclass{article}\\usepackage{graphicx}\\begin{document}"
                                      "\\includegraphics{https://127.0.0.1:1/chart.png}\\end{document}",
                                      offline);
        assert((!result.clean &&
                holds(result.errors, "\\includegraphics of 'https://127.0.0.1:1/chart.png' refused: the run is offline")) &&
               "an offline run fetches no picture");
    }
    {
        const Result result = article("\\usepackage{graphicx}", "\\includegraphics{no-such-picture.png}");
        assert((!result.clean && holds(result.errors, "could not read 'no-such-picture.png'")) &&
               "a picture that is not there is named");
    }

    // --- The picture block ------------------------------------------------------------
    {
        const Result result = article("", "\\begin{picture}{100pt}{50pt}\\linecolor{#FF0000}\\line{0pt}{0pt}"
                                          "{100pt}{50pt}\\linestyle{dashed}\\lineweight{2pt}"
                                          "\\line{0pt}{50pt}{100pt}{0pt}\\end{picture}");
        assert((result.clean && holds(result.pdf, "1 0 0 RG")) && "a line is stroked in its color");
        assert((holds(result.pdf, "] 0 d")) && "and a dashed one dashed");
    }
    {
        const Result result = article("", "\\begin{picture}{100pt}{100pt}\\isometric\\spaceline{0pt}{0pt}{0pt}"
                                          "{10pt}{10pt}{10pt}\\orthographic{30}{45}\\spaceline{0pt}{0pt}{0pt}{0pt}"
                                          "{0pt}{10pt}\\planar\\end{picture}");
        assert((result.clean) && "projected lines");
    }
    {
        // Declared ten points tall, drawn a hundred: the text after it has to
        // start below the drawing, not ninety points up inside it.
        const Result result = article("", "\\begin{picture}{10pt}{10pt}\\line{0pt}{0pt}{100pt}{100pt}\\end{picture}\n\n"
                                          "After.");
        float lowest = 0.0f;
        for (std::size_t at = result.pdf.find(" Tm"); at != std::string::npos; at = result.pdf.find(" Tm", at + 1)) {
            const std::size_t start = result.pdf.rfind(' ', at - 1) + 1;
            float y = 0.0f;
            std::from_chars(result.pdf.data() + start, result.pdf.data() + at, y);
            if (y < 700.0f) lowest = std::max(lowest, y);
        }
        assert((result.clean && lowest > 72.0f + 100.0f) &&
               "the text after a picture starts below all of its drawing");
    }

    // --- TikZ -------------------------------------------------------------------------
    {
        const Result result = article("\\usepackage{tikz}", R"(\begin{tikzpicture}[scale=1.5]
    \draw[thick, ->] (0,0) -- (3,0);
    \draw[blue!60, dashed] (0,0) rectangle (2,1);
    \draw (0,0) -- (1,1) -- (2,0) -- cycle;
    \draw[red] (1,1) circle (0.5);
    \draw[very thin, gray] (0,0) grid (3,2);
    \draw (0,0) |- (1,1) -| (2,0);
    \draw (0,0) -- ++(1,0) -- +(0,1) -- (30:1cm);
    \draw[line width=2pt, opacity=0.5] (3,0) arc (0:90:1);
    \filldraw[fill=red] (0,0) circle (0.2);
    \node at (1,1) {label};
\end{tikzpicture}
\tikz{\draw (0,0) -- (1,1);})");
        assert((result.clean) && "TikZ paths of every kind");
        assert((count(result.pdf, " l S") > 100) && "drawn as strokes, curves included");
        assert((holds(result.pdf, "1 0 0 RG")) && "each in its own color");
        assert((holds(result.text, "label")) && "a node's text is set");

        const Result outside = article("", "\\draw (0,0) -- (1,1);");
        assert((holds(outside.errors, "\\draw needs an open picture")) && "\\draw needs a picture");
        const Result option = article("", "\\begin{tikzpicture}\\draw[wobbly] (0,0) -- (1,1);\\end{tikzpicture}");
        assert((option.clean && holds(option.errors, "warning: \\draw: the option 'wobbly' is not one this engine draws")) &&
               "an option it cannot draw is a warning that names it, and the line is drawn without it");
        const Result path = article("", "\\begin{tikzpicture}\\draw (0,0) parabola (1,1);\\end{tikzpicture}");
        assert((path.clean && holds(path.errors, "is not a path this engine draws")) && "and so is a path it cannot read");
    }
    {
        // Curves, as TikZ writes them, flattened into strokes.
        const Result curves = article("", "\\begin{tikzpicture}\\draw (0,0) .. controls (1,2) and (2,-1) .. (3,1);"
                                          "\\draw (0,0) to[bend left] (3,0) to[out=90,in=180] (4,1);"
                                          "\\draw (0,0) .. controls +(0,1) and +(0,1) .. (2,0);\\end{tikzpicture}");
        assert((curves.clean && curves.errors.empty()) && "a curve's controls, a bend and the angles a `to` leaves at");
        assert((count(curves.pdf, " l S") > 20) && "each drawn as many strokes");
        const Result broken = article("", "\\begin{tikzpicture}\\draw (0,0) .. (1,1);\\end{tikzpicture}");
        assert((!broken.clean && holds(broken.errors, "a curve needs its controls")) && "a curve without them is named");

        // Fills: \fill's inside, \filldraw's inside and outline, fill= on \draw.
        const Result fills = article("", "\\begin{tikzpicture}\\fill[red] (0,0) rectangle (1,1);"
                                         "\\filldraw[fill=blue,draw=black] (2,0) circle (0.5);"
                                         "\\draw[fill=green] (3,0) -- (4,1) -- (5,0) -- cycle;\\end{tikzpicture}");
        assert((fills.clean && fills.errors.empty() && count(fills.pdf, "\nh f\n") == 3) && "three regions filled");
        assert((holds(fills.pdf, "1 0 0 rg") && holds(fills.pdf, "0 0 1 rg")) && "each in its own color");
    }
    {
        // A PDF as a figure: a page the engine made itself, handed back in,
        // set as a form with its fonts, and the part of it asked for shown.
        const Result made = article("", "A figure made elsewhere, $x^2$.");
        latex::Host handed;
        handed.files["figure.pdf"] = made.pdf;
        const Result result = typeset("\\documentclass{article}\\usepackage{graphicx}\\begin{document}"
                                      "\\includegraphics[width=5cm,page=1,trim=100 600 100 100,clip]{figure}"
                                      "\\includegraphics[width=3cm]{figure.pdf}\\end{document}",
                                      handed);
        assert((result.clean && result.errors.empty()) && "a PDF's page is drawn, its keys read");
        assert((count(result.pdf, "/Subtype /Form") == 1 && holds(result.pdf, "/Fm0 Do")) &&
               "as one form however often it is drawn");
        assert((holds(result.pdf, " re W n")) && "clipped to the part shown");
        assert((count(result.pdf, "/FontFile") >= 2) && "its fonts carried with it, beside the document's own");
    }

    {
        const Result result = article("\\usepackage{tikz}", "\\begin{tikzpicture}\\draw[domain=0:2, samples=5] "
                                                            "plot ({\\x}, {\\x*\\x});\\end{tikzpicture}");
        assert((result.clean && count(result.pdf, " l\n") + count(result.pdf, " l ") >= 1) &&
               "a plot of a function drawn through its points");
    }
    {
        const Result result = article("\\usepackage{tikz}", "\\begin{tikzpicture}\\node at (0,0) {A};\\end{tikzpicture}"
                                                            "\\hfill\\begin{tikzpicture}\\node at (0,0) {B};"
                                                            "\\end{tikzpicture}");
        std::vector<float> across;
        for (std::size_t at = result.pdf.find(" Tm"); at != std::string::npos; at = result.pdf.find(" Tm", at + 1)) {
            const std::size_t y = result.pdf.rfind(' ', at - 1);
            const std::size_t x = result.pdf.rfind(' ', y - 1) + 1;
            float value = 0.0f;
            std::from_chars(result.pdf.data() + x, result.pdf.data() + y, value);
            across.push_back(value);
        }
        assert((result.clean && across.size() >= 2 && std::ranges::max(across) - std::ranges::min(across) > 150.0f) &&
               "two pictures side by side in the line, \\hfill between them");
    }
    {
        // A picture in the line stands on the baseline, as TikZ's does: a
        // line up from its origin starts where the text around it sits.
        const Result result = article("\\usepackage{tikz}", "x\\tikz\\draw (0,0) -- (0,1);y "
                                                            "\\tikz[baseline=-0.5ex]{\\draw (0,0) circle (1ex);}");
        const std::size_t move = result.pdf.find(" m\n");
        const std::size_t start = result.pdf.rfind(' ', move - 1) + 1;
        float y = 0.0f;
        std::from_chars(result.pdf.data() + start, result.pdf.data() + move, y);
        const std::size_t text = result.pdf.find(" Tm");
        const std::size_t first = result.pdf.rfind(' ', text - 1) + 1;
        float baseline = 0.0f;
        std::from_chars(result.pdf.data() + first, result.pdf.data() + text, baseline);
        assert((result.clean && std::abs(y - baseline) < 0.01f) && "its bottom on the baseline, and \\tikz's two forms");
    }
    {
        // A matrix: its cells as nodes named by row and column, arrows
        // between them stopping at their edges, and a label halfway along
        // the line it is written on.
        const Result result = article("\\usepackage{tikz}",
                                      "\\begin{tikzpicture}\\matrix (m) [matrix of math nodes, row sep=2em, column sep=3em]"
                                      "{ a & b \\\\ c & d \\\\ };\\draw[->] (m-1-1) -- node[above] {$x$} (m-1-2);"
                                      "\\draw[->] (m-1-1) -- (m-2-1);\\end{tikzpicture}");
        assert((result.clean && result.errors.empty()) && "a matrix of math nodes, and arrows between its cells");
        for (const std::string_view part : {"a", "b", "c", "d", "x"}) assert(holds(result.text, part) && "every cell set");
        std::vector<float> across;
        for (std::size_t at = result.pdf.find(" Tm"); at != std::string::npos; at = result.pdf.find(" Tm", at + 1)) {
            const std::size_t y = result.pdf.rfind(' ', at - 1);
            const std::size_t x = result.pdf.rfind(' ', y - 1) + 1;
            float value = 0.0f;
            std::from_chars(result.pdf.data() + x, result.pdf.data() + y, value);
            across.push_back(value);
        }
        assert((across.size() >= 5 && across[4] > across[0] && across[4] < across[1]) &&
               "the label between the two cells its line joins");
    }
    {
        // Edges: each from where the path stands, with its own tips and bend,
        // a loop back to its node, and the path going on from where it was.
        const Result result = article("\\usepackage{tikz}",
                                      "\\begin{tikzpicture}\\node[circle,draw] (a) at (0,0) {a};"
                                      "\\node[circle,draw] (b) at (2,0) {b};\\node (c) at (0,2) {c};"
                                      "\\path[->] (a) edge node[above] {1} (b) edge[bend left] (c) (b) edge[loop above] ();"
                                      "\\draw (a) to[out=90,in=180] (c);\\draw[double] (a) -- (c);"
                                      "\\draw[->, shift left=2pt] (a) -- (b);\\draw[|->>] (a) -- (c);"
                                      "\\draw[hook->] (b) -- (c);\\end{tikzpicture}");
        assert((result.clean && result.errors.empty()) && "edges, loops, double lines, shifts and every tip");
        assert((holds(result.text, "1") && count(result.pdf, " l S") > 150) && "drawn, the label set");
    }
    {
        // A node's own outline: dashed, doubled, its corners rounded.
        const Result result = article("\\usepackage{tikz}",
                                      "\\begin{tikzpicture}\\node[draw, dashed] at (0,0) {a};"
                                      "\\node[draw, double] at (2,0) {b};"
                                      "\\node[draw, rounded corners=3pt] at (4,0) {c};\\end{tikzpicture}");
        assert((result.clean && result.errors.empty() && holds(result.pdf, "] 0 d")) && "a node's outline dashed");
        assert((count(result.pdf, " l S") == 4 + 8 + 28) &&
               "a box of four sides, one drawn twice, and one of four sides and four quarter circles");
    }
    {
        // A coordinate's parts may be formulas, and a point may be shifted.
        const Result result = article("\\usepackage{tikz}",
                                      "\\begin{tikzpicture}\\coordinate (a) at (0,0);\\coordinate (b) at (2,0);"
                                      "\\draw (0,0) -- ({1+1},{sin(30)*2});"
                                      "\\draw (a) -- ([xshift=10pt]b);\\end{tikzpicture}");
        const std::vector<std::pair<float, float>> drawn = strokes(result.pdf);
        constexpr float centimetre = 72.27f / 2.54f;
        const auto near = [](const std::pair<float, float> stroke, const float across, const float up) {
            return std::abs(stroke.first - across) < 0.01f && std::abs(stroke.second - up) < 0.01f;
        };
        assert((result.clean && result.errors.empty() && drawn.size() == 2) && "two lines, each read");
        assert((near(drawn[0], 2 * centimetre, centimetre)) && "a coordinate of formulas");
        assert((near(drawn[1], 2 * centimetre + 10.0f, 0.0f)) && "a point shifted");

        // calc's `$...$` works one point out of others: partway, a length
        // along, turned, scaled and summed.
        const Result calc = article("\\usepackage{tikz}\\usetikzlibrary{calc}",
                                    "\\begin{tikzpicture}\\coordinate (a) at (0,0);\\coordinate (b) at (2,0);"
                                    "\\draw (a) -- ($(a)!0.5!(b)+(0,1)$);"
                                    "\\draw (a) -- ($(a)!1cm!(b)$);"
                                    "\\draw (a) -- ($(a)!0.5!90:(b)$);"
                                    "\\draw (a) -- ($2*(b)-(0,1)$);\\end{tikzpicture}");
        const std::vector<std::pair<float, float>> worked = strokes(calc.pdf);
        assert((calc.clean && calc.errors.empty() && worked.size() == 4) && "four lines, each read");
        assert((near(worked[0], centimetre, centimetre)) && "halfway to a point, then up one");
        assert((near(worked[1], centimetre, 0.0f)) && "a centimetre towards a point");
        assert((near(worked[2], 0.0f, centimetre)) && "halfway to a point turned a quarter round");
        assert((near(worked[3], 4 * centimetre, -centimetre)) && "twice a point, less another");

        // TikZ's let: point and number registers, read up to `in` and
        // written out in the rest of the path.
        const Result let = article("\\usepackage{tikz}\\usetikzlibrary{calc}",
                                   "\\begin{tikzpicture}\\coordinate (a) at (0,0);\\coordinate (b) at (3,4);"
                                   "\\draw let \\p1 = (a), \\p2 = (b), \\n1 = {veclen(\\x2-\\x1,\\y2-\\y1)} in (a) -- (\\n1, 0);"
                                   "\\draw let \\p1 = ($(b)-(a)$), \\n{angle} = {atan2(\\y1,\\x1)} in (a) -- (\\n{angle}:1cm);"
                                   "\\draw let \\p{top} = (b) in (a) -- (\\x{top}, 0);\\end{tikzpicture}");
        const std::vector<std::pair<float, float>> set = strokes(let.pdf);
        assert((let.clean && let.errors.empty() && set.size() == 3) && "three paths with registers, each read");
        assert((near(set[0], 5 * centimetre, 0.0f)) && "a length worked out of two points' registers");
        assert((near(set[1], 0.6f * centimetre, 0.8f * centimetre)) && "an angle, a register named in braces");
        assert((near(set[2], 3 * centimetre, 0.0f)) && "a point register's x");
        const Result unread = article("\\usepackage{tikz}", "\\begin{tikzpicture}\\draw let \\p1 = (1,0) (0,0) -- (1,1);"
                                                            "\\end{tikzpicture}");
        assert((holds(unread.errors, "up to 'in'")) && "a let without its 'in' is named");
    }

    return 0;
}
