#pragma once

#include "layout/document.hpp"
#include "layout/node.hpp"
#include "layout/pager.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "syntax/expression/node.hpp"
#include "typography/font.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <string_view>
#include <vector>

namespace render::layout {

    /// @brief Turns a formula into boxes, and a document into pages.
    ///
    /// Two jobs meet here because they are the same job at two scales. lower()
    /// walks an expression tree and returns one box holding every glyph in it,
    /// positioned; compose() walks a document and returns the pages its boxes
    /// fall onto. Neither produces ink -- that is the Composer's part.
    ///
    /// @par Where the numbers come from
    /// A maths font carries a MATH table saying where the axis sits, how thick
    /// a fraction bar is and how far a superscript rises. Those are read from
    /// the font in use and are what make a formula line up with itself. A font
    /// without one -- an ordinary text face -- falls back on Metrics, whose
    /// defaults are proportions of the size rather than measurements, and
    /// which look plausible rather than right. A document that wants real
    /// formulas should select a maths family for them.
    ///
    /// @par Styles
    /// A formula is set at one of three sizes, as TeX names them: the base
    /// size, script size for an exponent, and script-script size for an
    /// exponent on an exponent. Anything nested deeper stays at
    /// script-script. The smaller sizes are real fonts, fetched from the
    /// registry, not the base font drawn small -- which is why a superscript
    /// keeps its stroke weight instead of going spindly.
    ///
    /// @par Use
    /// @code
    /// layout::Typesetter typesetter(arena, registry, shaper);
    ///
    /// // One formula, laid out to the column width:
    /// layout::Node* box = typesetter.lower(tree, *math, column);
    ///
    /// // The whole document, broken into pages:
    /// for (const auto& page : typesetter.compose(document)) {
    ///     composer.draw(page.nodes, left, top);
    /// }
    /// @endcode
    class Typesetter {
    public:
        /// @brief Proportions used when the font carries no MATH table.
        ///
        /// Every distance is a fraction of the font size, so one set of
        /// numbers works at any size.
        struct Metrics {
            float axis{0.25f};          ///< Height of the maths axis.
            float thickness{0.04f};     ///< Fraction bar thickness.
            float numerator{0.677f};    ///< How far a displayed numerator rises.
            float denominator{0.686f};  ///< How far a displayed denominator drops.
            float ascent{0.394f};       ///< How far a numerator rises in a line.
            float descent{0.345f};      ///< How far a denominator drops in a line.
            float upper{0.444f};        ///< How far a binomial's top rises in a line.
            float superscript{0.45f};   ///< How far a superscript rises.
            float subscript{0.20f};     ///< How far a subscript drops.
            float script{0.70f};        ///< Size of a script, against its base.
            float gap{0.12f};           ///< Clearance around a bar or a radical.
            float clearance{0.15f};     ///< Clearance above a displayed radical's contents.
            float ascender{0.04f};      ///< Room left above a radical's bar.
            float before{0.28f};        ///< Space before a radical's degree.
            float after{-0.56f};        ///< Space after it, tucking the sign under it.
            float raise{0.60f};         ///< Height of the degree, as a fraction of the sign's.
            float padding{0.12f};       ///< Room either side of a fraction: `\\nulldelimiterspace`, 1.2pt at ten points.
            float display{1.40f};       ///< Least height of a large operator in a displayed formula.
            float drop{0.25f};          ///< Most a superscript sits below the top of a tall base.
            float sink{0.20f};          ///< Least a subscript sits below the bottom of a deep base.
            float limit{0.15f};         ///< Clearance between a large operator and its limits.
        };

        /// @brief Which of the maths alphabets letters are set in.
        ///
        /// Resolved from a command's name once, where the command is met, and
        /// carried down in the frame, so each letter under it is placed by a
        /// switch rather than by comparing names again.
        enum class Alphabet : std::uint8_t {
            Italic,         ///< The maths italic every variable is set in: no command, or `\\mathit`.
            Upright,        ///< `\\mathrm`, `\\operatorname`, and words: the letters as they are.
            Bold,           ///< `\\mathbf`: bold upright, Greek included.
            Heavy,          ///< `\\bm` and `\\boldsymbol`: TeX's bold maths italic.
            Blackboard,     ///< `\\mathbb`.
            Calligraphic,   ///< `\\mathcal`, and `\\mathscr` beside it.
            Fraktur,        ///< `\\mathfrak`.
            Sans,           ///< `\\mathsf`.
            Typewriter      ///< `\\mathtt`.
        };

