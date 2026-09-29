#pragma once

#include "render/graphics/color.hpp"
#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace render::primitives {

    /// @brief Colored text, and the colors a document names: `\\textcolor`,
    ///        `\\definecolor` and `\\colorlet`.
    ///
    /// @par Language
    /// @code
    /// \textcolor{#CC0000}{This runs in red.}
    /// \textcolor{red}{So does this.}
    /// \textcolor{rgba(0,0,204,0.5)}{And this in half-opaque blue.}
    ///
    /// \definecolor{brand}{HTML}{FF8800}
    /// \definecolor{ink}{rgb}{0.1,0.1,0.3}
    /// \colorlet{muted}{brand!40}
    /// \textcolor{brand}{In the brand's own orange,}
    /// \textcolor{muted}{then in a paler one,}
    /// \textcolor{red!70!black}{and in a darker red, mixed where it is used.}
    /// @endcode
    ///
    /// A color is written the way xcolor writes one, or the way a web page
    /// does, and both read the same everywhere: a name the document defined,
    /// a built-in name, a hex triplet or quadruplet, `rgb()`/`rgba()`, and
    /// xcolor's mixtures -- `red!30` is thirty percent red on white, and
    /// `red!30!blue` thirty percent red on blue, chained as far as a
    /// document likes.
    ///
    /// `\\definecolor` takes xcolor's models: `HTML`, `RGB` (0--255), `rgb`
    /// and `gray` (0--1), `cmyk` and `cmy`. Each is converted once, when it
    /// is defined, and kept as the variable `color.name` in the form
    /// graphics::Color::parse() reads -- so a program embedding the engine
    /// can set a document's colors the same way, with no markup at all.
    ///
    /// Text that matches no form at all is black, which is what the text
    /// would have been anyway; a model \\definecolor does not know is an
    /// error, since the color it was meant to be cannot be guessed.
    class Colors {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Colors(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the color primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; text is shaped with its selection,
        ///                and named colors are kept among its variables.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded: a color model it does not
        ///        know, or a value that does not fit its model.
        [[nodiscard]] const std::vector<syntax::Traceback>& tracebacks() const noexcept { return tracebacks_; }

        /// @brief Reads a color as a document writes one: a name it defined,
        ///        any form graphics::Color::parse() reads, or an xcolor mixture
        ///        of those.
        ///
        /// Shared with the drawing primitives, so a line and a word read
        /// `brand!40` the same way.
        ///
        /// @param text      The color as written.
        /// @param variables Where the document's own names are kept.
        /// @complexity O(n) in the text's length.
        [[nodiscard]] static graphics::Color resolve(std::string_view text,
                                                     const syntax::primitives::Variables& variables);

        /// @brief Converts a value in one of xcolor's models to the form
        ///        graphics::Color::parse() reads: what `[gray]{0.9}` is,
        ///        wherever a color is written with its model.
        /// @param model The model's name, as `rgb` or `HTML`.
        /// @param value The value, as `0.1,0.2,0.3` or `FF8800`.
        /// @return The color's text, or nothing when the model is unknown or
        ///         the value does not fit it.
        [[nodiscard]] static std::optional<std::string> convert(std::string_view model, std::string_view value);

    private:
        /// @brief A color as text graphics::Color::parse() reads back exactly.
        [[nodiscard]] static std::string write(const graphics::Color& color);

        mutable std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this module found.
    };

}
