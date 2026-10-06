#include "latex.hpp"
#include "render/primitives/plots.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Plots primitive: pgfplots' axes, their ticks and labels, and what
// \addplot draws in them -- coordinates, tables and functions of x -- and
// the legend, with the functions worked out as pgfplots writes them.

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

/// Typesets one picture with pgfplots loaded, and says what was reported
/// when anything was.
static Result picture(const std::string_view body) {
    Result result = typeset("\\documentclass{article}\\usepackage{pgfplots}\\begin{document}"
                            "\\begin{tikzpicture}" + std::string(body) + "\\end{tikzpicture}\\end{document}");
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

/// Whether a function comes to what it should, near enough.
static bool comes(const std::string_view function, const double x, const double expected) {
    const auto value = render::primitives::Plots::calculate(function, x);
    return value && std::abs(*value - expected) < 1e-9;
}

int main() {
    using render::primitives::Plots;

    assert(comes("x^2 - 3*x + 2", 4.0, 6.0) && "sums, products and a power");
    assert(comes("-x^2", 3.0, -9.0) && "a sign binds looser than a power");
    assert(comes("2^3^2", 0.0, 512.0) && "a power binds to the right");
    assert(comes("sin(90) + cos(0)", 0.0, 2.0) && "sine and cosine in degrees");
    assert(comes("sin(deg(pi/2))", 0.0, 1.0) && "deg turns radians to degrees");
    assert(comes("sin(\\x r)", std::acos(-1.0) / 2.0, 1.0) && "TikZ's \\x, and r for radians");
    assert(comes("exp(ln(5)) + sqrt(16) + abs(-2)", 0.0, 11.0) && "exp, ln, sqrt and abs");
    assert(comes("max(1, min(x, 3)) + pow(2, 10)", 7.0, 1027.0) && "functions of two");
    assert(comes("{1e-3} * 1000", 0.0, 1.0) && "braces as parentheses, and a number with an exponent");
    assert(!Plots::calculate("ln(0)", 0.0) && "no finite value is none");
    assert(!Plots::calculate("foo(1)", 0.0) && !Plots::calculate("1 +", 0.0) && "nor is what is not a function");

    // TikZ's formulas: a number with a unit is a length in points, and says so.
    bool measured = false;
    const auto length = Plots::calculate("veclen(3pt, 4pt) + 1in", 0.0, &measured);
    assert((length && std::abs(*length - 77.27) < 1e-9 && measured) && "veclen, and a length in points");
    measured = false;
    assert((comes("mod(7, 3) + int(2.7) + sign(-4)", 0.0, 2.0)) && "mod, int and sign");
    assert((Plots::calculate("2*3", 0.0, &measured) && !measured) && "a formula of no unit is a number");
    assert((comes("1cm", 0.0, 72.27 / 2.54)) && "a centimetre in points");

    {
        const Result result = picture("\\begin{axis}[xlabel={Time}, ylabel={Speed}, title={Run}]"
                                      "\\addplot[blue, domain=0:4] {x^2};"
                                      "\\end{axis}");
        assert((result.clean && holds(result.text, "Run") && holds(result.text, "Time") &&
                holds(result.text, "Speed")) &&
               "an axis's title and labels");
        assert((holds(result.text, "10") && holds(result.text, "15") && holds(result.text, "3")) &&
               "its ticks at round numbers across the data, fives up and ones across");
    }
    {
        const Result result = picture("\\begin{axis}"
                                      "\\addplot coordinates {(0,1) (1,3) (2,2)};"
                                      "\\addplot table {x y \\\\ 0 5 \\\\ 2 1 \\\\};"
                                      "\\legend{first, second}"
                                      "\\end{axis}");
        assert((result.clean && holds(result.text, "first") && holds(result.text, "second")) &&
               "coordinates, a table and a legend");
        assert(holds(result.text, "5") && "the table's data in the limits");
    }
    {
        const Result result = picture("\\begin{semilogyaxis}\\addplot coordinates {(1,1) (2,100)};"
                                      "\\addlegendentry{errors}\\end{semilogyaxis}");
        assert((result.clean && holds(result.text, "10") && holds(result.text, "errors")) &&
               "a logarithmic axis's powers of ten");
    }
    {
        const Result result = picture("\\begin{axis}[ybar, symbolic x coords={A,B}, xtick=data]"
                                      "\\addplot coordinates {(A,3) (B,5)};\\end{axis}");
        assert((result.clean && holds(result.text, "A") && holds(result.text, "B")) &&
               "bars at symbolic coordinates, labelled by them");
    }
    {
        const Result result = typeset("\\documentclass{article}\\usepackage{pgfplots}\\begin{document}"
                                      "\\addplot {x};\\end{document}");
        assert((!result.clean && holds(result.errors, "\\addplot outside an axis")) && "a plot needs an axis");
    }
    {
        const Result result = picture("\\begin{axis}\\addplot {nosuch(x)};\\end{axis}");
        assert(holds(result.errors, "is not a function this can work out") && "a function it cannot work out");
    }
    {
        // Error bars: a line each way they are drawn and a bar across each
        // end -- three strokes a point, given with the data, from a table's
        // column, or the same for every point -- and the limits make room.
        const Result plain = picture("\\begin{axis}[xtick={0,1}, ytick={1,2}]"
                                     "\\addplot+[mark=none] coordinates {(0,1) (1,2)};\\end{axis}");
        const Result given = picture("\\begin{axis}[xtick={0,1}, ytick={1,2}]"
                                     "\\addplot+[mark=none, error bars/.cd, y dir=both, y explicit] "
                                     "coordinates {(0,1) +- (0,0.5) (1,2) +- (0,0.25)};\\end{axis}");
        const Result tabled = picture("\\begin{axis}[xtick={0,1}, ytick={1,2}]"
                                      "\\addplot+[mark=none, error bars/y dir=plus, error bars/y explicit] "
                                      "table[x=x, y=y, y error=e] {x y e \\\\ 0 1 0.5 \\\\ 1 2 0.25 \\\\};\\end{axis}");
        const Result fixed = picture("\\begin{axis}[xtick={0,1}, ytick={1,2}]"
                                     "\\addplot+[mark=none, error bars/.cd, x dir=both, x fixed=0.1] "
                                     "coordinates {(0,1) (1,2)};\\end{axis}");
        assert((plain.clean && given.clean && tabled.clean && fixed.clean) && "plots with error bars, each read");
        assert((count(given.pdf, " l S") == count(plain.pdf, " l S") + 6) && "a bar and two ends a point");
        assert((count(tabled.pdf, " l S") == count(plain.pdf, " l S") + 6) && "from a table's column, upwards");
        assert((count(fixed.pdf, " l S") == count(plain.pdf, " l S") + 6) && "the same across for every point");
    }
    {
        // fill between: the region between two named plots -- one of them
        // not drawn -- filled once, under the lines, within its soft clip.
        const Result result = picture("\\begin{axis}[domain=0:2]"
                                      "\\addplot[name path=f, blue] {x^2};"
                                      "\\addplot[name path=g, draw=none] {0};"
                                      "\\addplot[gray, fill opacity=0.3] fill between[of=f and g, soft clip={domain=0.5:1.5}];"
                                      "\\end{axis}");
        assert((result.clean && result.errors.empty()) && "a fill between two plots");
        assert((count(result.pdf, "\nh f\n") == 1) && "filled once");
        assert((result.pdf.find("h f\n") < result.pdf.find("0 0 1 RG")) && "under the blue line");
        const Result lost = picture("\\begin{axis}\\addplot[name path=f] {x};"
                                    "\\addplot fill between[of=f and nothing];\\end{axis}");
        assert((holds(lost.errors, "fill between: no plot named 'nothing'")) && "a plot it cannot find is named");
    }
    return 0;
}
