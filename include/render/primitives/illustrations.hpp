#pragma once

#include "render/graphics/canvas.hpp"
#include "render/graphics/color.hpp"
#include "render/graphics/drawing.hpp"
#include "render/graphics/image.hpp"
#include "render/graphics/projection.hpp"
#include "render/primitives/context.hpp"
#include "syntax/parser.hpp"
#include "syntax/traceback.hpp"

#include <array>
#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace render::primitives {

    /// @brief Line drawing: the `picture`, `overlay` and `tikzpicture`
    ///        environments and what they hold.
    ///
    /// @par Language
    /// @code
    /// \begin{picture}{120pt}{80pt}
    ///     \linecolor{#CC0000}
    ///     \line{10pt}{10pt}{110pt}{70pt}
    ///
    ///     \linecolor{blue}\linestyle{dashed}\lineopacity{0.4}
    ///     \line{10pt}{70pt}{110pt}{10pt}
    ///
    ///     \isometric                                   % or \planar, or \orthographic{e}{a}
    ///     \linecolor{rgb(0,150,0)}\linestyle{solid}\lineweight{1.2pt}\lineopacity{1}
    ///     \spaceline{0pt}{0pt}{0pt}{40pt}{0pt}{0pt}
    ///     \spaceline{0pt}{0pt}{0pt}{0pt}{40pt}{0pt}
    ///     \spaceline{0pt}{0pt}{0pt}{0pt}{0pt}{40pt}
    /// \end{picture}
    ///
    /// \begin{overlay}{0pt}{0pt}{612pt}{20pt}   % across the very top of the page
    ///     \line{0pt}{0pt}{612pt}{0pt}
    /// \end{overlay}
    /// @endcode
    ///
    /// A picture is its own coordinate system: x right, y up, origin at its
    /// own bottom left corner, the way graph paper is read rather than the
    /// way the page itself runs. `\\linecolor`, `\\lineopacity`, `\\linestyle`,
    /// `\\lineweight` and the view commands set how the *next* `\\line` or
    /// `\\spaceline` draws; none of them draws anything by themselves, and
    /// each stays set until the next one changes it.
    ///
    /// An overlay is the same picture, placed differently: its own two extra
    /// numbers are where its bottom left corner sits on the *page* -- from
    /// the sheet's own top left corner, margins and indentation both set
    /// aside -- and it takes no room in the document's flow at all, so
    /// nothing after it moves down to make way for it. That is what lets one
    /// sit in a margin, or run across the top of the page before anything
    /// else has been placed there, rather than only ever inside the column
    /// the text itself is set in.
    ///
    /// A color is read by render::graphics::Color::parse(): a hex triplet or
    /// quadruplet, `rgb()`/`rgba()`, or a name. Writing alpha into the color
    /// itself -- `rgba(0,0,0,0.4)` -- and setting `\\lineopacity` separately
    /// are the same knob; a color written without its own alpha leaves
    /// whatever `\\lineopacity` last set rather than opaquing it back.
    ///
    /// Everything here reduces to render::graphics::Canvas::line() and
    /// compose(): this module is the macro surface over it and holds no
    /// drawing logic of its own. A picture or an overlay is its own block,
    /// so a document may hold several without one's lines leaking into
    /// another's. A picture's box grows to hold whatever is drawn past its
    /// declared size, so a drawing never lies over the text around it.
    ///
    /// @par TikZ
    /// @code
    /// egin{tikzpicture}[scale=1.5]
    ///     \draw[thick, ->] (0,0) -- (2,0);
    ///     \draw[blue!60, dashed] (0,0) rectangle (1,1);
    ///     \draw (0,0) -- (1,1) -- (2,0) -- cycle;
    ///     \draw[red] (1,1) circle (0.5);
    ///     \draw[very thin, gray] (0,0) grid (2,2);
    /// \end{tikzpicture}
    /// @endcode
    ///
    /// A TikZ picture takes exactly the room its drawing does, with no size
    /// to declare. `\draw` works in a plain picture too, in the same
    /// centimetres; see draw() for everything a path may say.
    ///
    /// @par Images
    /// @code
    /// \begin{figure}
    ///     \includegraphics{plot.png}{240pt}{180pt}
    ///     \caption{A plot, read from a file beside the document.}
    ///     \label{fig:plot}
    /// \end{figure}
    ///
    /// \includegraphics{https://example.com/diagram.webp}{300pt}{200pt}
    ///
    /// \includegraphics[width=0.5\textwidth]{plot.png}   % LaTeX's graphicx form
    /// \includegraphics[height=4cm]{plot.png}            % the other side follows
    /// \includegraphics[scale=0.5]{plot.png}             % half its natural size
    /// \includegraphics{plot.png}                        % its natural size
    /// @endcode
    ///
    /// The bracketed form is LaTeX's own. `width` and `height` take any
    /// dimension, or a fraction of `\\textwidth`, `\\linewidth`,
    /// `\\columnwidth` or `\\textheight`; given one, the other keeps the
    /// picture's proportions, and given both with `keepaspectratio` the
    /// picture is fitted inside them. A picture's natural size is a point per
    /// pixel, as pdfTeX takes one with no resolution of its own.
    ///
    /// A source is a path on disk -- resolved against the working
    /// directory, the way any relative path is -- or a `http://`/`https://`
    /// URL, fetched with WinHTTP; either way the bytes are sniffed for a
    /// PNG, JPEG or WebP signature and decoded by whichever of libpng,
    /// libjpeg-turbo or libwebp reads that one, into the plain pixels a
    /// Node::Bitmap draws. A figure's caption is the Floats module's.
    class Illustrations {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Illustrations(syntax::Lexicon& lexicon) noexcept;

        /// @brief Installs the picture primitives.
        /// @param parser  Parser to bind into.
        /// @param context Engine services; used for its Blocks module and arena.
        void operator()(syntax::Parser& parser, Context& context) const;

        /// @brief Errors this module has recorded.
        ///
        /// Each module keeps its own list rather than sharing one, so nothing
        /// has to be constructed and threaded through them, and
        /// Wrapper::traceback() gathers them when a run finishes.
        [[nodiscard]] const std::vector<syntax::Traceback>& traceback() const noexcept {
            return tracebacks;
        }

    private:

        /// Parser-level primitive `\\end{picture}` injects, so the finished
        /// canvas lands in the document at the point compose() actually
        /// walks the parse tree, rather than at the point it was expanded --
        /// see Footnotes for why a Mouth-level hook cannot append it directly.
        static constexpr std::string_view flush = "\\pictureflush";

        /// @brief Draws one TikZ path onto the innermost open canvas.
        ///
        /// Coordinates are TikZ's: `(x,y)`, a number without a unit being a
        /// centimetre; `(angle:radius)`; `+(dx,dy)` from the last point, and
        /// `++(dx,dy)` moving it. Between points, `--` and `to` draw a line,
        /// `|-` and `-|` a right angle, `rectangle` a box and `grid` graph
        /// paper; `cycle` closes the path, `circle (r)` and `arc (a:b:r)` go
        /// round. Options set the color -- any a document can name, mixtures
        /// included -- the width by TikZ's names or `line width=`, dashes, and
        /// arrow tips.
        ///
        /// `node[right] {$x$}` sets text at the point the path has reached --
        /// or, with `midway`, halfway along the last line drawn -- typeset as
        /// any text is, formulas included, and placed by the side of it the
        /// options name: `right`, `above left`, `anchor=north`, or centred on
        /// the point with none. `at (x,y)` puts it elsewhere, which is how
        /// `\node` reads.
        ///
        /// @param parser  Parser a node's text is read with.
        /// @param options The bracketed options, as written; may be empty.
        /// @param path    The path, up to its semicolon.
        /// @param origin  Where it was written, for a report.
        /// @param context Engine services: the variables a named color is kept in.
        /// @param stroked False for `\path` and `\fill`, which draw no outline.
        /// @param filled  True for `\fill` and `\filldraw`, which fill the inside.
        /// @complexity O(n) in the path's length, plus the segments a curve
        ///             or a grid is drawn with.
        void draw(syntax::Parser& parser, std::string_view options, std::string_view path, memory::Location origin,
                  Context& context, bool stroked, bool filled) const;

        /// @brief A TikZ node or coordinate given a name: where its outline's
        ///        centre is, how far the outline reaches either way, and
        ///        whether it is round.
        struct Landmark {
            graphics::Point2 centre{};   ///< Its centre, in the picture's own coordinates.
            float across{0.0f};          ///< Half its width, its inner sep included.
            float up{0.0f};              ///< Half its height, likewise.
            bool round{false};           ///< A circle or an ellipse rather than a rectangle.
        };

        mutable std::vector<syntax::Traceback> tracebacks{};       ///< Errors this module found.
        mutable std::unordered_map<std::string, Landmark> landmarks{};   ///< The open picture's named nodes and coordinates.
        mutable std::unordered_map<std::string, std::string> styles{};   ///< TikZ's styles by name: \\tikzset's and a picture's own.
        mutable std::vector<float> distances{};                    ///< Each open picture's `node distance`, in points.
        mutable std::vector<std::string> folders{};                 ///< Where \\graphicspath says pictures are, each ending in a slash.
        mutable std::vector<graphics::Canvas> canvases{};           ///< Open pictures, outermost first.

        // Parallel to #canvases: how much room each open one takes -- an
        // overlay the room it declared, pinned where #anchors says; a picture
        // its declared room, grown to hold its drawing; a TikZ picture just
        // its drawing -- and how much its TikZ coordinates are scaled by.
        mutable std::vector<graphics::Canvas::Room> rooms{};
        mutable std::vector<layout::Node::Point> anchors{};
        mutable std::vector<float> scales{};
        // And the height each stands on the baseline at: TikZ's `baseline`
        // option as written -- a length, or a node and its anchor, `([yshift=-.5ex]cd.center)`
        // -- or nothing for its bottom.
        mutable std::vector<std::string> baselines{};

        mutable graphics::Color color{graphics::black};             ///< Current stroke color.
        mutable float weight = 1.0f;                                ///< Current stroke width, in points.
        mutable bool dashed = false;                                ///< Current stroke style.
        mutable graphics::Projection view = graphics::Projection::isometric();   ///< Current 3D view.
        mutable layout::Node* pending = nullptr;                    ///< The picture just closed, awaiting flush.

        // A deque, not a vector: every decoded image is kept for the whole
        // run, since a Node::Bitmap points straight into one of these rather
        // than copying its pixels, and a vector's own growth would move them
        // out from under every pointer already handed out.
        mutable std::deque<graphics::Image> images{};

        /// Each picture already decoded, by where its bytes lie -- a file
        /// handed in or read from disk stays where it is for the whole run --
        /// so one drawn again is found in one probe and embedded once.
        mutable std::unordered_map<const std::byte*, const graphics::Image*> pictures{};

        /// Each page of another PDF drawn, kept as the images are, and found
        /// again the same way: by where the file's bytes lie, then its page.
        mutable std::deque<graphics::Drawing> drawings{};
        mutable std::unordered_map<const std::byte*, std::vector<std::pair<int, const graphics::Drawing*>>> sheets{};
    };

}
