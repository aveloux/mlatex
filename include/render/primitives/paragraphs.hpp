#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <string_view>
#include <vector>

namespace render::primitives {

    /// @brief The shape of a paragraph: its indentation, alignment and margins.
    ///
    /// None of these draw anything. Each becomes a layout::Node::Directive in
    /// its place among the material, and the document obeys it when it
    /// assembles the paragraphs that follow -- which is the only point at which
    /// a paragraph's shape can be decided, since TeX decides it at the `\\par`
    /// that ends the paragraph and not at the command.
    ///
    /// @par Language
    /// @code
    /// \noindent This paragraph starts flush with the margin.
    ///
    /// \begin{center}
    ///     Every line of this is centred,
    ///     and so is this one.
    /// \end{center}
    ///
    /// \begin{flushleft} Ragged right. \end{flushleft}
    /// \begin{flushright} Ragged left. \end{flushright}
    ///
    /// \begin{quote}                       % a short passage, in from both margins
    ///     A quotation, unindented.
    /// \end{quote}
    ///
    /// \begin{quotation}                   % a longer one, paragraphs indented as body text is
    ///     Several paragraphs of it.
    /// \end{quotation}
    ///
    /// \begin{verse}                       % poetry: the same margins, ragged at the right
    ///     One line of it, \\   % a line ends here
    ///     and another.
    /// \end{verse}
    ///
    /// {\centering A centred paragraph without a block around it.\par}
    /// \justifying                 % and back to both edges straight
    ///
    /// \leftskip=20pt              % every line from here starts 20pt in
    /// \rightskip=20pt             % and ends 20pt short of the far edge
    /// @endcode
    ///
    /// @par How the blocks nest
    /// Each block's opening states its whole shape -- both margins and the
    /// alignment -- so closing one restores the block around it exactly, or
    /// the document's own baseline when there is none, whichever pieces that
    /// block changed. A `flushleft` inside a `quote` hands back the quote's
    /// margins and justification when it ends, not only its alignment.
    class Paragraphs {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Paragraphs(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the paragraph primitives and hooks the alignment blocks.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the blocks are hooked through its
        ///                block module, and `\\leftskip` scans against its registers.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded: a `\\leftskip` with no
        ///        dimension after it.
        [[nodiscard]] const std::vector<syntax::Traceback>& tracebacks() const noexcept {
            return tracebacks_;
        }

    private:
        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.

        /// What each open alignment block opened with, innermost last: the
        /// source that ends the paragraph and sets that block's alignment,
        /// which is also exactly what closing the block inside it restores.
        mutable std::vector<std::string_view> openings{};
    };

}
