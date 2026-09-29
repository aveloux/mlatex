#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace render::primitives {

    /// @brief Font selection: which face the document is set in.
    ///
    /// @par Language
    /// @code
    /// \textfont{lmroman10-regular}      % the face running text is set in
    /// \mathfont{NewCMMath-Regular}      % the face formulas are set in
    ///
    /// \font\big = lmroman12-bold at 18pt   % name a face at a size
    /// \big                                  % and select it
    ///
    /// \@face{main}{TeX Gyre Termes}         % a face by the name the system
    /// \@face{math}{STIX Two Math}           % knows it, set in the nearest one
    /// @endcode
    ///
    /// fontspec's \\setmainfont and \\setmathfont are `\\@face`: a document
    /// that names a system face is set in the one the engine carries nearest
    /// it, and one with nothing near it keeps its face, with a warning.
    ///
    /// A family is whatever the font library indexed it as, which for a tree
    /// of files is each file's name without its extension, matched without
    /// regard to case. A directory's own name is a family too, so `text` and
    /// `expression` work when the tree is laid out that way.
    ///
    /// @par Why two roles
    /// Prose and mathematics want different faces, and a formula set in a text
    /// face has no Greek and no maths table -- which is what produces a page
    /// of empty boxes where the symbols should be. Keeping the two selections
    /// apart is what lets one document have both.
    class Typeface {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Typeface(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the font primitives.
        /// @param parser  Parser whose expander to bind into.
        /// @param context Engine services; faces come from its registry.
        void operator()(syntax::Parser& parser, Context& context) const;

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
        mutable std::vector<syntax::Traceback> tracebacks{};   ///< Errors this module found.
    };

}
