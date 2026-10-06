#pragma once

#include "syntax/mouth.hpp"
#include "syntax/semantics/registers.hpp"
#include "memory/dictionary.hpp"

#include <concepts>
#include <functional>
#include <map>
#include <string>

namespace syntax::primitives {

    // Same reason, and a necessary one: relay.hpp includes this header, so
    // including it back would close the cycle. Context only holds a reference.
    class Relay;
    class Variables;

    /// @brief Files a program embedding the engine hands it from memory, by
    ///        name: packages, inputs and pictures alike.
    ///
    /// Keyed the way the embedded modules are -- `letterhead/main.mtex` is a
    /// package, `terms.mtex` a file to \\input, `chart.png` a picture -- and
    /// looked at before them, so a program may replace a built-in package
    /// with its own without rebuilding the engine. The text is bytes, not
    /// only UTF-8: a picture is kept here exactly as it would sit on disk.
    using Files = memory::Dictionary<std::string>;

    /// @brief A file beside the document, by the name the document gives it:
    ///        `refs.bib`, `figures/plot.png`, `chapter.tex`.
    ///
    /// Set by a host that read the document from a folder, and read through
    /// once each: what it returns stays where it points for the whole run.
    /// A name that reaches outside that folder -- from its root, or up
    /// through `..` -- is never read, and a document that came from memory
    /// has no folder to read from, only the files it wrote itself (#Writer),
    /// which is what keeps a document from reading what it was not given.
    /// Asked only after the files handed in and the engine's own packages, so
    /// a package costs no look at the disk.
    ///
    /// @return The file's bytes, or null when there is no such file.
    using Reader = std::function<const std::string*(std::string_view name)>;

    /// @brief Keeps a file a document writes for itself -- the `refs.bib` in
    ///        its `filecontents` block -- where the #Reader finds it, before
    ///        anything on disk, for the rest of the run.
    ///
    /// Nothing reaches the disk: a run writes no file but its PDF.
    using Writer = std::function<void(std::string_view name, std::string_view text)>;

    /// @brief Everything a primitive needs that is not the expander itself.
    ///
    /// Passed by reference to every module, so a module never owns engine
    /// state and two modules always see the same state.
    ///
    /// @par Prefixes
    /// `\\shared`, `\\spanning` and `\\guarded` -- TeX's `\\global`, `\\long`
    /// and `\\outer` -- and `\\provided` and `\\expanded`, which make LaTeX's
    /// `\\providecommand` and TeX's `\\edef` of an ordinary definition, each
    /// say something about the definition that follows them,
    /// which another module makes. They are kept here, where both sides can
    /// see them: the prefix sets its flag, and whatever defines or assigns
    /// next takes it with `std::exchange(context.global, false)`, so a prefix
    /// never outlives the one definition it was written for.
    struct Context {
        semantics::Registers& registers;    ///< The register bank, scoped by Union.
        Relay& relay;                       ///< Conditionals, for a module that needs to branch.
        const Variables& variables;         ///< Named values, for a module that reads one by name.

        bool global{false};                 ///< The next definition or assignment outlives its group.
        bool spanning{false};               ///< The next macro's arguments may hold `\\par`.
        bool isolated{false};               ///< The next macro may not appear inside an argument.
        bool robust{false};                 ///< The next macro is kept as written by `\\edef`: `\\protected`.
        bool provided{false};               ///< The next definition is made only if the name means nothing yet.
        bool expanded{false};               ///< The next macro's body is expanded as it is defined, as `\\edef` does.

        /// TeX's `\\afterassignment`: a token to read once the next definition
        /// or assignment has been made, whichever module makes it; empty for none.
        Token after{};

        const Files* files{nullptr};        ///< Files handed in from memory, or none.
        Reader disk{};                      ///< Files beside the document on disk, or none.
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