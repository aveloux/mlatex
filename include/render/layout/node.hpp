#pragma once

#include "font.hpp"
#include "memory/slice.hpp"
#include "render/graphics/drawing.hpp"
#include "render/graphics/image.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace render::typography {
    // Forward declared: a directive only points at the patterns a
    // language's words break by, and needs nothing else of them.
    class Hyphenator;
}

namespace render::layout {

    class Node {
    public:
        enum class Type : std::uint8_t {
            Box,       // Container holding horizontal or vertical lists of child nodes
            Glue,      // Elastic spacing that stretches or shrinks during line or page breaking
            Kern,      // Fixed non-breakable spacing between layout elements
            Penalty,   // Breakpoint evaluation node with an attached penalty value
            Rule,      // Solid rectangular geometric shape like lines or rules
            Glyph,     // Individual rendered typographic character or symbol
            Path,      // A straight stroke between two points, for a canvas's own drawing
            Bitmap,    // A decoded raster image, drawn at a given size
            Pause,     // Explicit break directive or breakpoint container
            Directive  // An instruction to the document rather than something drawn
        };

        enum class Order : std::uint8_t {
            Normal,    // Standard finite elasticity order
            Fil,       // First level infinite elasticity overriding normal
            Fill,      // Second level infinite elasticity overriding fil
            Filll      // Third level infinite elasticity overriding fill
        };

        enum class Alignment : std::uint8_t {
            Horizontal, // Horizontal list box alignment mode
            Vertical    // Vertical list box alignment mode
        };

        /// @brief How the lines of a paragraph sit in their column.
        enum class Justification : std::uint8_t {
            Full,     ///< Both edges straight; the spaces stretch to fill each line.
            Left,     ///< Flush left, ragged right: `\\raggedright` and `flushleft`.
            Right,    ///< Flush right, ragged left: `\\raggedleft` and `flushright`.
            Center    ///< Every line centred: `\\centering` and `center`.
        };

        enum class Sign : std::uint8_t {
            None,       // Baseline glue state with zero scaling
            Stretching, // Glue expanded beyond baseline width
            Shrinking   // Glue compressed below baseline width
        };

        struct Point {
            float x{0.0f};
            float y{0.0f};
        };

        struct Size {
            float width{0.0f};
            float height{0.0f};
        };

        /// @brief A color, the way a page description wants one: three
        ///        channels, each from 0 to 1, and an opacity the same way.
        ///        Opaque black -- every channel zero, alpha one -- is what a
        ///        rule or a glyph draws in when nothing asked for another; a
        ///        node's own color is never optional, just usually the default.
        struct Color {
            float r{0.0f};
            float g{0.0f};
            float b{0.0f};
            float alpha{1.0f};

            [[nodiscard]] friend bool operator==(const Color&, const Color&) noexcept = default;
        };

        /// @brief A linear map a box's contents are drawn through: graphicx's
        ///        \\rotatebox, \\scalebox, \\resizebox and \\reflectbox.
        ///
        /// Down the page is positive, as everywhere in the layout, so a turn
        /// anticlockwise on the page by an angle t is `a = cos t`,
        /// `b = -sin t`, `c = sin t`, `d = cos t`. The contents are drawn with
        /// their reference point moved #x to the right of the box's, which is
        /// what brings the left edge of what the map makes to the box's own.
        struct Transform {
            float a{1.0f};   ///< Across, from across.
            float b{0.0f};   ///< Down, from across.
            float c{0.0f};   ///< Across, from down.
            float d{1.0f};   ///< Down, from down.
            float x{0.0f};   ///< How far right of the box's reference point the contents' own sits.
        };

        /// @brief A container: a run of nodes, measured as one.
        ///
        /// #shift and #offset move the box away from where its parent's cursor
        /// left it, and both always apply, whichever way the parent runs. A
        /// horizontal parent uses #shift to raise a superscript or drop a
        /// fraction onto the axis; a vertical parent uses #offset to centre a
        /// line in the column. Positive is down the page and to the right.
        struct Box {
            float width{0.0f};       ///< Advance width.
            float height{0.0f};      ///< Extent above the reference point.
            float depth{0.0f};       ///< Extent below it.
            float shift{0.0f};       ///< Displacement down the page.
            float offset{0.0f};      ///< Displacement to the right.
            float ratio{0.0f};       ///< How far the glue inside was stretched or shrunk.
            Sign sign{Sign::None};   ///< Which of the two that was, if either.
            Order order{Order::Normal};   ///< Which order of glue #ratio applies to; the rest keep their width.
            Alignment alignment{Alignment::Horizontal};   ///< Which way the contents run.
            memory::Slice<Node*> list{};                  ///< The contents.

