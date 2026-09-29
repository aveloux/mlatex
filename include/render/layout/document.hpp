#pragma once

#include "layout/ledger.hpp"
#include "layout/node.hpp"
#include "layout/pager.hpp"
#include "layout/paragraph.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"
#include "syntax/expression/node.hpp"
#include "typography/font.hpp"
#include "typography/hyphenator.hpp"
#include "typography/shaper.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace render::layout {

    // Forward declared, not included: typesetter.hpp includes this header, so
    // including it back would close the cycle. A document only lowers formulas
    // through the reference it is handed, which needs the name and not the
    // definition.
    class Typesetter;

    /// @brief Everything a document is made of, in the order it was written.
    ///
    /// Material arrives the way the parser produced it -- a run of text, then
    /// a formula, then more text -- and the document's job is to decide what
    /// belongs together. Anything that can sit in a line joins the paragraph
    /// being built; anything that cannot ends it and becomes a block of its
    /// own. That is why `$x$` stays in its sentence while `\\[x\\]` gets a
    /// line to itself.
    ///
    /// @par What is in a paragraph
    /// A horizontal list, exactly as TeX means it: glyphs, the glue between
    /// words, the penalties that say where a word may be hyphenated, an inline
    /// formula already measured, a box a primitive built. All of it measures
    /// the same way, so the breaker does not need to know which is which.
    ///
    /// @par Indentation, alignment and margins
    /// A paragraph's first line is indented by Configuration::indent, as
    /// LaTeX's `\\parindent` has it, except where LaTeX would not. Text that
    /// carries straight on from a display, a list or a table is not indented,
    /// and neither is a paragraph after `\\noindent` -- but a blank line in
    /// between makes it a new paragraph, which is. After a heading nothing is
    /// indented until a paragraph actually starts, blank lines or not. A ragged
    /// or centred paragraph is never indented. How the lines sit, and how far
    /// in from the margin they start, arrive as Node::Directive instructions in
    /// their place among the material, and hold for every paragraph after them
    /// until another one changes them.
    ///
    /// @par Use
    /// @code
    /// layout::Document document(arena, scratch, shaper, typesetter);
    /// document.configuration().width = 595.0f;     // A4
    /// document.hyphenate(&hyphenator);
    ///
    /// document.append("some text ", *text, 12.0f);
    /// document.append(formula, *maths);            // joins that sentence
    /// document.separate();                         // and ends the paragraph
    /// @endcode
    class Document {
    public:
        /// @brief The page, in points.
        struct Configuration {
            float width{612.0f};    ///< Page width.
            float height{792.0f};   ///< Page height.
            float left{72.0f};      ///< Left margin.
            float right{72.0f};     ///< Right margin.
            float top{72.0f};       ///< Top margin.
            float bottom{72.0f};    ///< Bottom margin.
            float leading{14.0f};   ///< Baseline-to-baseline distance within a paragraph.
            float indent{15.0f};    ///< Indentation of a paragraph's first line: `\\parindent`.
            float size{12.0f};      ///< The body's size, `\\normalsize`, which every size step is taken from.
            float skip{0.0f};       ///< Space between two paragraphs, beyond the baseline's: `\\parskip`.
            std::size_t columns{1}; ///< How many columns the class sets the text in: 2 for `[twocolumn]`.
            float gap{10.0f};       ///< Between two columns: `\\columnsep`.
            Pager::Placement placement{};   ///< Where floats may go: `\\topfraction` and the rest.
        };

        /// @brief One block of the document.
        struct Element {
            /// @brief What kind of block this is.
            enum class Type : std::uint8_t {
                Paragraph,   ///< A horizontal list, to be broken into lines.
                Directive    ///< A box that stands on its own.
            };

            Type type{Type::Paragraph};      ///< Which of the two it is.
            Paragraph* paragraph{nullptr};   ///< Set for Type::Paragraph.
            Node* node{nullptr};             ///< Set for Type::Directive.
            std::size_t columns{1};          ///< For Type::Paragraph: the columns it was written in, one of
                                             ///< which its lines are as wide as.
            Element* next{nullptr};          ///< The block after this one.
        };

        /// @brief Builds an empty document.
        /// @param arena Allocator for blocks and everything they produce.
        /// @param scratch Allocator for intermediates within a layout pass.
        /// @param shaper  Shaper, for turning text into glyphs.
        /// @param typesetter  Layout engine, for lowering formulas.
        /// @param page    The page geometry; letter paper by default, and
        ///                `\\documentclass` overrides it from the document.
        Document(
            memory::Arena& arena,
            memory::Arena& scratch,
            const typography::Shaper& shaper,
            const Typesetter& typesetter,
            const Configuration& page
        ) noexcept;

        /// @brief Binds a document on letter paper, until `\\documentclass`
        ///        says otherwise.
        /// @param arena Arena for everything that outlives a pass.
        /// @param scratch Arena for what does not.
        /// @param shaper  Shaper for the text.
        /// @param typesetter  Lowers formulas into boxes.
        Document(
            memory::Arena& arena,
            memory::Arena& scratch,
            const typography::Shaper& shaper,
            const Typesetter& typesetter
        ) noexcept;

        /// @brief Adds a run of text to the paragraph being built.
        ///
        /// Words are shaped, the spaces between them become glue, and where a
        /// hyphenator has been given, the places each word may be broken
        /// become penalties. Shaped runs are remembered, so text that repeats
        /// -- a running head, a label -- is shaped once for the document.
        ///
        /// So are the words inside them. A word is shaped and hyphenated the
        /// first time it is seen in a font and handed out again every time
        /// after, which for prose -- a few hundred words, over and over -- is
        /// most of them. The glyphs and penalties handed out are shared, not
        /// copied; nothing after this point writes to either.
        ///
        /// @param text Its text; must outlive the document.
        /// @param font Font to set it in; must outlive the document.
        /// @param size Size in points, which keys the shaping cache.
        /// @param color Color to set it in, or null for black. Colored text
        ///              is set with copies of the shared glyphs, which the
        ///              color is written on, so the cache stays black.
        /// @complexity O(n) in the characters; shaping and hyphenation only
        ///             for a word not seen before in this font.
        void append(std::string_view text, const typography::Font& font, float size,
                    const Node::Color* color = nullptr);

        /// @brief A run of text as the nodes a paragraph would hold for it,
        ///        held by no paragraph: what a box, a table's cell or a list
        ///        item is made of, set the way running text is -- its words
        ///        shaped and broken where the language in force lets them
        ///        break, the spaces between them glue, its dashes and quotes
        ///        spelled out.
        ///
        /// @param text    Its text; must outlive the document.
        /// @param font    Font to set it in; must outlive the document.
        /// @param opening True when it opens what it is set into, so a blank
        ///                at its head sets nothing, as at a paragraph's head.
        /// @param color   Color to set it in, or null for black.
        /// @return The nodes, in order.
        /// @complexity O(n) in the characters; a word seen before is a lookup.
        [[nodiscard]] memory::Slice<Node*> set(std::string_view text, const typography::Font& font, bool opening,
                                               const Node::Color* color = nullptr);

        /// @brief Adds a formula.
        ///
        /// An inline formula joins the paragraph being built. A displayed one
        /// ends that paragraph, takes a block of its own centred in the
        /// column, and the text after it starts a new paragraph.
        ///
        /// @param expression Its tree; null is ignored.
        /// @param font       Font to set it in -- a maths family for real results.
        /// @param color      The color of the text it was written in; null for black.
        /// @complexity O(n) in the formula's nodes.
        void append(const syntax::expression::Node* expression, const typography::Font& font,
                    const Node::Color* color = nullptr);

        /// @brief Adds a box a primitive already built, or obeys an instruction.
        ///
        /// A Node::Directive takes no room: it says how the paragraphs from
        /// here on are to be assembled, and is acted on here rather than kept.
        ///
        /// @param node  The box or instruction; null is ignored.
        /// @param block True when it stands on its own -- a heading, a
        ///              display, a table -- and false when it belongs in the
        ///              line, as a kern or an explicit box does.
        /// @complexity O(1).
        void append(Node* node, bool block);

        /// @brief Adds a displayed formula: a block of its own, with the
        ///        space LaTeX keeps above and below a display around it.
        ///
        /// The text after it carries on from it, and so is not indented.
        ///
        /// @param box The display, already as wide as the column.
        void display(Node* box);

        /// @brief Ends the paragraph being built.
        ///
        /// What a blank line in the source means. Doing it twice in a row does
        /// nothing the second time, so a document full of blank lines does not
        /// fill up with empty paragraphs.
        ///
        /// @complexity O(n) in the paragraph's own material.
        void separate();

        /// @brief Sets where words may be broken.
        ///
        /// A different hyphenator from the one in use forgets every word
        /// already shaped, since each was remembered with the breaks the old
        /// one gave it. So does a change to how near a word's ends it may
        /// break -- which a document changes by changing its language, as a
        /// Node::Directive::Command::Language in its place does.
        ///
        /// @param hyphenator Hyphenator to use, or null to break no word;
        ///                   must outlive the document.
        /// @param left       Least letters a break may leave before it: `\\lefthyphenmin`.
        /// @param right      Least it may carry over after it: `\\righthyphenmin`.
        void hyphenate(const typography::Hyphenator* hyphenator, std::size_t left = 2, std::size_t right = 3) noexcept;

        /// @brief Ends any open paragraph and lays every paragraph out.
        ///
        /// Paragraphs already broken to the current column are left alone, so
        /// calling this twice costs the second time nothing.
        ///
        /// @complexity O(n) in the blocks, plus each stale paragraph's own work.
        void layout() noexcept;

        /// @brief Every block, in order.
        /// @complexity O(n); the list is copied into a slice for the caller.
        [[nodiscard]] memory::Slice<Element*> elements() const noexcept;

        /// @brief What a page carries besides its column: a head and a foot,
        ///        each in three places, and a rule under one and over the other.
        ///
        /// Read by the composer as it draws each page, in the style the page
        /// is in: Style::Fancy draws these, Style::Plain only a number in the
        /// foot, Style::Empty nothing. A Command::Number among the nodes is
        /// the page's number, written out as the page is drawn.
        struct Furniture {
            std::array<memory::Slice<Node*>, 3> head{};   ///< The head's left, centre and right.
            std::array<memory::Slice<Node*>, 3> foot{};   ///< The foot's, the same way.
            float rule{0.4f};                             ///< Under the head, in points: `\\headrulewidth`.
            float line{0.0f};                             ///< Over the foot: `\\footrulewidth`.
            const typography::Font* face{nullptr};        ///< The face a plain page's number is set in.
            Node::Color background{1.0f, 1.0f, 1.0f};     ///< What the page is painted in: `\\pagecolor`.
        };

        /// @brief Sets the face a character is looked for in when the text's
        ///        own face has no glyph for it: a check mark, a star, a card's
        ///        suit in the middle of a sentence.
        ///
        /// Asked only for a character the text's face cannot draw, so a run
        /// it draws whole costs nothing more. The character is set at the
        /// size this face was loaded at.
        ///
        /// @param font The face, or nullptr for none.
        void fallback(const typography::Font* font) noexcept { fallback_ = font; }

        /// @brief What every page carries besides its column.
        [[nodiscard]] const Furniture& furniture() const noexcept { return furniture_; }
        [[nodiscard]] Furniture& furniture() noexcept { return furniture_; }   ///< @copydoc Document::furniture() const

        /// @brief One entry of the file's outline, which a reader lists
        ///        beside the pages: a heading, as hyperref bookmarks one.
        struct Bookmark {
            std::size_t level{0};   ///< How deep: 0 for a chapter, 1 a section, and on.
            std::string title{};    ///< What it says, its number first.
            std::size_t anchor{0};  ///< The anchor it goes to.
        };

        /// @brief What the file says of itself besides its pages: what a
        ///        reader shows as its title, author, subject and keywords --
        ///        hyperref's pdftitle and the rest -- its outline, and whether
        ///        it keeps to PDF/A-2b, the archival standard, as pdfx and
        ///        `\\DocumentMetadata` ask.
        struct Metadata {
            std::string title{};                  ///< `pdftitle`.
            std::string author{};                 ///< `pdfauthor`.
            std::string subject{};                ///< `pdfsubject`.
            std::string keywords{};               ///< `pdfkeywords`.
            std::vector<Bookmark> bookmarks{};    ///< The outline, in order.
            bool archival{false};                 ///< Kept to PDF/A-2b.
        };

        /// @brief What the file says of itself besides its pages.
        [[nodiscard]] const Metadata& metadata() const noexcept { return metadata_; }
        [[nodiscard]] Metadata& metadata() noexcept { return metadata_; }   ///< @copydoc Document::metadata() const

        /// @brief The page geometry.
        [[nodiscard]] const Configuration& configuration() const noexcept { return configuration_; }
        [[nodiscard]] Configuration& configuration() noexcept { return configuration_; }   ///< @copydoc Document::configuration() const

        /// @brief How many blocks the document holds.
        [[nodiscard]] std::size_t count() const noexcept { return blocks; }

        /// @brief How wide one of so many columns is across the text block,
        ///        `\\columnsep` apart: `\\columnwidth`.
        /// @param count How many columns.
        /// @complexity O(1).
        [[nodiscard]] float column(const std::size_t count) const noexcept {
            const float text = configuration_.width - configuration_.left - configuration_.right;
            if (count <= 1) return text;
            return (text - configuration_.gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        }

        /// @brief How wide text is set from here on: one of the columns in force.
        /// @complexity O(1).
        [[nodiscard]] float column() const noexcept { return column(split ? split : configuration_.columns); }

    private:
        /// @brief Links a freshly built block onto the end of the list.
        /// @param element The block.
        void attach(Element* element) noexcept;

        /// @brief Sets a run's words, dashes, quotes and spaces, the part of
        ///        append() and set() they share.
        /// @param into    Where the nodes go; appended to.
        /// @param text    The run.
        /// @param font    Its font, whose size keys the word cache.
        /// @param opening True when a blank at its head is to set nothing.
        void divide(std::vector<Node*>& into, std::string_view text, const typography::Font& font, bool opening);

        /// @brief TeX's space factor after a run of text: 3000 after a
        ///        sentence's stop, 2000 after a colon, 1500 after a
        ///        semicolon, 1250 after a comma, 1000 after anything else or
        ///        after a capital's stop, an abbreviation's.
        /// @param text     The run, its last character the one that counts.
        /// @param previous The factor before it, which closing marks pass on.
        /// @return The factor the space after it is set by.
        /// @complexity O(1).
        [[nodiscard]] static int weigh(std::string_view text, int previous) noexcept;

        memory::Arena& arena;                   ///< Allocator for blocks.
        memory::Arena& scratch;                 ///< Allocator for layout intermediates.
        const typography::Shaper& shaper;       ///< Text into glyphs.
        const Typesetter& typesetter;           ///< Formulas into boxes.
        Ledger ledger;                          ///< Cache of already-shaped runs.
        Ledger words;                           ///< Cache of already-shaped words, penalties included.
        Configuration configuration_{};         ///< The page.
        Furniture furniture_{};                 ///< What every page carries besides its column.
        Metadata metadata_{};                   ///< What the file says of itself.
        const typography::Hyphenator* hyphenation{nullptr};  ///< Where words may break.
        std::size_t before{2};                                ///< Least letters a break leaves: `\\lefthyphenmin`.
        std::size_t after{3};                                 ///< Least it carries over: `\\righthyphenmin`.
        const typography::Font* fallback_{nullptr};           ///< Where a character the text's face lacks is looked for.

        std::vector<Node*> pending{};   ///< The paragraph being built.
        bool indentation{true};         ///< Whether the next paragraph opens indented.
        bool suppression{false};        ///< Whether a blank line leaves that as it is: after a heading.
        Node::Justification justification{Node::Justification::Full};   ///< How paragraphs are set from here on.
        float margin{0.0f};             ///< Indent of every line of the paragraphs from here on, from the left.
        float gutter{0.0f};             ///< Indent of every line of the paragraphs from here on, from the right.
        bool reversed{false};           ///< Whether the paragraphs from here on read right to left.
        int factor{1000};               ///< TeX's space factor after what was set last: see weigh().
        std::size_t split{0};           ///< The columns from here on, as Command::Columns said; 0 for the class's.

        /// @brief A paragraph shape a Save directive kept.
        struct Shape {
            Node::Justification justification{Node::Justification::Full};   ///< How its lines sit.
            float margin{0.0f};   ///< Its left indent.
            float gutter{0.0f};   ///< Its right indent.
            bool reversed{false}; ///< Whether it reads right to left.
        };
        std::vector<Shape> shapes{};    ///< What Save kept, innermost last.
        Element* head{nullptr};         ///< First block.
        Element* tail{nullptr};         ///< Last block, so appending stays O(1).
        std::size_t blocks{0};          ///< How many there are.
    };

}
