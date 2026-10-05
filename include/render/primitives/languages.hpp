#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"
#include "typography/hyphenator.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render::primitives {

    /// @brief Languages: how a language's words break, which way its
    ///        paragraphs read, and the words LaTeX prints in it.
    ///
    /// What babel and polyglossia do, underneath their commands. A language
    /// is chosen by the name either package knows it by -- `russian`,
    /// `ngerman`, `arabic` -- and choosing it does three things. Its words
    /// break by its own patterns, TeX's `hyph-*.pat.txt`, read from the
    /// assets the first time the language is chosen, no nearer a word's ends
    /// than it allows. Its paragraphs read right to left when it is written
    /// so, Arabic, Hebrew and Persian. And its captions and its date --
    /// `\\abstractname`, `\\figurename`, `\\today` -- are said its way, from
    /// the file of them babel's module keeps for it, read the first time.
    ///
    /// Each is an instruction in its place among the material, since the
    /// words it applies to are broken after the whole document is read.
    ///
    /// @par Language
    /// @code
    /// \@language{english,russian}     % the last named is the one chosen
    /// \@hyphenation{russian}          % its patterns alone, for a phrase
    /// \@direction{RTL}                % paragraphs from here read right to left
    /// \@languagecommands{arabic}      % \textarabic{...} and \begin{Arabic}
    ///
    /// \begin{otherlanguage}{arabic} ... \end{otherlanguage}
    /// \begin{otherlanguage*}{russian} ... \end{otherlanguage*}
    /// @endcode
    ///
    /// @par Reported
    /// A language it does not know, whose words are then broken as the
    /// language before it broke them and whose captions stay as they were.
    class Languages {
    public:
        /// @brief One language: what babel and polyglossia call it, and
        ///        what choosing it sets.
        struct Language {
            std::string_view name;       ///< Its name as a package option: `ngerman`.
            std::string_view captions;   ///< The file of its captions and date, under `babel/`; empty for none.
            std::string_view patterns;   ///< Its patterns' file, between `hyph-` and `.pat.txt`; empty for none.
            std::uint8_t left;           ///< Least letters a break leaves: `\\lefthyphenmin`.
            std::uint8_t right;          ///< Least it carries over: `\\righthyphenmin`.
            bool reversed;               ///< Whether it reads right to left.
        };

        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Languages(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the language primitives and the `otherlanguage` blocks.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the patterns are read from its folder of them.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief A language by any of the names it goes by.
        /// @param name Its name, as a document writes it.
        /// @return The language, or null for one not known.
        /// @complexity O(n) in the languages known, which is a few dozen.
        [[nodiscard]] static const Language* get(std::string_view name) noexcept;

        /// @brief What was reported while the document was read.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        /// Every language's patterns read so far, each kept for the whole
        /// run, since the words set in it point at it until the end.
        mutable std::deque<typography::Hyphenator> hyphenators{};
        mutable std::vector<std::string_view> read{};          ///< Which patterns each of #hyphenators holds.
        mutable std::vector<std::string_view> captioned{};     ///< The caption files already read.
        mutable std::string current{"english"};                ///< The language chosen.
        mutable std::vector<std::string> kept{};               ///< What each open language block will go back to.
        mutable std::vector<std::string_view> watched{};       ///< The languages polyglossia's commands made a block for.

        /// The numerals each language was told to be written with, as
        /// polyglossia's `numerals=` names them: `maghrib` or `western` for
        /// the digits as typed, `mashriq` or `eastern` for its own.
        mutable std::vector<std::pair<std::string, std::string>> numerals{};
        mutable std::vector<syntax::Traceback> tracebacks{};  ///< What was reported.
    };

}
