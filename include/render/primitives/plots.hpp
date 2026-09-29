#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render::primitives {

    /// @brief pgfplots: an axis in a TikZ picture, and the plots drawn in it
    ///        -- from points, from a table, or from a function of x.
    ///
    /// @par Language
    /// @code
    /// \begin{tikzpicture}
    ///   \begin{axis}[xlabel={$x$}, ylabel={$f(x)$}, grid=major, legend pos=north west]
    ///     \addplot[blue, domain=-2:2, samples=50] {x^2};
    ///     \addplot coordinates {(0,1) (1,3) (2,2)};
    ///     \addplot table {data.dat};
    ///     \legend{$x^2$, measured}
    ///   \end{axis}
    /// \end{tikzpicture}
    /// @endcode
    ///
    /// An axis is drawn as it closes, with TikZ's own `\\draw` and `\\node`,
    /// in the picture around it: its box, or its lines through the origin;
    /// its ticks at round numbers -- powers of ten on a logarithmic one --
    /// and their labels; its labels and title; a grid when asked; every plot
    /// clipped to the box, as lines, marks or bars; and the legend. A plot
    /// given no options, or `+` before them, takes the next of pgfplots' own
    /// colors and marks; one given options is drawn as they say, black and
    /// without marks unless they say otherwise.
    ///
    /// A function is sampled over its domain, `-5:5` unless the plot or the
    /// axis says, at `samples` points, 25 unless they say, and read as
    /// pgfplots reads one: `+ - * / ^`, parentheses, `x`, `pi` and `e`, and
    /// sin, cos and tan in degrees, deg, rad, exp, ln, log10, log2, sqrt,
    /// abs, asin, acos, atan, atan2, min, max, pow, floor, ceil, round,
    /// sinh, cosh and tanh. A parametric plot, `({cos(x)}, {sin(x)})`, is
    /// two of them.
    ///
    /// @par Reported
    /// A plot outside an axis, and one whose points could not be read -- a
    /// function it cannot work out, a table file it cannot find.
    class Plots {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Plots(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the axes, `\\addplot`, `\\addlegendentry` and `\\legend`.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; a table's file is read through its reader.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Works out a function of x as pgfplots writes one.
        /// @param text The function: `x^2 - 3*sin(deg(x))`.
        /// @param x    The value of x.
        /// @return Its value, or nothing when it is not a function this reads
        ///         or has no finite value there.
        /// @complexity O(n) in the text.
        [[nodiscard]] static std::optional<double> calculate(std::string_view text, double x);

        /// @brief Errors this module has recorded.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        /// @brief One plot, as `\\addplot` gave it.
        struct Plot {
            std::string options{};                        ///< As written.
            bool cycled{false};                           ///< Takes the next of pgfplots' colors and marks.
            std::vector<std::pair<double, double>> points{};   ///< Its data: x and y, NaN for a gap.
        };

        /// @brief One axis being read: its kind, its options and its plots.
        struct Axis {
            std::string kind{};                   ///< `axis`, `semilogyaxis` and the rest.
            std::string options{};                ///< As written.
            std::vector<Plot> plots{};            ///< In the order given.
            std::vector<std::string> legends{};   ///< Each plot's entry, in order.
        };

        mutable std::vector<Axis> axes{};                        ///< The axes open, innermost last.
        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
    };

}