            /// True for a canvas: its children already carry their own
            /// position, so nothing here advances the pen between them the
            /// way a line or a column otherwise would.
            bool absolute{false};

            /// True for an overlay: drawn at #anchor, measured from the
            /// page's own top left corner rather than from wherever the
            /// document's pen has reached, and taking no room in the flow --
            /// #height and #depth are left zero for exactly that. This is
            /// what lets a picture sit in a margin, or across the top of the
            /// page before anything else has been placed there at all.
            bool anchored{false};
            Point anchor{};   ///< Where, on the page, when #anchored is set.

            /// The map the contents are drawn through, or none; #width,
            /// #height and #depth are then the bounds of what it makes.
            const Transform* transform{nullptr};
        };

        struct Glue {
            float width{0.0f};
            float stretch{0.0f};
            float shrink{0.0f};
            Order expand{Order::Normal};
            Order limit{Order::Normal};
            const Node* leader{nullptr};   ///< Drawn along the glue: a rule stretched to its width, or a box repeated on a grid -- TeX's `\\leaders`.
        };

        struct Kern {
            float width{0.0f};
        };

        struct Penalty {
            std::int32_t value{0};
            bool flag{false};
        };

        struct Rule {
            float width{0.0f};
            float height{0.0f};
            float depth{0.0f};
            Color color{};   ///< Fill color; black by default.
        };

        /// @brief One character, placed.
        ///
        /// #code is a glyph index into #font, not a character -- the two are
        /// different numbers, and shaping is what turns one into the other.
        /// #point keeps the character it came from, which nothing on the page
        /// needs but a reader selecting text does.
        struct Glyph {
            float width{0.0f};                     ///< Advance width.
            float height{0.0f};                    ///< Extent above the baseline.
            float depth{0.0f};                     ///< Extent below it.
            float x{0.0f};                         ///< Offset from the pen, across.
            float y{0.0f};                         ///< Offset from the pen, down.
            std::uint32_t code{0};                 ///< Glyph index within #font.
            std::uint32_t point{0};                ///< The character it stands for.
            const typography::Font* font{nullptr}; ///< Which font draws it.

            /// The characters a ligature stands for, in UTF-8 -- `fi` for the
            /// one glyph the font draws for the two letters -- so the text
            /// copied out of the page reads as it was written. Empty for a
            /// glyph that stands for #point alone.
            std::string_view letters{};
            Color color{};                         ///< Fill color; black by default.
        };

        /// @brief One straight stroke, from one point to another.
        ///
        /// Both ends are offsets from the reference point a canvas's own box
        /// hands its children, the same way a Glyph's #Glyph::x and #Glyph::y
        /// are -- which is what lets a canvas draw every stroke it holds at
        /// the one position its box was placed at, none of them advancing a
        /// pen the way an ordinary line or column would.
        struct Path {
            Point start{};          ///< One end, across and down from the reference point.
            Point end{};             ///< The other end, the same way.
            float width{1.0f};       ///< Stroke width, in points.
            bool dashed{false};      ///< False for a solid stroke, true for a dashed one.
            Color color{};           ///< Stroke color; black by default.
            memory::Slice<Point> area{};   ///< When not empty, a region filled in #color -- its corners in
                                           ///< order, placed as #start is -- rather than a stroke.
        };

        /// @brief A decoded raster image, drawn at a given size.
        ///
        /// Drawn as a "big glyph" would be: #height is the extent above the
        /// reference point and the depth is left zero, so the image's own
        /// bottom edge is what a column or the page's own flow measures it
        /// from, the same convention a Rule follows.
        struct Bitmap {
            float width{0.0f};                        ///< Drawn width, in points.
            float height{0.0f};                       ///< Drawn height, in points.
            const graphics::Image* source{nullptr};   ///< The decoded pixels.
            const graphics::Drawing* drawing{nullptr};   ///< Or a page of another PDF, drawn as a form.
            std::array<float, 4> window{};               ///< For a #drawing: the part of its page shown, in its
                                                         ///< own points -- left, bottom, right, top -- the rest clipped.
        };

