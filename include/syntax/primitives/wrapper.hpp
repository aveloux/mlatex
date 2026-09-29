#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/blocks.hpp"
#include "syntax/primitives/compute.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/decimals.hpp"
#include "syntax/primitives/hooks.hpp"
#include "syntax/primitives/include.hpp"
#include "syntax/primitives/loops.hpp"
#include "syntax/primitives/macros.hpp"
#include "syntax/primitives/relay.hpp"
#include "syntax/primitives/values.hpp"
#include "syntax/primitives/variables.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace syntax::primitives {

    /// @brief Every core primitive, installed in one call.
    ///
    /// Each module is a Primitive -- a callable taking `(Mouth&, Context&)` --
    /// so this type holds them and folds over them. Adding a module is a
    /// member and a name in the pack; nothing declares or calls `ingest`.
    ///
    /// @par Cost
    /// The fold is expanded at compile time, so installation is a straight run
    /// of bind() calls with no loop, no indirection and no allocation beyond
    /// the handler table itself. Dispatch afterwards is an array index on the
    /// interned symbol, so a primitive costs O(1) to reach however many are
    /// installed.
    ///
    /// @par Use
    /// @code
    /// syntax::primitives::Wrapper core(lexicon);
    /// syntax::primitives::Context context{registers, core.relay, core.variables};
    /// core(mouth, context);
    ///
    /// // ... run the document, then collect what the core reported ...
    /// for (const auto& fault : core.traceback()) {
    ///     std::cerr << fault.format() << '\n';
    /// }
    /// @endcode
    ///
    /// Context needs a Relay and a Variables reference and Wrapper owns both,
    /// so build the Wrapper first and hand its #relay and #variables to the
    /// Context.
    class Wrapper {
    public:
        /// @brief Constructs every module against one interning table.
        /// @param lexicon Interning table, shared with the expander.
        explicit Wrapper(Lexicon& lexicon) noexcept;

        /// @brief Installs every module.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services, passed through to each module.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Every error the core reported, gathered from each module.
        ///
        /// Modules keep their own lists, so nothing has to be constructed and
        /// threaded through them. This merges those lists on demand, which is
        /// a once-per-run cost rather than a per-error one.
        ///
        /// @return A fresh vector holding every module's tracebacks.
        /// @complexity O(n) in the number of errors reported.
        [[nodiscard]] std::vector<Traceback> traceback() const;

        // Declared first because Context is built from the relay and the
        // other modules are handed that same Context.
        Relay relay;       ///< Conditionals; Context holds a reference to this.
        Blocks blocks;     ///< `\\enter` and `\\leave`; its depth() reports unclosed blocks.
        Macros macros;     ///< `\\define`, `\\forget`, `\\alias` and the prefixes.
        Values values;     ///< Registers: `\\set`, `\\increase`, `\\scale`, `\\reduce`, `\\name`.
        Compute compute;   ///< `\\evaluate`.
        Loops loops;       ///< `\\repeat`, `\\group`, `\\ungroup`.
        Include include;   ///< `\\include`, `\\input` and the package primitives.
        Variables variables;    ///< `\\variable`, `\\setvariable`, `\\ifvariable`, `\\setkeys`: what a
                                ///< Context reads and a caller hands values in through before a run.
        Decimals decimals;      ///< `\\calculate`, `\\amount`, `\\separators`.
        Hooks hooks;            ///< `\\addtohook`, `\\usehook`.
    };

    static_assert(Primitive<Wrapper>, "Wrapper must itself be installable as a module");

}