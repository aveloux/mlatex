#pragma once

#include "syntax/mouth.hpp"
#include "syntax/semantics/registers.hpp"

#include <concepts>
#include <functional>

// Forward declared, not included: policy.hpp is tiny, but a document's
// permissions are the sandbox's business and nothing here reads a field.
// Holding a reference needs the name, not the definition.
namespace sandbox { struct Policy; }

namespace syntax::primitives {

    // Same reason, and a necessary one: relay.hpp includes this header, so
    // including it back would close the cycle. Context only holds a reference.
    class Relay;

    /// @brief Everything a primitive needs that is not the expander itself.
    ///
    /// Passed by reference to every module, so a module never owns engine
    /// state and two modules always see the same state.
    struct Context {
        const sandbox::Policy& policy;      ///< What the document is allowed to do.
        semantics::Registers& ledger;       ///< The register bank, scoped by Union.
        Relay& relay;                       ///< Conditionals, for a module that needs to branch.
    };

    /// @brief A module: anything callable as `(Mouth&, Context&)`.
    ///
    /// This is the whole interface. A module declares no base class and
    /// overrides nothing; it just has to be invocable, which is why Wrapper
    /// can fold over a pack of unrelated types.
    template <typename Module>
    concept Primitive = std::invocable<Module, Mouth&, Context&>;

    /// @brief Installs every module given, in order.
    ///
    /// @param mouth   Expander to bind into.
    /// @param context Engine services, handed to each module unchanged.
    /// @param modules The modules to install; later ones win a name clash.
    /// @complexity O(1) in the number of modules -- the fold is expanded at
    ///             compile time, so this is a straight run of calls.
    void bind(Mouth& mouth, Context& context, Primitive auto&&... modules) {
        (std::invoke(modules, mouth, context), ...);
    }

}