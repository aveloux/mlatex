#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <string>
#include <vector>

namespace render::primitives {

    /// @brief Footnotes, set at the foot of the page that calls them.
    ///
    /// @par Language
    /// @code
    /// A claim that needs support.\footnote{The support, in full.}
    /// A second call\footnotemark{} and its text\footnotetext{Given apart.}
    /// \author{A. Author\thanks{Where the author works.}}   % *, then dagger, ...
    /// @endcode
    ///
    /// @par Where the note goes
    /// A call leaves its mark in the line -- the number, raised, in the
    /// body's upright face -- and beside it a directive carrying the note,
    /// already laid out as a paragraph of its own. The pager reserves the
    /// note's height at the foot of whichever page the call's line lands on,
    /// moving the line and its note to the next page together when both do
    /// not fit, and the composer sets the notes there under LaTeX's short
    /// rule. So a note is always on the page that calls it.
    ///
    /// @par How a note is set
    /// As LaTeX's `\\@makefntext` sets one: in `\\footnotesize`, its mark
    /// raised and right-aligned in a box 1.8 ems wide at the head of its
    /// first line, the lines after it full width. The number is the
    /// `footnote` counter's, printed through `\\thefootnote`; `\\thanks`
    /// counts its own marks, `*` and then the daggers, as a title's are.
    class Footnotes {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Footnotes(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs `\\footnote`, `\\footnotemark`, `\\footnotetext` and `\\thanks`.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the number comes from its counters.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded: a `\\footnote` with no text face.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept {
            return tracebacks;
        }

    private:
        /// @brief A mark as it is set in the running text: raised, smaller,
        ///        in the upright body face whatever the text around it is.
        /// @param context Engine services, for the faces.
        /// @param arena   Allocator for the box.
        /// @param text    The mark.
        /// @param size    The size of the text it is set against.
        /// @return The mark's box.
        [[nodiscard]] static layout::Node* mark(const Context& context, memory::Arena& arena,
                                                const std::string& text, float size);

        /// @brief Reads a note's text and lays it out at the foot's size.
        /// @param parser  Parser to read the text with; the brace is next.
        /// @param context Engine services.
        /// @param label   The mark the note opens with.
        /// @return The note, as a column, or nullptr with no text to read.
        [[nodiscard]] layout::Node* note(syntax::Parser& parser, Context& context, const std::string& label) const;

        /// @brief A call's mark and the note it carries, as one node.
        /// @param arena  Allocator for the nodes.
        /// @param raised The mark in the line, or nullptr for none.
        /// @param set    The note, or nullptr for none.
        /// @param origin Where in the source it came from.
        /// @return The node.
        [[nodiscard]] static syntax::Node* call(memory::Arena& arena, layout::Node* raised, layout::Node* set,
                                                memory::Location origin);

        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
        mutable int thanked{0};                                 ///< How many `\\thanks` marks are given out.

        /// How far a mark is raised above the baseline, as a fraction of the
        /// text it is set in: where a superscript sits.
        static constexpr float raise = 0.36f;
    };

}
