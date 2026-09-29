#pragma once

#include "syntax/tokens.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace syntax {

    class Mouth;

    /// @brief What CTAN's packages define, for the commands the engine gives
    ///        no meaning of its own.
    ///
    /// A document written for LaTeX asks for commands from packages the
    /// engine does not carry. Rather than fail on each, the expander asks
    /// here the first time it meets a name with no meaning, and the glossary
    /// -- `assets/glossary.mtex`, compiled in with `#embed` and split by the
    /// compiler into a memory::Catalog -- answers with the command as
    /// `\\@define` takes it:
    ///
    /// @code
    /// \foreignlanguage[3][]{#3}
    /// \microtypesetup[1]{}
    /// \xrightarrow[2][]{\underset{#1}{\overset{#2}{\longrightarrow}}}
    /// @endcode
    ///
    /// That line goes in front of the name, after `\\@shared\\@spanning\\@define`,
    /// so the command is defined for the rest of the run -- outside the group
    /// it was first met in too -- takes a paragraph in an argument, as
    /// LaTeX's `\\newcommand` does, and the name is read again, now meaning it.
    ///
    /// Nothing is read before it is asked for: a document that uses no such
    /// command pays one flag per name it did not know, and one that uses one
    /// pays a single lookup and a line. Each name is asked about once, so a
    /// line that fails to define its command cannot be read over and over.
    class Glossary {
    public:
        /// @brief Gives a command with no meaning the one the glossary holds,
        ///        the first time it is met.
        /// @param mouth The expander the name was read from, still at its
        ///              front.
        /// @param token The name, not yet read off the stream.
        /// @return True when its definition now stands in front of it, to be
        ///         read before the name is read again; false when the name
        ///         was asked about already, is the parser's own, or is not in
        ///         the glossary.
        /// @complexity O(1): a flag, then at most one lookup per name in a run.
        bool define(Mouth& mouth, const Token& token);

        /// @brief The line the glossary holds for a name.
        /// @param name A command's name, backslash and all: `\\selectlanguage`.
        /// @return Its whole line, name first, or std::nullopt when it holds none.
        /// @complexity O(1) expected.
        [[nodiscard]] static std::optional<std::string_view> get(std::string_view name) noexcept;

        /// @brief How many commands it holds.
        [[nodiscard]] static std::size_t size() noexcept;

    private:
        std::vector<bool> tried{};   ///< Names asked about already, by symbol.
    };

}