        /// @brief Which of the three maths sizes a subformula is set at.
        enum class Style : std::uint8_t {
            Text,          ///< The formula's own size.
            Script,        ///< An exponent or an index.
            Scriptscript   ///< An exponent on an exponent, and anything deeper.
        };

        /// @brief Binds a typesetter to its allocator and its font sources.
        /// @param arena    Allocator for the boxes it builds.
        /// @param registry Where smaller sizes of a font come from.
        /// @param shaper   Shaper, for symbols that have no code point of their own.
        /// @param metrics  Fallbacks for a font with no MATH table.
        Typesetter(
            memory::Arena& arena,
            typography::Registry& registry,
            const typography::Shaper& shaper,
            const Metrics& metrics
        ) noexcept;

        /// @brief Binds a typesetter with the default fallback proportions.
        /// @param arena    Allocator for the boxes it builds.
        /// @param registry Where fonts at other sizes come from.
        /// @param shaper   Shaper for text inside formulas.
        Typesetter(
            memory::Arena& arena,
            typography::Registry& registry,
            const typography::Shaper& shaper
        ) noexcept;

        /// @brief Lays a formula out.
        /// @param node   Root of the expression tree; null yields null.
        /// @param font   Font the formula is set in; a maths family for real results.
        /// @param target Column width, for centring a displayed formula.
        /// @param style  Size to set at; callers start at Style::Text.
        /// @return One box holding the whole formula, or nullptr when nothing
        ///         in the tree could be drawn.
        /// @complexity O(n) in the tree's nodes; every rule is a table lookup
        ///             or a font query, and neither searches.
        [[nodiscard]] Node* lower(
            const syntax::expression::Node* node,
            const typography::Font& font,
            float target,
            Style style = Style::Text
        ) const;

        /// @brief Lays an inline formula out as the pieces a paragraph may
        ///        break it between.
        ///
        /// TeX lets a line end inside a formula in the text only after a
        /// relation or a binary operator at its outer level, and asks a
        /// price for it -- `\\relpenalty` of 500, `\\binoppenalty` of 700.
        /// Those penalties are set here, and the boxes around them opened
        /// up, so the breaker sees `a + b = c` as five pieces with two places
        /// to end a line rather than one box it cannot enter. The space after
        /// the operator starts the next line, where the breaker drops it.
        ///
        /// @param node Root of the expression tree; null yields nothing.
        /// @param font Font the formula is set in.
        /// @return The pieces, in order; one box when the formula offers no
        ///         break, and empty when nothing in it could be drawn.
        /// @complexity O(n) in the tree's nodes, as lower() is.
        [[nodiscard]] memory::Slice<Node*> unfold(
            const syntax::expression::Node* node,
            const typography::Font& font
        ) const;

        /// @brief Colors a laid-out formula: its symbols, and its rules --
        ///        a fraction's bar, a radical's -- as a formula written in
        ///        colored text is.
        /// @param node  What lower() or unfold() made; null is ignored.
        /// @param color The color.
        /// @complexity O(n) in the nodes under it.
        static void paint(Node* node, const Node::Color& color) noexcept;

        /// @brief Lays a grid out as its rows, each as wide as the whole grid.
        ///
        /// A numbered display -- an `align` -- sets each row of its grid on a
        /// line of its own with that row's number beside it, so it needs the
        /// rows apart rather than stacked. Every row comes back with the same
        /// width and its columns where the grid would have put them.
        ///
        /// @param node A Matrix node; anything else yields no rows.
        /// @param font Font the formula is set in.
        /// @return The rows, top to bottom.
        /// @complexity O(n) in the cells.
        [[nodiscard]] memory::Slice<Node*> rows(
            const syntax::expression::Node* node,
            const typography::Font& font
        ) const;

        /// @brief Lays a document out and breaks it into pages.
        /// @param document Document to set; its paragraphs are laid out first.
        /// @return The pages, in order.
        /// @complexity O(n) in the document's boxes, plus each paragraph's own
        ///             line breaking.
        [[nodiscard]] memory::Slice<Pager::Page> compose(Document& document) const;

