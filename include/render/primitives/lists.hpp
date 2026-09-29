#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace render::primitives {

    /// @brief Lists, and the items inside them.
    ///
    /// @par Language
    /// @code
    /// \begin{itemize}
    ///     \item A bulleted item.
    ///     \item Another, long enough to run onto a second line, which
    ///           starts under the first word rather than under the bullet.
    /// \end{itemize}
    ///
    /// \begin{enumerate}
    ///     \item Numbered one.        % 1.
    ///     \item Numbered two.        % 2.
    ///         \begin{enumerate}
    ///             \item Nested.      % (a)
    ///         \end{enumerate}
    /// \end{enumerate}
    ///
    /// \begin{description}
    ///     \item[Term] What it means.
    /// \end{description}
    ///
    /// \begin{enumerate}[label=(\roman*)]      % enumitem's options: (i), (ii), ...
    /// \begin{enumerate}[(a)]                  % and its short labels: (a), (b), ...
    /// \begin{itemize}[noitemsep]              % no space between the items
    /// \begin{enumerate}[start=4, nosep]       % 4., 5., ... and none around the list
    /// @endcode
    ///
    /// An item is not an argument: `\\item` starts a new paragraph with a
    /// label, and whatever follows is that paragraph's text until the next
    /// item or the end of the list. Every line of it starts at the list's
    /// margin, and the label hangs to the left of the first -- LaTeX's
    /// `\\leftmargin` and `\\labelsep`, measured in ems of the text face.
    ///
    /// Lists nest, and each level is indented past the one outside it. The
    /// labels change with depth as LaTeX's do: a bullet, a dash, an asterisk
    /// and a centred dot for itemize; `1.`, `(a)`, `i.` and `A.` for
    /// enumerate. A numbered item is what a `\\label` after it refers to.
    ///
    /// The space around a list and between its items is LaTeX's own --
    /// `\\topsep` around it, `\\itemsep` and `\\parsep` between items, each
    /// smaller at each level down -- and enumitem's options change it and
    /// the labels: a label is written with `\\arabic*`, `\\alph*`, `\\roman*`
    /// and their capital forms standing for the item's number, and may hold
    /// anything text may, a formula or a style.
    ///
    /// @par How the block is noticed
    /// The three environment names are registered with the block module rather
    /// than bound as primitives of their own, so `\\begin` and `\\end` stay
    /// one implementation and this module only says what those three names
    /// mean to it.
    class Lists {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Lists(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs `\\item` and registers the three list blocks.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the blocks are hooked through its
        ///                block module.
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

    private:
        /// @brief What an item's label looks like.
        enum class Marker : std::uint8_t {
            Bullet,       ///< A dot, the same at every item.
            Number,       ///< Counting from one within this level.
            Description   ///< Whatever the item wrote in brackets.
        };

        /// @brief One open list.
        struct Level {
            Marker marker{Marker::Bullet};   ///< What its items are labelled with.
            int counter{0};                  ///< How many items it has had.
            std::string label{};             ///< Its own label, as enumitem's `label=` wrote it; empty for the default.
            float spacing{-1.0f};            ///< Space between two items, in points; below zero for LaTeX's own.
            float around{-1.0f};             ///< Space above and below the list, in points; the same.
            bool begun{false};               ///< An item has been set in it, so the next is spaced from it.
        };

        /// How far each level is indented past the one outside it, in ems.
        static constexpr float indent = 2.5f;

        /// Space between a label and the text after it, in ems.
        static constexpr float separation = 0.5f;

        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.
        mutable std::vector<Level> levels{};                    ///< The lists currently open.

        /// enumitem's \\setlist: options each kind of list reads before its
        /// own, by the kind's name -- and under the empty name, what every
        /// list reads.
        mutable std::unordered_map<std::string, std::string> defaults{};
    };

}