        /// @brief A page break: what it costs, and -- in the penalty's flag --
        ///        whether it ends the whole sheet rather than the column.
        struct Pause {
            Penalty penalty{};
        };

        /// @brief An instruction to the document, carried in its place in the list.
        ///
        /// Whether a paragraph is indented, how its lines are aligned and how
        /// far in from the margin they start are decided when the paragraph is
        /// assembled, which is long after the command that asked for them was
        /// read. So the command travels as a node, in order with everything
        /// else, and the document acts on it when it reaches it.
        ///
        /// @par Example
        /// `\\noindent` becomes a Directive whose #command is Command::Flush;
        /// `\\centering` becomes one whose #command is Command::Align and whose
        /// #justification is Justification::Center.
        struct Directive {
            /// @brief What the document is asked to do.
            enum class Command : std::uint8_t {
                Indent,     ///< Open the next paragraph indented: `\\indent`.
                Flush,      ///< Open the next paragraph flush, unless a blank line comes first: `\\noindent`.
                Suppress,   ///< Open the next paragraph flush whatever comes first: after a heading.
                Align,      ///< Set paragraphs from here on as #justification says.
                Margin,     ///< Move one edge of every line from here on in by #width.
                Number,     ///< The page's own number, drawn here in #font: `\\thepage`.
                Page,       ///< Change how pages are furnished or numbered, from the page this lands on.
                Note,       ///< A footnote's text, #note, set at the foot of the page this lands on.
                Save,       ///< Keep the paragraph shape in force -- alignment and margins -- to go back to.
                Restore,    ///< Go back to the shape the last Save kept.
                Hold,       ///< A float opens: what follows, to its Release, is set together, never split.
                Release,    ///< The float the last Hold opened closes.
                Anchor,     ///< A place a page reference or a table of contents asks the page of.
                Aside,      ///< A margin note, #note, set in the margin level with the line it stands in.
                Columns,    ///< What follows is set in #index columns side by side: `\\twocolumn`, multicols.
                Barrier,    ///< Every float still waiting is set before what follows: `\\FloatBarrier`.
                Language,   ///< Words from here on break by #hyphenator, no nearer their ends than #before and #after.
                Direction,  ///< Paragraphs from here on read right to left when #reversed is set, left to right if not.
                Link,       ///< What is drawn from here to the next Unlink goes to #target, or to #index's anchor.
                Unlink,     ///< The innermost open link ends.
                Repeat      ///< A long table's rows follow, to the next Repeat: a column they break out of ends
                            ///< with #foot, and the next opens with #head. One with neither ends the table.
            };

            /// @brief Where a float may go, as LaTeX's `[htbp!]` says: a bit
            ///        each, in #place.
            enum Place : std::uint8_t {
                Here = 1,     ///< `h`: where it is written.
                Top = 2,      ///< `t`: at the head of a column.
                Bottom = 4,   ///< `b`: at the foot of a column.
                Alone = 8,    ///< `p`: on a page of floats.
                Force = 16    ///< `!`: past the fractions and counts that would stop it.
            };

            /// @brief What a page carries besides its column.
            enum class Style : std::uint8_t {
                Keep,       ///< As it was.
                Plain,      ///< Its number, centred in the foot: LaTeX's own default.
                Empty,      ///< Nothing.
                Headings,   ///< Its number, at the right of the head.
                Fancy       ///< Whatever `\\fancyhead` and `\\fancyfoot` put there.
            };

            /// @brief How pages are numbered.
            enum class Numbering : std::uint8_t {
                Keep,        ///< As they were.
                Arabic,      ///< 1, 2, 3.
                Roman,       ///< i, ii, iii -- or I, II, III, with #capital.
                Alphabetic   ///< a, b, c -- or A, B, C, with #capital.
            };