    private:
        /// @brief Everything one subformula is set against.
        ///
        /// Carried by value down the recursion rather than threaded through
        /// six parameters, and holding the font's own MATH numbers already
        /// resolved so that no node has to ask the font twice.
        struct Frame {
            const typography::Font* font{nullptr};   ///< Font at this style's size.
            const typography::Font* base{nullptr};   ///< The formula's own font, at full size.
            Alphabet alphabet{Alphabet::Italic};     ///< Which alphabet letters are set in.
            float target{0.0f};                      ///< Column width, for display centring.
            Style style{Style::Text};                ///< Which of the three sizes this is.
            float axis{0.0f};                        ///< Maths axis height, in points.
            float thickness{0.0f};                   ///< Fraction bar thickness, in points.
            float numerator{0.0f};                   ///< Displayed numerator rise, in points.
            float denominator{0.0f};                 ///< Displayed denominator drop, in points.
            float ascent{0.0f};                      ///< Numerator rise in a line, in points.
            float descent{0.0f};                     ///< Denominator drop in a line, in points.
            float upper{0.0f};                       ///< A binomial's top rise in a line, in points.
            float separation{0.0f};                  ///< Least room from a fraction's bar to its parts, in a line.
            float distance{0.0f};                    ///< The same in a displayed formula.
            float spacing{0.0f};                     ///< Least room between a binomial's parts, in a line.
            float spread{0.0f};                      ///< The same in a displayed formula.
            float superscript{0.0f};                 ///< Superscript rise, in points.
            float subscript{0.0f};                   ///< Subscript drop, in points.
            float gap{0.0f};                         ///< Clearance above a radical's contents, in points.
            float clearance{0.0f};                   ///< The same in a displayed formula, in points.
            float rule{0.0f};                        ///< A radical's bar thickness, in points.
            float ascender{0.0f};                    ///< Room above a radical's bar, in points.
            float before{0.0f};                      ///< Space before a radical's degree, in points.
            float after{0.0f};                       ///< Space after it, in points; negative.
            float raise{0.0f};                       ///< Height of the degree, as a fraction of the sign's.
            float quad{0.0f};                        ///< One em, the unit maths spacing is in.
            float display{0.0f};                     ///< Least height of a displayed large operator.
            float drop{0.0f};                        ///< Superscript drop below a tall base's top.
            float sink{0.0f};                        ///< Subscript drop below a deep base's bottom.
            float limit{0.0f};                       ///< Clearance around an operator's limits.

            /// Set in text style though the formula is displayed, as TeX sets
            /// a fraction's parts and a matrix's cells: a large operator at
            /// its size in a line with its limits beside it, and a fraction
            /// inside stepped down a size.
            bool stepped{false};

            /// Where an inline formula's outer level goes, piece by piece, so
            /// a line may end after a relation or a binary operator there as
            /// TeX lets it; null inside anything built as a box of its own --
            /// a group, a script, a fraction -- which a line never breaks.
            std::vector<Node*>* pieces{nullptr};
        };

        /// @brief Resolves a frame for a font at a style.
        ///
        /// Every size is resolved from the formula's own font, never from the
        /// size above it: a script on a script is the font's script-script
        /// size, not its script size scaled down a second time.
        ///
        /// @param font     Font the formula is set in, at its own size.
        /// @param target   Column width.
        /// @param style    Size to resolve for.
        /// @param alphabet Alphabet the letters are set in.
        /// @return The frame, whose font is the smaller one when the style is.
        [[nodiscard]] Frame frame(const typography::Font& font, float target, Style style,
                                  Alphabet alphabet = Alphabet::Italic) const;

        /// @brief A node as a box of its own, safe to shift or to centre.
        ///
        /// Boxes are positioned by setting their shift and offset, so a part
        /// about to be moved has to be one whose shift and offset nobody else
        /// has set: a fraction arrives raised to the axis, and moving it as a
        /// superscript would otherwise throw that away. A box that is still
        /// where it was built comes back as it is; anything else is wrapped.
        ///
        /// @param part Node to enclose; may be null.
        /// @return A box that can be moved, or nullptr for null.
        /// @complexity O(1).
        [[nodiscard]] Node* enclose(Node* part) const;

        /// @brief How far in from a box's left edge its first ink starts.
        ///
        /// A box's edge is where its first glyph's pen starts, not where that
        /// glyph's ink does: an upright `5` stands a third of a point in from
        /// its origin, an italic `x` barely a tenth. Anything that sets a mark
        /// right against a box -- a radical sign against what it covers -- has
        /// to know the difference, or one letter touches the mark while
        /// another sits clear of it. Along a line only the first thing drawn
        /// is read, and any kern or glue in front of it counts; in a column
        /// every row is, and the one reaching furthest left decides -- a
        /// fraction's bar rather than its numerator. A rule or an image
        /// starts its ink at its edge.
        ///
        /// @param node The box, or a glyph on its own; may be null.
        /// @return The distance, in points; negative when the ink overhangs.
        /// @complexity O(n) in the rows of the columns met on the way to the
        ///             first glyph of each.
        [[nodiscard]] static float bearing(const Node* node) noexcept;

