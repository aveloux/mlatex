#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace syntax::primitives {

    /// @brief Named block structure: `\\begin{name}` ... `\\end{name}`.
    ///
    /// The document's outermost block is `\\begin{document}`. A block opens a
    /// scope of kind Scope::Type::Environment, so closing one with a bare `}`
    /// is caught by Mouth::pop(), and closing it under the wrong name is
    /// caught here -- unless it was watched transparent, in which case it
    /// opens no scope of its own and whatever its hooks opened is what a bare
    /// `}` would be caught against instead.
    ///
    /// `\\enter` and `\\leave` are bound to the same handlers, for a document
    /// that prefers to say what it means.
    ///
    /// @par Language
    /// @code
    /// \begin{document}
    ///     \begin{quote}
    ///         text
    ///     \end{quote}
    /// \end{document}
    /// @endcode
    ///
    /// Blocks nest strictly. `\\begin{a} \\begin{b} \\end{a}` is reported at
    /// the `\\end`, naming both the block that was open and the one the
    /// document tried to close.
    ///
    /// @par Hooks
    /// Another module may ask to be told when a particular block opens or
    /// closes, which is how a list environment keeps its own nesting without
    /// the block module having to know what a list is. Dispatch is an index
    /// on the interned name, so any number of hooks costs one lookup.
    ///
    /// @code
    /// blocks.watch("itemize",
    ///     [this](syntax::Mouth&) { levels.push_back(Bullet); },
    ///     [this](syntax::Mouth&) { levels.pop_back(); });
    /// @endcode
    ///
    /// @par A document's own blocks
    /// `\\newenvironment{name}[n][default]{begin}{end}`, its `\\renew` and
    /// `\\provide` forms and xparse's `\\NewDocumentEnvironment` make a block
    /// whose opening is a macro taking its arguments, `\\name:open`, and
    /// whose closing is one taking none, `\\name:close` -- names no document
    /// can write, a colon ending a control word. `\\begin` reads the first
    /// inside the block's group; `\\end` reads the second, then closes the
    /// block, so the end code may close what the begin code opened. A block
    /// a document defines of a name the engine draws itself is the
    /// document's; one it redefines keeps the engine's.
    class Blocks {
    public:
        /// What a hook does when its block opens or closes.
        using Handler = std::function<void(Mouth&)>;

        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Blocks(Lexicon& lexicon) noexcept;

        /// @brief Installs `\\begin`, `\\end`, `\\enter` and `\\leave`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; unused by this module.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Asks to be told when one named block opens or closes.
        ///
        /// Any number of modules may watch the same block: each is told, and
        /// none replaces another. Openings run in the order they were asked
        /// for and closings in the reverse, so what the first asked for is
        /// read first both times -- the document's own `\\\\AtEndDocument`
        /// code before the endnotes another module prints there.
        ///
        /// @param name        Block name, without braces.
        /// @param entering    Run after the block opens; may be empty.
        /// @param leaving     Run before the block closes; may be empty.
        /// @param transparent True when the hooks manage their own scope and
        ///                    this block should open none of its own -- a
        ///                    formula display is the concrete case: it opens
        ///                    an Equations scope through `\\[`, and a second,
        ///                    Environment one around that would only have to
        ///                    close before it, which nothing here can arrange.
        /// @complexity O(1) amortized.
        void watch(std::string_view name, Handler entering, Handler leaving,
                  bool transparent = false) const;

        /// @brief Asks to be told when any block opens or closes, whatever
        ///        its name: before a named block's own opening hooks, and
        ///        after its own closing ones -- so what is kept on the way in
        ///        is put back after the block has done with it.
        /// @param entering Run as every block opens.
        /// @param leaving  Run as every block closes.
        void watch(Handler entering, Handler leaving) const;

        /// @brief How many blocks are open.
        /// @complexity O(1).
        [[nodiscard]] std::size_t depth() const noexcept { return open.size(); }

        /// @brief Name of the innermost open block, or empty when none is.
        /// @complexity O(1).
        [[nodiscard]] std::string_view innermost() const noexcept;

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::tracebacks() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return tracebacks_; }

    private:
        /// @brief What to run when one named block opens or closes.
        struct Hook {
            Handler entering{};        ///< Run after the block opens.
            Handler leaving{};         ///< Run before the block closes.
            bool transparent{false};   ///< True: no Environment scope of its own.
            bool own{false};           ///< The document defined it: its code is its own two macros.
        };

        /// @brief Reads a `{name}` argument and interns it.
        /// @param mouth Expander to read from.
        /// @return The interned name, or none when the group was empty.
        [[nodiscard]] Symbol title(Mouth& mouth) const;

        mutable std::vector<Traceback> tracebacks_{};   ///< Errors this module found.
        mutable std::vector<Symbol> open{};             ///< Names of the blocks currently entered.
        mutable std::vector<Symbol> closing{};          ///< A document's blocks whose end code is being read.
        mutable std::vector<Hook> hooks{};              ///< Indexed by interned block name.
        mutable Hook every{};                           ///< Told of every block.
        Lexicon* lexicon{nullptr};                      ///< For resolving names back to text.
    };

}