            Command command{Command::Indent};                     ///< What to do.
            Justification justification{Justification::Full};   ///< For Command::Align.
            float width{0.0f};   ///< For Command::Margin, in points; for Command::Number, the room it takes.
            bool trailing{false};   ///< For Command::Margin: false moves the left edge, true the right.
            const typography::Font* font{nullptr};   ///< For Command::Number: the face it is set in.
            Style style{Style::Keep};                ///< For Command::Page.
            Numbering numbering{Numbering::Keep};    ///< For Command::Page: numbering restarts at 1 when set.
            bool capital{false};                     ///< For Command::Page: Roman or letters in capitals.
            bool local{false};                       ///< For Command::Page: this page alone, not every one from here.
            const Node* note{nullptr};               ///< For Command::Note and Command::Aside: the note, set as a column.
            bool fixed{false};                       ///< For Command::Hold: `[H]`, set where it is written and never moved.
            std::uint8_t place{Top | Bottom | Alone};   ///< For Command::Hold: where it may go, of #Place.
            bool across{false};                      ///< For Command::Hold: across every column of a page of several,
                                                     ///< as `figure*` is.
            const typography::Hyphenator* hyphenator{nullptr};   ///< For Command::Language: its patterns; null breaks no word.
            std::uint8_t before{2};                  ///< For Command::Language: least letters a break leaves, `\\lefthyphenmin`.
            std::uint8_t after{3};                   ///< For Command::Language: least it carries over, `\\righthyphenmin`.
            std::uint32_t digits{0};                 ///< For Command::Language: the code point its digits are drawn
                                                     ///< from -- U+0660 for Arabic's, U+06F0 for Persian's and
                                                     ///< Urdu's -- or 0 for the digits as they were written.
            bool spaced{false};                      ///< For Command::Language: French's spaces, no line ending at
                                                     ///< one, before `;` `:` `!` `?` and `»` and after `«`.
            bool reversed{false};                    ///< For Command::Direction: whether paragraphs read right to left.
            Node* head{nullptr};                     ///< For Command::Repeat: the rows set atop each column a table
                                                     ///< carries on into, or none.
            Node* foot{nullptr};                     ///< For Command::Repeat: the rows set under the last row in each
                                                     ///< column a table breaks out of, or none.
            std::string_view target{};               ///< For Command::Link: the address it goes to; empty for #index's anchor.
            Color border{};                          ///< For Command::Link: the frame a reader draws round it; none at no alpha.
            std::size_t index{0};                    ///< For Command::Anchor: which anchor, counted from 0 in the order written;
                                                     ///< for Command::Columns, how many columns; for Command::Hold,
                                                     ///< its kind, the floats of one kind keeping their order; for
                                                     ///< Command::Page, one more than the number the page it lands
                                                     ///< on takes, or 0 to count on.
        };

        explicit Node(const Type type = Type::Box) noexcept : type(type) {}

        [[nodiscard]] const Box& box() const noexcept { return data.box; }
        [[nodiscard]] const Glue& glue() const noexcept { return data.glue; }
        [[nodiscard]] const Kern& kern() const noexcept { return data.kern; }
        [[nodiscard]] const Penalty& penalty() const noexcept { return data.penalty; }
        [[nodiscard]] const Rule& rule() const noexcept { return data.rule; }
        [[nodiscard]] const Glyph& glyph() const noexcept { return data.glyph; }
        [[nodiscard]] const Path& path() const noexcept { return data.path; }
        [[nodiscard]] const Bitmap& bitmap() const noexcept { return data.bitmap; }
        [[nodiscard]] const Pause& pause() const noexcept { return data.pause; }
        [[nodiscard]] const Directive& directive() const noexcept { return data.directive; }

        void box(const Box& value) noexcept { type = Type::Box; data.box = value; }
        void glue(const Glue& value) noexcept { type = Type::Glue; data.glue = value; }
        void kern(const Kern& value) noexcept { type = Type::Kern; data.kern = value; }
        void penalty(const Penalty& value) noexcept { type = Type::Penalty; data.penalty = value; }
        void rule(const Rule& value) noexcept { type = Type::Rule; data.rule = value; }
        void glyph(const Glyph& value) noexcept { type = Type::Glyph; data.glyph = value; }
        void path(const Path& value) noexcept { type = Type::Path; data.path = value; }
        void bitmap(const Bitmap& value) noexcept { type = Type::Bitmap; data.bitmap = value; }
        void pause(const Pause& value) noexcept { type = Type::Pause; data.pause = value; }
        void directive(const Directive& value) noexcept { type = Type::Directive; data.directive = value; }

        Type type{Type::Box};   ///< What the node is, and so which of its data it holds.

    private:

        union Data {
            Box box;
            Glue glue;
            Kern kern;
            Penalty penalty;
            Rule rule;
            Glyph glyph;
            Path path;
            Bitmap bitmap;
            Pause pause;
            Directive directive;

            Data() : box{} {}
            ~Data() {}
        } data{};
    };

}