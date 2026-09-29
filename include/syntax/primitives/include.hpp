#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"
#include "memory/dictionary.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace syntax::primitives {

    /// @brief Pulls in another file of the language, and whole packages of
    ///        them: `\\include`, `\\input`, `\\usepackage`, `\\requirepackage`,
    ///        `\\ifpackageloaded`, `\\providepackage`.
    ///
    /// The engine embeds every `.mtex` file under `src/modules` into the
    /// binary at build time, keyed by its path below that directory; this is
    /// how a document -- or the prelude itself -- reaches one by name rather
    /// than the driver having to know every file that exists.
    ///
    /// @par Packages
    /// A package is a folder under `src/modules`, and the folder's own
    /// `main.mtex` is its wrapper: the one file `\\usepackage` reads, which
    /// reaches the rest of the package with `\\include`. A package is named
    /// the way LaTeX names it -- `\\usepackage{amsmath}` reads
    /// `amsmath/main.mtex` -- so a preamble written for LaTeX loads as it
    /// was written. Options in brackets become the package's variables --
    /// `\\usepackage[margin=2cm]{geometry}` sets `geometry.margin` -- for the
    /// package to read with `\\variable` and `\\ifvariable`.
    ///
    /// A package whose whole job the engine already does -- inputenc,
    /// graphicx, xcolor -- has no folder at all: the core package marks it
    /// loaded with `\\providepackage`, so asking for it reads nothing and
    /// reports nothing.
    ///
    /// A package is read once, however many times it is asked for. That is
    /// what lets packages depend on each other: one that builds on another
    /// says so with `\\requirepackage`, and neither the document nor a third
    /// package that asked for the same one causes it to be read twice. A
    /// package can also ask whether another has been loaded, and do more or
    /// less depending on the answer; with named hooks (Hooks) and named
    /// values (Variables) that is how packages talk to each other.
    ///
    /// @par Language
    /// @code
    /// \usepackage{amsmath}                 % reads amsmath/main.mtex, once
    /// \usepackage[utf8]{inputenc}          % options are accepted
    /// \usepackage{amssymb,graphicx}        % several at once, in that order
    /// \requirepackage{xcolor}              % the same, from inside a package
    /// \ifpackageloaded{hyperref}{yes}{no}  % whichever branch applies
    ///
    /// \include{titling}                    % a file, by name
    /// \input{titling.mtex}                 % TeX's own name for the same thing
    /// @endcode
    ///
    /// @par Finding a file
    /// `\\include` looks first in the folder of the package being read, then
    /// from the top of `src/modules`, each time as written and then with
    /// `.mtex` added -- so a package's wrapper reaches its own files by their
    /// bare names. A name that is a package's folder reads that package, once,
    /// as `\\usepackage` would.
    class Include {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Include(Lexicon& lexicon) noexcept;

        /// @brief Installs every file and package primitive.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services: the variables a package's options
        ///                become, and the files handed in from memory.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Has a package been loaded -- or begun loading -- already?
        /// @param name The package, as `\\usepackage` names it.
        /// @complexity O(1) average.
        [[nodiscard]] bool loaded(std::string_view name) const;

        /// @brief Errors this module has recorded: a name found nowhere embedded.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept {
            return tracebacks;
        }

    private:
        /// @brief Reads one embedded file into the stream, as part of a folder.
        ///
        /// The folder is taken as the current one for as long as the file is
        /// being read, which is what lets an `\\include` inside it find its
        /// neighbours by their bare names. Its tokens are the next ones read,
        /// so the folder is entered now and left by a marker placed after
        /// them -- a name the lexer can never produce, bound to nothing but
        /// leaving it.
        ///
        /// @param mouth  Expander to read into.
        /// @param key    The file's path below `src/modules`.
        /// @param text   The file's contents.
        /// @complexity O(n) in the file's length, to lex it.
        void read(Mouth& mouth, std::string_view key, std::string_view text) const;

        /// @brief Loads a package by name, unless it was loaded already.
        /// @param mouth   Expander to read into.
        /// @param context Engine services, for the files handed in.
        /// @param name    The package, as `\\usepackage` names it.
        /// @param origin  Where it was asked for, for a report.
        void load(Mouth& mouth, const Context& context, std::string_view name, memory::Location origin) const;

        /// @brief Finds a file by the key it is stored under.
        ///
        /// One handed in from memory first, then one built into the engine,
        /// so a program's own `letterhead/main.mtex` -- or its own
        /// `hyperref/main.mtex`, replacing the engine's -- is found by the
        /// same \\usepackage a built-in one is.
        ///
        /// @param context Engine services; its files are the ones handed in.
        /// @param key     Path below the modules directory, as `core/main.mtex`.
        /// @return The file's text, or nothing when neither has it.
        /// @complexity O(log n) in the files handed in, then O(1).
        [[nodiscard]] static std::optional<std::string_view> get(const Context& context, std::string_view key);

        /// @brief The names in a comma-separated list, trimmed, empty ones left out.
        /// @param list As `amsmath, amssymb`.
        /// @complexity O(n) in the list's length.
        [[nodiscard]] static std::vector<std::string> split(std::string_view list);

        Symbol leave{};   ///< The marker that ends a file's folder; see read().

        /// The folder of each file being read, innermost last; empty for the
        /// top of `src/modules`.
        mutable std::vector<std::string> folders{};

        mutable memory::Names packages{};   ///< Every package loaded so far.
        mutable std::vector<Traceback> tracebacks{};         ///< Errors this module found.
    };

}
