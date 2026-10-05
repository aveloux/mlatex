#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"
#include "memory/dictionary.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace syntax::primitives {

    /// @brief Named values a document reads: `\\variable`, `\\setvariable`,
    ///        `\\unsetvariable`, `\\ifvariable`, `\\ifstrequal` to compare
    ///        two pieces of text, and `\\iftest` to weigh a test as ifthen
    ///        writes one.
    ///
    /// This is how a number worked out somewhere else ends up on the page. A
    /// program that calls the engine -- through the foreign-function library
    /// in `ffi.hpp`, or with `--set name=value` on the command line -- hands
    /// it values by name before the document runs, and the document writes
    /// `\\variable{name}` wherever one belongs. An invoice's customer, its
    /// number and its figures can all arrive this way, with the document
    /// itself a template that never changes.
    ///
    /// A value is text, and reading it lexes that text into the document as
    /// if it had been written there: `Paid in full` reads as words,
    /// `\\textbf{Paid}` as bold, `1234.50` as digits a calculation can use.
    /// Every other module reads the same table through Context::variables,
    /// which is how `\\calculate` resolves a name inside an expression.
    ///
    /// @par Language
    /// @code
    /// \variable{customer}                    % the value, read into the document
    /// \setvariable{total}{\calculate{2*3}}   % stored after expanding: "6"
    /// \ifvariable{discount}{yes}{no}         % whichever branch applies
    /// \unsetvariable{discount}               % taken back: \ifvariable says no again
    /// \setkeys{geometry}{margin=1in,a4paper} % geometry.margin and geometry.a4paper
    ///
    /// \ifstrequal{\variable{plan}}{gold}{Welcome back.}{Upgrade today.}
    /// \iftest{\value{page} > 1 \AND \NOT \boolean{draft}}{Continued.}{}
    /// @endcode
    ///
    /// `\\ifstrequal` expands both of its first two arguments before it
    /// compares them, so a value, a macro and plain text all compare by what
    /// they come to -- which is what a package testing a document's own
    /// settings needs, and what etoolbox's toggles and ifthen's `\\equal`
    /// are built on.
    ///
    /// `\\setkeys` is LaTeX's keyval, storing each key as a variable under
    /// the family's name; it is also what `\\usepackage[options]{name}` does
    /// with the options, so a package reads what it was given by name.
    ///
    /// Values are global: a `\\setvariable` inside a group outlives it, since
    /// a running total built up line by line has to. A value set by the
    /// caller and one set by the document share the one table, and a later
    /// setting replaces an earlier one.
    class Variables {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Variables(Lexicon& lexicon) noexcept;

        /// @brief Installs `\\variable`, `\\setvariable`, `\\unsetvariable`,
        ///        `\\ifvariable`, `\\ifstrequal`, `\\iftest` and `\\setkeys`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; unused by this module.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Sets a value, as a calling program does before a document runs.
        /// @param name  What the document calls it.
        /// @param value Its text.
        /// @complexity O(1) average.
        void define(std::string_view name, std::string_view value) const;

        /// @brief Takes a value back, as a calling program does between documents.
        /// @param name What the document calls it; nothing happens when nothing was set.
        /// @complexity O(1) average.
        void unset(std::string_view name) const;

        /// @brief Sets `family.key` for every `key=value` in a list, as
        ///        `\\setkeys` does.
        ///
        /// A bare key -- `landscape`, `utf8` -- is set to `true`. Spaces around
        /// a key or a value are dropped, commas inside braces belong to the
        /// value, and one pair of braces around a whole value is removed, so
        /// `margin={1in}` and `margin=1in` mean the same.
        ///
        /// @param family What every key is prefixed with.
        /// @param list   The pairs, comma-separated.
        /// @complexity O(n) in the list's length.
        void assign(std::string_view family, std::string_view list) const;

        /// @brief A value by name.
        /// @param name What the document calls it.
        /// @return Its text, or nullptr when nothing was set under that name.
        ///         Valid until that name is set again.
        /// @complexity O(1) average.
        [[nodiscard]] const std::string* get(std::string_view name) const noexcept;

        /// @brief Errors this module has recorded: names read that were never set.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        /// Every value, by name. Mutable because the module is installed as a
        /// const callable, as every module is, while its table changes as the
        /// document runs.
        mutable memory::Dictionary<std::string> values{};

        mutable std::vector<Traceback> tracebacks{};   ///< Errors this module found.
    };

}
