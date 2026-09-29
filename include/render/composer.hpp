#pragma once

#include "layout/document.hpp"
#include "layout/node.hpp"
#include "layout/typesetter.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "typography/font.hpp"
#include "typography/shaper.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace render {

    /// @brief Turns a page's boxes into the marks that print it.
    ///
    /// Everything above this point measures; this is where a position becomes
    /// an instruction. It walks the box tree, tracks where each node's origin
    /// falls, and writes a page description -- the operators a PDF content
    /// stream is made of.
    ///
    /// @par Runs
    /// A glyph written on its own costs an operator and a font selection.
    /// Glyphs in one face at one size are therefore gathered into a single
    /// run, closed only when the face changes, a rule interrupts it, or the
    /// page ends. A page in one face is one run.
    ///
    /// @par Fonts
    /// Each distinct face used is given a resource name, and every glyph drawn
    /// from it is remembered. The writer needs both: the names go in the
    /// page's resource dictionary, and the glyphs are what it cuts the font
    /// down to, so a document using forty characters embeds forty.
    ///
    /// @par Use
    /// @code
    /// render::Composer composer(arena, scratch, shaper, typesetter);
    /// composer.document().append(text, font, 12.0f);
    ///
    /// for (const auto& page : typesetter.compose(composer.document())) {
    ///     const std::string_view content = composer.draw(page.nodes, page.notes, left, top);
    ///     // ... hand `content` and composer.faces() to the writer ...
    /// }
    /// @endcode
    class Composer {
    public:
        /// @brief One face used by the pages drawn so far.
        ///
        /// A face is embedded with only the glyphs the document drew, numbered
        /// from one in the order they were first drawn. The page descriptions
        /// name those numbers rather than the ones the file uses, which is
        /// what keeps a maths face with five thousand glyphs from embedding
        /// five thousand empty slots to reach the twelve that were wanted.
        struct Entry {
            const typography::Font* font{nullptr};   ///< A font over it, for its metrics.
            std::vector<std::uint32_t> glyphs{};     ///< Glyphs drawn, in first-drawn order.
            std::vector<std::uint32_t> points{};     ///< The character each of those came from.
            std::vector<std::string_view> letters{}; ///< A ligature's characters, for each; empty for one alone.

            /// Each of those glyphs' widths in thousandths of the em, rounded
            /// as the reader is given them: taken once, when the glyph is
            /// first drawn, and read back for every later draw and for the
            /// width table the writer embeds.
            std::vector<float> widths{};
            std::vector<std::uint32_t> numbers{};    ///< Glyph to its number, indexed by glyph.
        };

        /// @brief Binds a composer to its allocators and its layout engine.
        /// @param arena Allocator for the document's own boxes.
        /// @param scratch Allocator for intermediates within one pass.
        /// @param shaper  Shaper, handed on to the document.
        /// @param typesetter  Layout engine, for lowering formulas and paginating.
        Composer(
            memory::Arena& arena,
            memory::Arena& scratch,
            const typography::Shaper& shaper,
            layout::Typesetter& typesetter
        ) noexcept;

        /// @brief Describes one page.
        ///
        /// The description is built fresh on each call and stays valid until
        /// the next one, so a caller writes each page out before drawing the
        /// next.
        ///
        /// @param nodes The page's blocks, in reading order.
        /// @param notes The footnotes they call, set at the foot of the text
        ///              block under a short rule, as LaTeX sets them.
        /// @param left  Where the text block's left edge sits, in points.
        /// @param top   Where its top edge sits, in points.
        /// @return The page description.
        /// @complexity O(n) in the nodes beneath them.
        [[nodiscard]] std::string_view draw(memory::Slice<layout::Node*> nodes, memory::Slice<const layout::Node*> notes,
                                            float left, float top);

        /// @brief Every face the pages drawn so far have used.
        ///
        /// Indexed the way the page description names them: entry `n` is the
        /// resource the description calls `/F<n>`.
        [[nodiscard]] const std::vector<Entry>& faces() const noexcept { return used; }

        /// @brief Every opacity the pages drawn so far have used.
        ///
        /// Indexed the way the page description names them: entry `n` is the
        /// resource the description calls `/GS<n>`. An opaque document -- by
        /// far the common case -- draws none at all, so nothing is reserved
        /// for this until a color with alpha under one is actually seen.
        [[nodiscard]] const std::vector<float>& opacities() const noexcept { return transparency; }

        /// @brief Every image the pages drawn so far have used.
        ///
        /// Indexed the way the page description names them: entry `n` is the
        /// resource the description calls `/Im<n>`. The same image used
        /// twice is one entry, embedded once.
        [[nodiscard]] const std::vector<const graphics::Image*>& pictures() const noexcept { return pictures_; }

        /// @brief Every page of another PDF the pages drawn so far have used.
        ///
        /// Indexed as pictures() is: entry `n` is the form the page
        /// description calls `/Fm<n>`, embedded once however often drawn.
        [[nodiscard]] const std::vector<const graphics::Drawing*>& drawings() const noexcept { return drawings_; }

        /// @brief The document being composed.
        [[nodiscard]] layout::Document& document() noexcept { return document_; }
        [[nodiscard]] const layout::Document& document() const noexcept { return document_; }   ///< @copydoc Composer::document()

        /// @brief The page each anchor was drawn on, as that page is
        ///        numbered, by the anchor's index; empty for one not drawn.
        ///
        /// What a second pass reads to print a `\\pageref` or a table of
        /// contents: known only once the pages are, which is after the text
        /// that asks for them has been set.
        [[nodiscard]] const std::vector<std::string>& anchors() const noexcept { return anchors_; }

        /// @brief Where an anchor was drawn: which page, counted from 0, and
        ///        how far down it, in points from the page's top.
        struct Place {
            std::size_t page{0};   ///< The page.
            float down{0.0f};      ///< From its top.
        };

        /// @brief Where each anchor was drawn, by its index -- what a link to
        ///        it, or a bookmark, goes to.
        [[nodiscard]] const std::vector<Place>& places() const noexcept { return places_; }

        /// @brief A link as it was drawn: where it goes, and the area of each
        ///        line it covers.
        struct Link {
            std::string_view target{};   ///< The address it goes to; empty for #anchor's place.
            std::size_t anchor{0};       ///< The anchor it goes to, when it has no address.
            layout::Node::Color border{};   ///< The frame a reader draws round it; none at no alpha.
            std::vector<std::array<float, 4>> areas{};   ///< Left, top, right and bottom, from the page's top left.
        };

        /// @brief Every link drawn, by the page it was drawn on.
        [[nodiscard]] const std::vector<std::vector<Link>>& links() const noexcept { return links_; }

        /// @brief Every page drawn so far, as its text: what a reader copying
        ///        it would get -- a ligature as its letters, a word space as a
        ///        space, each line on a line of its own -- recorded as the
        ///        page is drawn, so reading it back costs nothing.
        [[nodiscard]] const std::vector<std::string>& texts() const noexcept { return texts_; }

        /// @brief The layout engine this composer was built with.
        [[nodiscard]] layout::Typesetter& engine() const noexcept { return typesetter; }

    private:
        /// @brief How far below a node's top its reference point sits.
        ///
        /// Zero for a column and for pure space, whose reference point is
        /// their top edge already; the height above the baseline for anything
        /// that sits on one. Walking a column means adding this, drawing, then
        /// adding whatever is left of the node's extent.
        ///
        /// @param item Node to measure; null measures zero.
        /// @complexity O(1).
        [[nodiscard]] static float height(const layout::Node* item) noexcept;

        /// @brief Walks one node, dispatching on what it is.
        /// @param item     Node to walk; null returns at once.
        /// @param position Its own position from the left.
        /// @param baseline Its reference point from the top.
        void node(const layout::Node* item, float position, float baseline);

        /// @brief Walks a box's contents, advancing the pen between them.
        /// @param item     The box; its own shift and offset are already applied.
        /// @param position Its own position from the left.
        /// @param baseline Its baseline, or its top edge when it runs vertically.
        void inside(const layout::Node* item, float position, float baseline);

        /// @brief Writes a rule as a filled rectangle.
        /// @param item     The rule node.
        /// @param position Its left edge.
        /// @param baseline Its baseline; the rule rises above it by its height.
        void rule(const layout::Node* item, float position, float baseline);

        /// @brief Sets the fill color, a rule or a run of glyphs paints in,
        ///        when it differs from the one already set.
        /// @param value Color to fill with.
        void fill(const layout::Node::Color& value);

        /// @brief Sets both alpha channels to a color's own, when it differs
        ///        from the one already set.
        ///
        /// PDF keeps one pair of alphas in the graphics state rather than one
        /// per paint operator, so a stroke and a fill both reach for this;
        /// setting both channels to the one value costs nothing extra and
        /// means neither has to track the other's.
        ///
        /// @param value Opacity to set, from 0 to 1.
        void translucent(float value);

        /// @brief Closes the open run, if there is one.
        void flush();

        /// @brief One glyph's width, in thousandths of the em.
        ///
        /// Rounded, because that is the form the width reaches a reader in and
        /// the reader advances its pen by what it was given. Advancing the
        /// model by anything finer would drift a thousandth per glyph and end
        /// the line somewhere else.
        ///
        /// @param font Font to measure in.
        /// @param glyph  Glyph index.
        /// @complexity O(1).
        [[nodiscard]] static float width(const typography::Font& font, std::uint32_t glyph) noexcept;

        /// @brief Draws one place of a head or a foot.
        /// @param nodes    What it holds; a Command::Number among them is the page's number.
        /// @param left     The column's left edge.
        /// @param span     The column's width.
        /// @param place    0 flush left, 1 centred, 2 flush right.
        /// @param baseline Its baseline, from the top.
        void slot(memory::Slice<layout::Node*> nodes, float left, float span, int place, float baseline);

        /// @brief The page's number, written out as it is numbered now.
        [[nodiscard]] std::string folio() const;

        /// @brief The page's number, shaped in a face and drawn with its
        ///        start at a point.
        /// @return How far it reaches.
        float numeral(const typography::Font* font, float position, float baseline);

        layout::Typesetter& typesetter;   ///< Layout engine.
        const typography::Shaper& shaper; ///< Text into glyphs, for a page's number.
        layout::Document document_;       ///< The document being composed.

        // The page being drawn: its number, how pages are numbered from here,
        // and the style pages are furnished in from here.
        std::int32_t count{1};                                                  ///< Its number.
        layout::Node::Directive::Numbering numbering{layout::Node::Directive::Numbering::Arabic};   ///< How.
        bool capital{false};                                                   ///< In capitals.
        layout::Node::Directive::Style style{layout::Node::Directive::Style::Plain};   ///< From here on.

        std::string content{};       ///< The page description being built.
        std::vector<Entry> used{};   ///< Faces used, in resource order.
        std::vector<std::string> anchors_{};   ///< The page of each anchor drawn so far.
        std::string transcript{};              ///< The text of the page being drawn.
        std::vector<std::string> texts_{};     ///< The text of every page drawn so far.

        // The open run: which face and size it is in, and where its pen is.
        const typography::Font* running{nullptr};   ///< Its font, or null when no run is open.
        std::size_t index{0};                    ///< Its resource index.
        float pen{0.0f};                         ///< Where its pen has reached from the left.
        float line{0.0f};                        ///< Its baseline from the top.

        // The color last set, so a page of the one color -- nearly every one
        // -- costs one operator rather than one per glyph and rule.
        layout::Node::Color fill_{};     ///< Fill color in use.
        layout::Node::Color stroke_{};   ///< Stroke color in use.

        std::vector<float> transparency{};   ///< Opacities used, in resource order.
        float opacity_ = 1.0f;               ///< Opacity in use.

        std::vector<const graphics::Image*> pictures_{};   ///< Images used, in resource order.
        std::vector<const graphics::Drawing*> drawings_{};   ///< Pages of other PDFs used, in resource order.

        std::vector<Place> places_{};                 ///< Where each anchor drawn so far stands.
        std::vector<std::vector<Link>> links_{};      ///< The links of every page drawn so far.
        std::vector<Link> open_{};                    ///< The links being drawn, innermost last.

        /// @brief Takes in what was just drawn into the innermost open link:
        ///        its area on the line it stands on, a new area once it is
        ///        on another line.
        /// @param left   Its left edge.
        /// @param top    Its top, from the page's top.
        /// @param right  Its right edge.
        /// @param bottom Its bottom.
        void cover(float left, float top, float right, float bottom);
    };

}