        /// @brief One delimiter, grown to a height and centred on the axis.
        ///
        /// Every stretchy mark in a formula is placed the same way: the font
        /// offers a series of sizes, the first tall enough is taken, and it is
        /// centred on the maths axis rather than stood on the baseline -- a
        /// parenthesis around a fraction has to reach as far below the axis as
        /// above it, and a displayed sum sits where a small one would.
        ///
        /// Past the tallest size the font draws whole, the mark is built from
        /// the font's parts by extend() and centred the same way, so it is a
        /// box rather than a glyph.
        ///
        /// @param codepoint The mark, as a character.
        /// @param reach     Height it has to cover, in points.
        /// @param scope     What it is set against.
        /// @return The glyph or the built box, or nullptr when the face has no
        ///         such mark.
        /// @complexity O(1), or O(n) in the pieces of a built mark.
        [[nodiscard]] Node* delimiter(std::uint32_t codepoint, float reach, const Frame& scope) const;

        /// @brief A stretchy mark grown to cover a height, standing on its
        ///        own baseline.
        ///
        /// The first of the font's sizes tall enough, or -- once even the
        /// tallest is too short -- the mark built from the font's own parts,
        /// so there is no height it cannot reach. Either way the result is
        /// one box whose ink runs from its baseline up to its height, which
        /// is what lets the caller place a radical's top at its bar or a
        /// parenthesis's middle on the axis with one shift, whichever it got.
        ///
        /// @param codepoint The mark, as a character.
        /// @param reach     Height its ink has to cover, in points.
        /// @param scope     What it is set against.
        /// @return The box, or nullptr when the face has no such mark.
        /// @complexity O(n) in the pieces of an assembled mark; O(1) otherwise.
        [[nodiscard]] Node* extend(std::uint32_t codepoint, float reach, const Frame& scope) const;

        /// @brief Wraps a box in a pair of delimiters grown to fit it.
        ///
        /// A parenthesised group, `\\left ... \\right` and a binomial are the
        /// same thing drawn around different insides, and all three come here.
        ///
        /// @par Example
        /// `\\left( \\frac{a}{b} \\right)` renders the fraction, then calls
        /// this with `(` and `)`: each parenthesis is grown until it covers the
        /// fraction's reach on whichever side of the axis is further.
        ///
        /// @param inner What is fenced; may be null, which fences nothing.
        /// @param open  Character on the left, or 0 for none.
        /// @param close Character on the right, or 0 for none.
        /// @param scope What it is set against.
        /// @return One box holding all three.
        /// @complexity O(1).
        [[nodiscard]] Node* fence(Node* inner, std::uint32_t open, std::uint32_t close,
                                  const Frame& scope) const;

        /// @brief Lowers one node against an already-resolved frame.
        /// @param node  Node to lower.
        /// @param scope What it is set against.
        /// @return Its box, or nullptr when there is nothing to draw.
        [[nodiscard]] Node* render(const syntax::expression::Node* node, const Frame& scope) const;

        /// @brief A grid's rows, each padded out to the grid's full width.
        ///
        /// Every cell is rendered and each column widened to its widest cell,
        /// the cells placed in their columns as the preamble says -- flush
        /// left, flush right or centred -- and the columns spaced as the grid
        /// says. Each row stands at least a strut tall and deep, so rows of
        /// short cells still keep a line's distance apart, as TeX's arrays do.
        ///
        /// @param node  A Matrix node.
        /// @param scope What it is set against.
        /// @return The rows, top to bottom; empty for a grid with no cells.
        /// @complexity O(n) in the cells.
        [[nodiscard]] memory::Slice<Node*> grid(const syntax::expression::Node* node, const Frame& scope) const;

        memory::Arena& arena;               ///< Allocator for boxes.
        typography::Registry& registry;     ///< Where smaller sizes come from.
        const typography::Shaper& shaper;   ///< For symbols with no code point.
        Metrics metrics{};               ///< Fallbacks for a font with no MATH table.
    };

}
