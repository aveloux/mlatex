#pragma once

#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <vector>

namespace render::primitives {

    /// @brief HTTP requests, read straight into the document: `\\httpget`,
    ///        `\\httppost`.
    ///
    /// @par Language
    /// @code
    /// \httpget{https://example.com/status}
    /// \httppost{https://example.com/echo}{hello}
    /// @endcode
    ///
    /// The same network::compose() a document's own `\includegraphics`
    /// reaches for its images reaches here too, generalized to whichever
    /// method a document names; the response body is set as plain text
    /// exactly where the command stood, the way `\includegraphics` sets an
    /// image where it stood.
    ///
    /// Nothing here parses the body: a JSON or HTML response is typeset as
    /// the literal characters it arrived as, which is correct for a status
    /// string or a short echo and a poor idea for anything longer -- a
    /// document that wants to interpret a response has to do that itself.
    class Requests {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Requests(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the request primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; unused beyond what every module takes.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::tracebacks() gathers them when a run finishes.
        [[nodiscard]] const std::vector<syntax::Traceback>& tracebacks() const noexcept {
            return tracebacks_;
        }

    private:
        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.
    };

}
