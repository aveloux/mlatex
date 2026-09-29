#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"
#include "memory/dictionary.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace render::primitives {

    /// @brief Citations and the list they cite: `\\cite` in every form LaTeX,
    ///        natbib and biblatex give it, `\\bibitem` and `thebibliography`,
    ///        and BibTeX's own work -- a `.bib` file read, sorted and set in a
    ///        style.
    ///
    /// @par Language
    /// @code
    /// As shown earlier \cite{knuth}, ...          % [1]
    /// ... and since \cite{knuth, lamport}.         % [1, 2]
    /// See \cite[p.~5]{lamport}.                    % [2, p. 5]
    ///
    /// \usepackage{natbib}                          % with an author-year style:
    /// \citet{knuth} shows ... \citep[p.~5]{knuth}   % Knuth (1984) ... (Knuth, 1984, p. 5)
    ///
    /// \bibliographystyle{plainnat}
    /// \bibliography{refs}                           % refs.bib, beside the document
    ///
    /// \usepackage[style=authoryear]{biblatex}      % or biblatex's own names:
    /// \addbibresource{refs.bib}
    /// \textcite{knuth} ... \parencite{knuth}
    /// \printbibliography
    /// @endcode
    ///
    /// A list written into the document is set as it is: each `\\bibitem` an
    /// entry, numbered by where it stands, or labelled as its bracket says.
    /// natbib's label `Knuth(1984)` gives an entry an author and a year, and
    /// an entry that has them is cited by them in natbib's forms.
    ///
    /// @par BibTeX
    /// `\\bibliography` and `\\printbibliography` do BibTeX's work where they
    /// stand: the named `.bib` files are read, the entries cited so far --
    /// or every one, after `\\nocite{*}` -- are sorted and set in the style
    /// `\\bibliographystyle` named, and the list is read as if the document
    /// had written it. The styles are BibTeX's standard ones: `plain` (sorted,
    /// numbered), `unsrt` (in the order cited), `abbrv` (initials), `alpha`
    /// (labels made of names and year), `ieeetr`, and natbib's `plainnat`,
    /// `unsrtnat` and `abbrvnat`, whose entries carry author and year.
    ///
    /// @par Forward references
    /// A `\\cite` may come before the `\\bibitem` it names, in which case its
    /// text is filled in once that `\\bibitem` is read -- the same
    /// wait-and-fill scheme References uses for `\\label` and `\\ref`.
    class Citations {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Citations(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the citation primitives and registers `thebibliography`.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; the block is hooked through its block module.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded: a key reused, a `.bib`
        ///        file missing, a key cited that no list holds.
        [[nodiscard]] const std::vector<syntax::Traceback>& tracebacks() const noexcept {
            return tracebacks_;
        }

    private:
        /// @brief How a citation is written.
        enum class Form : std::uint8_t {
            Plain,           ///< `\\cite`: numbers in brackets, or natbib's textual form.
            Parenthetical,   ///< `\\citep`, `\\parencite`: (Knuth, 1984), or [1].
            Textual,         ///< `\\citet`, `\\textcite`: Knuth (1984), or Knuth [1].
            Author,          ///< `\\citeauthor`: Knuth.
            Year,            ///< `\\citeyear`: 1984.
            Loose,           ///< `\\citealt`: Knuth 1984.
            Comma,           ///< `\\citealp`: Knuth, 1984.
            Number           ///< `\\citenum`: 1.
        };

        /// @brief One citation key: how it is cited, once a list has it, and
        ///        every citation still waiting for it.
        struct Mark {
            std::string text{};                         ///< Its number or label.
            std::string author{};                       ///< natbib's author, when the entry has one.
            std::string year{};                         ///< natbib's year, the same way.
            bool defined{false};                        ///< Whether a `\\bibitem` has claimed it.
            std::vector<std::size_t> references{};      ///< Citations, by index, to set again once it is.
            std::size_t anchor{0};                      ///< Where its entry stands, for a link to it.
            std::vector<layout::Node*> links{};         ///< hyperref's links to it made before it was, to point there.
        };

        /// @brief One citation: the node that prints it, the keys it names in
        ///        order, its notes, and its form.
        struct Citation {
            syntax::Node* node{nullptr};       ///< Text node holding what it prints.
            std::vector<std::string> keys{};   ///< The keys, spaces trimmed.
            std::string before{};              ///< natbib's first bracket of two: `see`.
            std::string note{};                ///< Its last bracket: `p.~5`.
            Form form{Form::Plain};            ///< How it is written.
        };

        /// @brief What a citation prints: numbers or labels in brackets, or
        ///        authors and years as natbib sets them when every key it
        ///        names has both and natbib is in use without `numbers`; a
        ///        question mark for a key no list has claimed yet.
        /// @param citation The citation.
        /// @param context  Engine services, for the packages' settings.
        /// @return Its text.
        /// @complexity O(k) in its keys.
        [[nodiscard]] std::string print(const Citation& citation, const Context& context) const;

        mutable memory::Dictionary<Mark> marks{};   ///< Every key seen or cited.
        mutable std::vector<Citation> citations{};  ///< Every citation, in order.
        mutable std::vector<std::string> resources{};   ///< biblatex's \\addbibresource files.
        mutable std::vector<std::string> extra{};       ///< Keys \\nocite adds; `*` for every entry.
        mutable int entries{0};                                  ///< How many `\\bibitem`s so far.
        mutable float widest{0.0f};                              ///< The widest label, from the list's argument, in points.
        mutable bool hung{false};                                ///< The list is set without labels, each entry hung: natbib's author-year list.
        mutable std::optional<std::string> heading{};            ///< The list's heading as \\printbibliography's options give it, once.
        mutable std::vector<syntax::Traceback> tracebacks_{};    ///< Errors this module found.

        /// The gap between an entry's "[N]" label and its text -- LaTeX's
        /// `\\labelsep` -- in ems, and the space between two entries,
        /// `\\itemsep` and `\\parsep`, in points at ten point.
        static constexpr float separation = 0.5f;
        static constexpr float between = 8.0f;
    };

}
