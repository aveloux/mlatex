#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"
#include "memory/dictionary.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace render::primitives {

    /// @brief Cross-references: `\\label`, `\\ref`, `\\eqref`, `\\cref`,
    ///        `\\Cref`, `\\pageref` and `\\nameref`.
    ///
    /// @par Language
    /// @code
    /// \section{Results}\label{results}
    ///
    /// \begin{equation}
    ///     E = mc^2 \label{energy}
    /// \end{equation}
    ///
    /// Section \ref{results} derives \eqref{energy}.    % Section 2 derives (1).
    /// \nameref{results} begins on page \pageref{results}.  % Results ... page 3.
    /// @endcode
    ///
    /// A label names whatever was numbered most recently -- a heading, an
    /// equation, an enumerated item -- which the numbering modules leave in
    /// Context::anchor for it, and Context::title for `\\nameref`.
    ///
    /// @par Forward references in one pass
    /// LaTeX needs a second run to resolve a reference to a label further on,
    /// because the first writes its labels to a file the second reads. Here
    /// nothing is set until the whole source has been read, so a reference is
    /// a text node left waiting and filled in when its label turns up, before
    /// or after it, in the same run. One whose label never appears reads `??`,
    /// as it does in LaTeX.
    ///
    /// The same serves a package whose text is given further on than it is
    /// used -- an acronym spelled out in a list at the document's end:
    /// `\\@forward{key}` is a text left waiting, and `\\@fulfil{key}{text}`
    /// fills it, before or after.
    ///
    /// @par Pages
    /// A page is another matter: it is known only once the text is set, after
    /// every reference to it has been. A label in the text leaves an anchor
    /// there -- one in a formula takes the anchor before it -- and
    /// `\\pageref` prints the page that anchor landed on the pass before,
    /// asking for that pass with Context::paged when there has been none.
    ///
    /// @par Cost
    /// One hash lookup per `\\label` and per reference; a label fills each
    /// reference waiting for it once, so the whole document costs O(n) in
    /// its labels and references together.
    class References {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit References(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the cross-reference primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; a label reads its anchor.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded: a label defined twice.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept {
            return tracebacks;
        }

        /// @brief The two marks a link is drawn between, and the color its
        ///        text takes.
        struct Hyperlink {
            layout::Node* open{nullptr};                 ///< Opens it; null when there is no link to make.
            layout::Node* close{nullptr};                ///< Ends it.
            const layout::Node::Color* tint{nullptr};    ///< Its text's color with `colorlinks`; null for the text's own.
        };

        /// @brief A link, as hyperref makes one once it is loaded: framed as
        ///        its default frames one -- red within the document, cyan
        ///        round an address, green round a citation -- or with
        ///        `colorlinks` its text in `linkcolor`, `urlcolor` or
        ///        `citecolor`, or neither with `hidelinks`. Nothing without
        ///        hyperref, which is what LaTeX's own references are.
        /// @param context Engine services: the variables hyperref's options are.
        /// @param target  The address it goes to; empty for @p anchor's place.
        /// @param anchor  The anchor it goes to, when it has no address.
        /// @param kind    `link`, `url` or `cite`: which of hyperref's colors.
        [[nodiscard]] static Hyperlink hyperlink(Context& context, std::string_view target, std::size_t anchor,
                                                 std::string_view kind);

        /// @brief What a reference or a citation set, made the link given:
        ///        its text in the link's color, between its two marks.
        /// @param arena   Where the group is made.
        /// @param content What was set.
        /// @param link    The link; with no marks, @p content comes back as it is.
        /// @param origin  Where it was written.
        [[nodiscard]] static syntax::Node* wrapped(memory::Arena& arena, syntax::Node* content, const Hyperlink& link,
                                                   memory::Location origin);

    private:
        /// @brief How a reference is written out.
        enum class Form : std::uint8_t {
            Number,    ///< The number alone: `\\ref`, and inside `\\eqref`'s parentheses.
            Named,     ///< Its kind, then its number: `\\cref`.
            Capital,   ///< The same, the kind capitalised: `\\Cref`.
            Page,      ///< The page it is on: `\\pageref`.
            Title      ///< What it is called: `\\nameref`.
        };

        /// @brief One label, and every reference that came before it.
        struct Mark {
            std::string text{};                         ///< What it names, once defined.
            std::string kind{};                         ///< What kind of thing that is.
            std::string title{};                        ///< What that is called.
            std::size_t anchor{static_cast<std::size_t>(-1)};   ///< Where it stands, for its page.
            bool defined{false};                        ///< Whether its `\\label` has been read.

            /// Text nodes to fill when it is, each with how.
            std::vector<std::pair<syntax::Node*, Form>> references{};

            /// Links to it made before it was, to be pointed at its anchor.
            std::vector<layout::Node*> links{};
        };

        /// @brief A reference's text, written out in its form.
        ///
        /// A kind is named by the variable `cref.kind` -- `Cref.kind` for the
        /// capitalised form -- when the cleveref package or the document set
        /// one, and by the kind itself otherwise. An equation's number is
        /// parenthesised, as cleveref prints one. A page is the one the pass
        /// before found, or `??` on a first pass.
        ///
        /// @param mark    The label.
        /// @param form    How to write it.
        /// @param context Where the names and the pages are kept.
        [[nodiscard]] static std::string write(const Mark& mark, Form form, const Context& context);

        mutable memory::Dictionary<Mark> marks{};   ///< Every label seen or asked for.
        mutable memory::Dictionary<Mark> forwards{};   ///< Every text `\\@fulfil` gave or `\\@forward` asked for, by key.
        mutable std::vector<std::pair<std::string, memory::Location>> awaited{};   ///< Labels referred to before they were made, and where.
        mutable std::vector<std::size_t> waiting{};   ///< Anchors a label gave out, for its mark to set down.
        mutable std::vector<syntax::Traceback> tracebacks{};    ///< Errors this module found.

        syntax::Symbol place{};   ///< The mark a label leaves in the text; no document can write it.
        syntax::Symbol audit{};   ///< The mark the document's end leaves, read after its hooks: no document can write it.
    };

}
