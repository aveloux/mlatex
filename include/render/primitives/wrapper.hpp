#pragma once

#include "render/primitives/algorithms.hpp"
#include "render/primitives/boxes.hpp"
#include "render/primitives/citations.hpp"
#include "render/primitives/colors.hpp"
#include "render/primitives/context.hpp"
#include "render/primitives/counters.hpp"
#include "render/primitives/expressions.hpp"
#include "render/primitives/floats.hpp"
#include "render/primitives/footnotes.hpp"
#include "render/primitives/groups.hpp"
#include "render/primitives/lists.hpp"
#include "render/primitives/page.hpp"
#include "render/primitives/paragraphs.hpp"
#include "render/primitives/requests.hpp"
#include "render/primitives/penalties.hpp"
#include "render/primitives/plots.hpp"
#include "render/primitives/diagrams.hpp"
#include "render/primitives/illustrations.hpp"
#include "render/primitives/languages.hpp"
#include "render/primitives/references.hpp"
#include "render/primitives/rules.hpp"
#include "render/primitives/sections.hpp"
#include "render/primitives/spacing.hpp"
#include "render/primitives/styles.hpp"
#include "render/primitives/symbols.hpp"
#include "render/primitives/tables.hpp"
#include "render/primitives/theorems.hpp"
#include "render/primitives/typeface.hpp"
#include "render/primitives/verbatim.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace render::primitives {

    /// @brief Every render primitive, installed in one call.
    ///
    /// Each module is a Primitive -- a callable taking
    /// `(syntax::Parser&, Context&)` -- so this type holds them and folds over
    /// them. Adding a module is a member and a name in the pack; nothing
    /// declares or calls an `ingest`.
    ///
    /// This is deliberately the same shape as
    /// syntax::primitives::Wrapper. The two layers install into different
    /// things -- one into the expander, one into the parser -- but a module is
    /// a plain callable in both, errors are a list per module in both, and
    /// dispatch is an index on the interned symbol in both.
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
    /// render::primitives::Wrapper visuals(lexicon);
    /// render::primitives::Context context{
    ///     document, typesetter, registers, registry, collection,
    ///     shaper, unicodes, core.blocks, core.variables, arena, selection
    /// };
    /// visuals(parser, context);
    ///
    /// // ... run the document, then collect what the layer reported ...
    /// for (const auto& fault : visuals.traceback()) {
    ///     std::cerr << fault.format() << '\n';
    /// }
    /// @endcode
    class Wrapper {
    public:
        /// @brief Constructs every module against one interning table.
        /// @param lexicon Interning table, shared with the expander.
        explicit Wrapper(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs every module.
        /// @param parser  Parser to bind into.
        /// @param context Engine services, passed through to each module.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Every error the layer reported, gathered from each module.
        ///
        /// Modules keep their own lists, so nothing has to be constructed and
        /// threaded through them. This merges those lists on demand, which is
        /// a once-per-run cost rather than a per-error one.
        ///
        /// @return A fresh vector holding every module's tracebacks.
        /// @complexity O(n) in the number of errors reported.
        [[nodiscard]] std::vector<syntax::Traceback> traceback() const;

    private:
        Page page;             ///< `\\documentclass`, the sheet and the margins.
        Typeface typeface;     ///< `\\textfont`, `\\mathfont` and `\\font`.
        Styles styles;         ///< `\\textbf`, `\\emph`, `\\large` and the rest.
        Symbols symbols;       ///< The characters a document cannot type for itself.
        Groups groups;         ///< The brace group, and the scope it bounds.
        Boxes boxes;           ///< `\\hbox`, `\\vbox` and `\\vtop`.
        Spacing spacing;       ///< Glue and kerns.
        Rules rules;           ///< `\\rule`, `\\hrule` and `\\vrule`.
        Penalties penalties;   ///< `\\penalty` and the forced breaks.
        Tables tables;         ///< `\\halign`.
        Sections sections;     ///< `\\section` and the levels beneath it.
        Paragraphs paragraphs; ///< `\\noindent`, `\\centering` and the alignment blocks.
        Lists lists;             ///< `\\item` and the three list blocks.
        Expressions expressions; ///< `$`, `\\(`, `\\[`, `equation` and the matrices.
        References references;  ///< `\\label`, `\\ref` and `\\eqref`.
        Citations citations;   ///< `\\cite`, `\\bibitem` and `thebibliography`.
        Footnotes footnotes;   ///< `\\footnote`.
        Counters counters;     ///< `\\newcounter`, `\\stepcounter`, `\\setcounter` and their printed forms.
        Theorems theorems;     ///< `\\newtheorem`, `\\theoremstyle`, the proof block and `\\qed`.
        Floats floats;         ///< `figure`, `table`, `algorithm`, `\\caption` and `\\captionof`.
        Algorithms algorithms; ///< The `algorithmic` block, `\\State` and `\\Statex`.
        Illustrations illustrations;     ///< The `picture`/`overlay` blocks, their line primitives, and `\\includegraphics`/`\\caption`.
        Colors colors;         ///< `\\textcolor`, `\\definecolor`, `\\colorlet`.
        Requests requests;      ///< `\\httpget`, `\\httppost`.
        Verbatim verbatim;      ///< `\\verb`, `verbatim`, `lstlisting` and `\\lstinline`.
        Plots plots;            ///< pgfplots' axes and what `\\addplot` draws in them.
        Diagrams diagrams;      ///< tikz-cd's, xy-pic's and amscd's diagrams, circuits and trees, as TikZ.
        Languages languages;    ///< A language's patterns, direction and captions, and `otherlanguage`.
    };

    static_assert(Primitive<Wrapper>, "Wrapper must itself be installable as a module");

}
