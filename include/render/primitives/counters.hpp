#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"
#include "memory/dictionary.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace render::primitives {

    /// @brief Named counters, as LaTeX keeps them: `\\newcounter`,
    ///        `\\stepcounter`, `\\refstepcounter`, `\\setcounter`,
    ///        `\\addtocounter`, `\\value`, and their printed forms.
    ///
    /// @par Language
    /// @code
    /// \newcounter{widget}                 % a counter of its own
    /// \newcounter{part}[widget]           % reset whenever widget steps
    /// \stepcounter{widget}
    /// There are \arabic{widget} widgets, or \Roman{widget} in Roman numerals.
    /// \setcounter{widget}{10}
    /// \addtocounter{widget}{-1}
    /// \thepart                            % the widget's number, a dot, the part's
    /// \numberwithin{equation}{section}    % equations numbered 2.1, 2.2, ...
    /// @endcode
    ///
    /// A counter may be numbered within another, which resets it each time
    /// that one steps and prints its number after that one's. `\\the<name>`
    /// is a macro like any other -- the parent's `\\the`, a dot, and
    /// `\\arabic{name}` -- so a document redefines it with `\\renewcommand`
    /// as it would in LaTeX, and everything that prints the counter prints
    /// what the document said. `\\arabic` and its kin expand to the digits
    /// themselves, so a counter can be read inside `\\evaluate` or `\\ifnum`.
    ///
    /// Other modules number through this one too: headings, equations,
    /// theorems and captions each step a counter here and print it through
    /// its `\\the` macro, so `\\setcounter{section}{4}` and
    /// `\\renewcommand{\\theequation}{...}` mean what LaTeX means by them.
    class Counters {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Counters(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the counter primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; unused beyond what every module takes.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Creates a counter, or moves one that exists under a new parent.
        ///
        /// Defines `\\the<name>` as its printed form, which a document may
        /// then redefine.
        ///
        /// @param mouth   Expander, where `\\the<name>` is defined.
        /// @param name    The counter.
        /// @param parent  The counter it is numbered within, or empty for none.
        /// @param numeral What prints its own value: `\\arabic`, `\\Roman`, `\\Alph`;
        ///                empty to leave its printed form as it is.
        /// @complexity O(1) amortized, plus the length of the old parent's list.
        void define(syntax::Mouth& mouth, std::string_view name, std::string_view parent = {},
                    std::string_view numeral = "\\arabic") const;

        /// @brief Adds one, and resets every counter numbered within it.
        /// @param name The counter; one that does not exist is created.
        /// @complexity O(n) in the counters numbered within it, however deep.
        void step(std::string_view name) const;

        /// @brief Gives a counter a value outright, resetting nothing.
        /// @param name  The counter; one that does not exist is created.
        /// @param value Its new value.
        void set(std::string_view name, int value) const;

        /// @brief A counter's value, or 0 for one that does not exist.
        [[nodiscard]] int value(std::string_view name) const;

        /// @brief Whether a counter by this name exists.
        [[nodiscard]] bool contains(std::string_view name) const;

        /// @brief A counter as `\\the<name>` prints it, expanded to text.
        /// @param mouth Expander, which expands the macro.
        /// @param name  The counter.
        /// @return What it prints, such as `2.1`.
        [[nodiscard]] static std::string print(syntax::Mouth& mouth, std::string_view name);

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::traceback() gathers them when a run finishes. A module
        /// records an error by appending to the list where it finds it, which
        /// is why there is no reporting function to go looking for.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept {
            return tracebacks;
        }

    private:
        /// @brief One named counter.
        struct Counter {
            int value{0};                        ///< What it stands at.
            std::string parent{};                ///< What it is numbered within, or empty.
            std::vector<std::string> children{}; ///< What is numbered within it.
        };

        /// @brief Defines `\\the<name>`: the parent's `\\the`, a dot, and the
        ///        counter's own value.
        /// @param mouth   Expander to define it in.
        /// @param name    The counter.
        /// @param parent  Its parent, or empty.
        /// @param numeral What prints its value.
        static void spell(syntax::Mouth& mouth, std::string_view name, std::string_view parent,
                          std::string_view numeral);

        /// @brief Sets every counter numbered within one back to zero, and
        ///        every counter numbered within those.
        /// @param name  The counter that stepped.
        /// @param depth How deep this is, so a counter made its own ancestor
        ///              cannot recurse forever.
        void reset(const std::string& name, int depth) const;

        mutable std::vector<syntax::Traceback> tracebacks{};                ///< Errors this module found.
        mutable memory::Dictionary<Counter> counters{};   ///< Every counter, by name.
    };

}
