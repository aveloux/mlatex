#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace render::primitives {

    /// @brief Type styles: family, series, shape and size, as LaTeX's font
    ///        selection has them.
    ///
    /// @par Language
    /// @code
    /// \textbf{bold}        \textit{italic}      \texttt{monospaced}
    /// \textsc{small caps}  \emph{emphasis}      \textsf{sans serif}
    /// \textsl{slanted}     \textup{upright}     \textnormal{the body's face}
    ///
    /// {\bfseries from here on}   % until the group closes
    /// {\itshape and here}
    /// \textbf{\textit{bold and italic at once}}
    ///
    /// \large bigger    \Large    \LARGE    \huge    \Huge biggest
    /// \small smaller   \footnotesize    \scriptsize    \tiny smallest
    /// \normalsize      % and back to the body size
    /// @endcode
    ///
    /// Family, series and shape change independently, as they do in LaTeX:
    /// `\\bfseries` inside `\\itshape` is bold italic, `\\sffamily` keeps both,
    /// and a size keeps all three, so `{\\bfseries\\large ...}` is large and
    /// bold. `\\emph` is italic in upright text and upright in italic text.
    /// The sizes are LaTeX's own for the class's body size -- `\\large` is 12
    /// points in a 10- or 11-point document, not a fixed fraction of it.
    ///
    /// @par How a face is chosen
    /// A font tree names its cuts in its file names, as Latin Modern does:
    /// `lmroman10-regular`, `lmroman10-bolditalic`, `lmromancaps10-regular`,
    /// `lmsans10-oblique`, `lmmono10-italic`. The face wanted is spelled that
    /// way at the design size LaTeX would pick for the size asked -- the
    /// 12-point design for 12 points, the 8-point one for a footnote's
    /// script -- and looked up, falling back to the 10-point design and then
    /// to the face in use, so a missing cut never fails a document.
    class Styles {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Styles(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the style primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; faces come from its registry.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::tracebacks() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<syntax::Traceback>& tracebacks() const noexcept {
            return tracebacks_;
        }

        /// @brief One change to the face in use.
        enum class Cut : std::uint8_t {
            Current,    ///< None: a change of size alone.
            Normal,     ///< `\\normalfont`: roman, medium, upright -- the body's face.
            Roman,      ///< `\\rmfamily`: the serif family, series and shape kept.
            Sans,       ///< `\\sffamily`.
            Mono,       ///< `\\ttfamily`.
            Medium,     ///< `\\mdseries`: the normal weight, family and shape kept.
            Bold,       ///< `\\bfseries`.
            Upright,    ///< `\\upshape`.
            Italic,     ///< `\\itshape`.
            Slanted,    ///< `\\slshape`.
            Caps,       ///< `\\scshape`.
            Emphasis    ///< `\\em`: italic in upright text, upright in italic.
        };

        /// @brief One of LaTeX's ten size steps.
        enum class Size : std::uint8_t {
            Tiny,        ///< `\\tiny`.
            Script,      ///< `\\scriptsize`.
            Footnote,    ///< `\\footnotesize`.
            Small,       ///< `\\small`.
            Normal,      ///< `\\normalsize`: the class's body size.
            Large,       ///< `\\large`.
            Larger,      ///< `\\Large`.
            Largest,     ///< `\\LARGE`.
            Huge,        ///< `\\huge`.
            Hugest       ///< `\\Huge`.
        };

        /// @brief The face for a change to the face in use, at a size.
        ///
        /// Headings, a description's terms and an equation's number find
        /// their faces here too, which is why it is public: one rule for what
        /// a cut is called.
        ///
        /// @param context Engine services, for the registry and the selection.
        /// @param cut     The change wanted.
        /// @param size    Size wanted, in points.
        /// @return A font, never null while a text face is selected.
        [[nodiscard]] static const typography::Font* resolve(
            const Context& context,
            Cut cut,
            float size
        );

        /// @brief A size step in points, for a body size.
        ///
        /// LaTeX's tables for its 10-, 11- and 12-point classes, the nearest
        /// of them to the body in use, scaled to it.
        ///
        /// @param body The body size, `\\normalsize`, in points.
        /// @param step The step wanted.
        /// @return Its size in points.
        /// @complexity O(1).
        [[nodiscard]] static float measure(float body, Size step) noexcept;

    private:
        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.

        /// @brief A face asked for by its parts and not yet selected: what
        ///        `\\fontsize`, `\\fontfamily`, `\\fontseries` and
        ///        `\\fontshape` leave for `\\selectfont`.
        struct Pending {
            std::optional<float> size{};     ///< In points.
            std::optional<Cut> family{};     ///< Roman, sans or typewriter.
            std::optional<Cut> series{};     ///< Bold or medium.
            std::optional<Cut> shape{};      ///< Italic, slanted, small capitals or upright.
        };
        mutable Pending pending{};           ///< What \\selectfont selects next.
    };

}
