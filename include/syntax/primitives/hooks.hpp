#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"
#include "memory/dictionary.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace syntax::primitives {

    /// @brief Named places one package leaves open for another to fill:
    ///        `\\addtohook`, `\\usehook`.
    ///
    /// This is how packages talk to each other without either having to know
    /// the other is there. A package that does something at a point others
    /// may care about -- a heading being set, a document starting -- runs
    /// `\\usehook{name}` there; any package that cares adds its own code to
    /// that name with `\\addtohook`, before or after, and it runs in the order
    /// it was added. A hook nothing was added to runs nothing. The core
    /// package gives both LaTeX's own names, `\\AddToHook` and `\\UseHook`,
    /// so a package written for LaTeX's hook system reads as it was written.
    ///
    /// @par Language
    /// @code
    /// \addtohook{heading}{\medskip}     % one package's contribution
    /// \addtohook{heading}{\noindent}    % another's, after it
    /// \usehook{heading}                 % runs both, in that order
    /// @endcode
    ///
    /// Code is kept as the tokens it was written as, unexpanded, so it means
    /// whatever its macros mean when the hook runs rather than when it was
    /// added -- as LaTeX has it. Hooks are global: added code outlives the
    /// group it was added in.
    class Hooks {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Hooks(Lexicon& lexicon) noexcept;

        /// @brief Installs `\\addtohook` and `\\usehook`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; unused by this module.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Every error this module has recorded; it records none.
        ///
        /// Adding to a hook and running one cannot fail -- an unknown hook
        /// is an empty one -- but every module answers the same question, so
        /// Wrapper can gather them without knowing which ones can.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

        /// @brief Runs a hook: its code goes in front of whatever is read
        ///        next, and nothing happens when nothing was added to it.
        ///
        /// What `\\usehook` does, and what the engine does itself at the two
        /// hooks LaTeX gives every document: `begindocument`, run as
        /// `\\begin{document}` opens, and `enddocument`, as it closes.
        ///
        /// @param mouth Expander to read the code into.
        /// @param name  The hook.
        /// @complexity O(n) in the hook's tokens.
        void run(Mouth& mouth, std::string_view name) const;

    private:
        /// What each hook runs, in the order it was added. Mutable because
        /// the module is installed as a const callable, as every module is.
        mutable memory::Dictionary<std::vector<Token>> hooks{};

        std::vector<Traceback> tracebacks{};   ///< Always empty; see traceback().
    };

}
