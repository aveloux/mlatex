#pragma once

#include "render/primitives/structure/algorithms.hpp"
#include "render/primitives/spacing/boxes.hpp"
#include "render/primitives/referencing/citations.hpp"
#include "render/primitives/text/colors.hpp"
#include "render/primitives/context.hpp"
#include "render/primitives/referencing/counters.hpp"
#include "render/primitives/mathematics/formulas.hpp"
#include "render/primitives/structure/floats.hpp"
#include "render/primitives/structure/footnotes.hpp"
#include "render/primitives/spacing/groups.hpp"
#include "render/primitives/structure/lists.hpp"
#include "render/primitives/structure/pages.hpp"
#include "render/primitives/spacing/paragraphs.hpp"
#include "render/primitives/network/requests.hpp"
#include "render/primitives/spacing/penalties.hpp"
#include "render/primitives/pictures/plots.hpp"
#include "render/primitives/pictures/diagrams.hpp"
#include "render/primitives/pictures/illustrations.hpp"
#include "render/primitives/text/languages.hpp"
#include "render/primitives/referencing/references.hpp"
#include "render/primitives/spacing/rules.hpp"
#include "render/primitives/structure/sections.hpp"
#include "render/primitives/spacing/glue.hpp"
#include "render/primitives/text/styles.hpp"
#include "render/primitives/text/characters.hpp"
#include "render/primitives/structure/tables.hpp"
#include "render/primitives/structure/theorems.hpp"
#include "render/primitives/text/typefaces.hpp"
#include "render/primitives/text/verbatim.hpp"
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
    ///     shaper, unicodes, core.environments, core.variables, arena, selection
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
        explicit Wrapper(syntax::Lexicon &lexicon) noexcept;

        /// @brief Installs every module.
        /// @param parser  Parser to bind into.
        /// @param context Engine services, passed through to each module.
        void operator()(syntax::Parser &parser, Context &context) const;

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
        structure::Pages pages; ///< `\\documentclass`, the sheet and the margins.
        text::Typefaces typefaces; ///< `\\textfont`, `\\mathfont` and `\\font`.
        text::Styles styles; ///< `\\textbf`, `\\emph`, `\\large` and the rest.
        text::Characters characters; ///< The characters a document cannot type for itself.
        spacing::Groups groups; ///< The brace group, and the scope it bounds.
        spacing::Boxes boxes; ///< `\\hbox`, `\\vbox` and `\\vtop`.
        spacing::Glue glue; ///< Glue and kerns.
        spacing::Rules rules; ///< `\\rule`, `\\hrule` and `\\vrule`.
        spacing::Penalties penalties; ///< `\\penalty` and the forced breaks.
        structure::Tables tables; ///< `\\halign`.
        structure::Sections sections; ///< `\\section` and the levels beneath it.
        spacing::Paragraphs paragraphs; ///< `\\noindent`, `\\centering` and the alignment blocks.
        structure::Lists lists; ///< `\\item` and the three list blocks.
        mathematics::Formulas formulas; ///< `$`, `\\(`, `\\[`, `equation` and the matrices.
        referencing::References references; ///< `\\label`, `\\ref` and `\\eqref`.
        referencing::Citations citations; ///< `\\cite`, `\\bibitem` and `thebibliography`.
        structure::Footnotes footnotes; ///< `\\footnote`.
        referencing::Counters counters; ///< `\\newcounter`, `\\stepcounter`, `\\setcounter` and their printed forms.
        structure::Theorems theorems; ///< `\\newtheorem`, `\\theoremstyle`, the proof block and `\\qed`.
        structure::Floats floats; ///< `figure`, `table`, `algorithm`, `\\caption` and `\\captionof`.
        structure::Algorithms algorithms; ///< The `algorithmic` block, `\\State` and `\\Statex`.
        pictures::Illustrations illustrations;
        ///< The `picture`/`overlay` blocks, their line primitives, and `\\includegraphics`/`\\caption`.
        text::Colors colors; ///< `\\textcolor`, `\\definecolor`, `\\colorlet`.
        network::Requests requests; ///< `\\httpget`, `\\httppost`.
        text::Verbatim verbatim; ///< `\\verb`, `verbatim`, `lstlisting` and `\\lstinline`.
        pictures::Plots plots; ///< pgfplots' axes and what `\\addplot` draws in them.
        pictures::Diagrams diagrams; ///< tikz-cd's, xy-pic's and amscd's diagrams, circuits and trees, as TikZ.
        text::Languages languages; ///< A language's patterns, direction and captions, and `otherlanguage`.
    };

    static_assert(Primitive<Wrapper>, "Wrapper must itself be installable as a module");
}
