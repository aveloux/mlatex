#pragma once

#include "layout/document.hpp"
#include "layout/typesetter.hpp"
#include "memory/arena.hpp"
#include "memory/location.hpp"
#include "render/layout/node.hpp"
#include "syntax/node.hpp"
#include "syntax/expression/unicodes.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/blocks.hpp"
#include "syntax/primitives/variables.hpp"
#include "syntax/number.hpp"
#include "syntax/semantics/registers.hpp"
#include "typography/font.hpp"
#include "typography/library.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <charconv>
#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace render::primitives {

    class Counters;

    /// @brief Which font a primitive should be using right now.
    ///
    /// Two roles, because a document sets its prose and its formulas in
    /// different faces and the choice has to survive a `\\font` in between.
    /// Modules read from here rather than holding fonts of their own, so a
    /// face selected in one primitive is the face the next one uses.
    class Selection {
    public:
        Selection() noexcept = default;
        Selection(const Selection&) noexcept = default;

        /// @brief Takes another's faces and color -- a group's, put back as it
        ///        closes -- which counts as a change like any other.
        Selection& operator=(const Selection& other) noexcept {
            tint = other.tint;
            prose = other.prose;
            maths = other.maths;
            ++count;
            return *this;
        }

        /// @brief How many times the face or the color has changed, only ever
        ///        rising: what the parser watches to end a run of text where
        ///        the face it was begun in no longer holds.
        [[nodiscard]] const std::uint64_t& changes() const noexcept { return count; }

        /// @brief The face running text is set in.
        [[nodiscard]] const typography::Font* text() const noexcept { return prose; }

        /// @brief Selects the face running text is set in.
        /// @param font Font to use; must outlive the document.
        void text(const typography::Font* font) noexcept {
            prose = font;
            ++count;
        }

        /// @brief The face formulas are set in.
        ///
        /// Falls back to the text face when none was selected, which keeps a
        /// formula visible in a document that never asked for a maths family
        /// even though the result will not be proper mathematics.
        [[nodiscard]] const typography::Font* formula() const noexcept {
            return maths ? maths : prose;
        }

        /// @brief Selects the face formulas are set in.
        /// @param font Font to use; must outlive the document.
        void formula(const typography::Font* font) noexcept {
            maths = font;
            ++count;
        }

        /// @brief The color text is set in, or null for the default, black.
        [[nodiscard]] const layout::Node::Color* color() const noexcept { return tint; }

        /// @brief Selects the color text is set in: xcolor's `\\color`.
        /// @param value Color to use, or null for the default; must outlive the document.
        void color(const layout::Node::Color* value) noexcept {
            tint = value;
            ++count;
        }

    private:
        const layout::Node::Color* tint{nullptr}; ///< Color for running text.
        const typography::Font* prose{nullptr};   ///< Face for running text.
        const typography::Font* maths{nullptr};   ///< Face for formulas.
        std::uint64_t count{0};                    ///< Changes so far; see changes().
    };

    /// @brief Everything a render primitive needs that is not the parser.
    ///
    /// Passed by reference to every module, so a module never owns engine
    /// state and two modules always see the same state. This is the render
    /// layer's counterpart to syntax::primitives::Context, and it is built the
    /// same way: one instance, shared.
    struct Context {
        layout::Document& document;                        ///< What the primitives fill.
        const layout::Typesetter& typesetter;              ///< Lowers formulas into boxes.
        syntax::semantics::Registers& registers;           ///< The register bank, scoped by Union.
        typography::Registry& registry;                    ///< Fonts already built.
        typography::Library& library;                      ///< Where font files come from.
        const typography::Shaper& shaper;                  ///< Text into positioned glyphs.
        const syntax::expression::Unicodes& unicodes;      ///< Maths symbol names.
        const syntax::primitives::Blocks& blocks;          ///< Named blocks, for a module that hooks one.
        const syntax::primitives::Variables& variables;    ///< Named values: colors a document defined, among others.
        memory::Arena& arena;                              ///< Allocator for anything that outlives a pass.
        Selection& selection;                              ///< Which font is in use.

        /// @brief What a `\\label` written now would refer to.
        ///
        /// The number most recently given out -- a section's `2.1`, an
        /// equation's `3`, an item's `4` -- which is LaTeX's `\@currentlabel`.
        /// Whichever module numbers something sets it; the one that records
        /// labels reads it, and neither has to know about the other.
        std::string anchor{};

        /// @brief What kind of thing #anchor numbers -- `section`, `equation`,
        ///        `figure`, `item` -- which is what `\\cref` names it by.
        std::string kind{};

        /// @brief What #anchor's thing is called, for `\\nameref`: a heading's
        ///        title, a caption's text -- LaTeX's `\\@currentlabelname`.
        std::string title{};

        /// @brief How many anchors have been given out, which is the next
        ///        one's index: a place whose page something asks for.
        std::size_t anchors{0};

        /// @brief One line of a list the document may print: a heading in
        ///        the table of contents, a caption in the list of figures or
        ///        of tables, or one \\addcontentsline wrote.
        struct Entry {
            std::string list{};                   ///< `toc`, `lof` or `lot`, as LaTeX names its files.
            std::size_t level{0};                 ///< 0 for a chapter, down to 3; 1 for a figure or a table.
            std::string number{};                 ///< As its heading or caption printed it; empty for none.
            memory::Slice<syntax::Node*> title{}; ///< What the line says, as it was read.
            std::size_t anchor{0};                ///< Where it stands, for its page.
        };

        /// @brief Every such line, in the order written; the lists are set
        ///        from them as the document ends.
        std::vector<Entry> entries{};

        /// @brief The page each anchor landed on the pass before this one,
        ///        by index, or none on a first pass.
        const std::vector<std::string>* folios{nullptr};

        /// @brief Whether anything asked for a page -- `\\pageref`, a table of
        ///        contents -- which only a second pass can print.
        bool paged{false};

        /// @brief Files handed in from memory, or none: a picture a program
        ///        drew itself is found here before anywhere on disk.
        const syntax::primitives::Files* files{nullptr};

        /// @brief Files beside the document on disk -- a picture, a `.bib`
        ///        file, a listing's source -- asked after #files.
        syntax::primitives::Reader disk{};

        /// @brief Where a file the document writes for itself is kept, for
        ///        #disk to find: `\\begin{filecontents}{refs.bib}`.
        syntax::primitives::Writer write{};

        /// @brief Where hyphenation patterns are read from: the assets' own
        ///        folder of TeX's `hyph-*.pat.txt`, or empty for none.
        std::string patterns{};

        /// @brief Whether the text being read reads right to left, as the
        ///        language chosen last says: what a primitive building a line
        ///        of its own -- a list item's label -- hangs to the right by.
        bool reversed{false};

        /// @brief Whether the normal face is the sans family, as the
        ///        document's `\\familydefault` says when it starts: what
        ///        `\\normalfont` and every primitive setting the normal face
        ///        choose.
        bool sans{false};

        /// @brief The named counters, which headings, equations, theorems and
        ///        captions all number through; set by Wrapper before any
        ///        module is installed.
        const Counters* counters{nullptr};
    };

    /// @brief A module: anything callable as `(syntax::Parser&, Context&)`.
    ///
    /// This is the whole interface. A module declares no base class and
    /// overrides nothing; it just has to be invocable, which is why Wrapper
    /// can fold over a pack of unrelated types.
    ///
    /// Render modules take the Parser rather than the Mouth because most of
    /// them produce a node. The ones that only assign -- the page geometry,
    /// the font selection -- reach the expander through `parser.mouth()`.
    template <typename Module>
    concept Primitive = std::invocable<Module, syntax::Parser&, Context&>;

    /// @brief Installs every module given, in order.
    ///
    /// @param parser  Parser to bind into.
    /// @param context Engine services, handed to each module unchanged.
    /// @param modules The modules to install; later ones win a name clash.
    /// @complexity O(1) in the number of modules -- the fold is expanded at
    ///             compile time, so this is a straight run of calls.
    void bind(syntax::Parser& parser, Context& context, Primitive auto&&... modules) {
        (std::invoke(modules, parser, context), ...);
    }


    /// @brief Wraps a layout box as a syntax node the parser can return.
    ///
    /// A primitive that builds a box has to hand the parser something from the
    /// syntax tree, because that is what a parse produces. A directive node is
    /// that: an opaque carrier whose payload the document appends as it is.
    ///
    /// @param arena    Allocator for the node.
    /// @param node     The box; may be null, which yields a node with nothing in it.
    /// @param location Where in the source it came from, for diagnostics.
    /// @param block    True when the box stands on its own -- a heading, a list
    ///                 item, a table, a rule across the page -- and false when
    ///                 it belongs in the line, as a kern or an `\\hbox` does.
    ///                 The document reads this to decide whether the box joins
    ///                 the paragraph being built or ends it.
    /// @return The carrier node.
    /// @complexity O(1).
    [[nodiscard]] inline syntax::Node* directive(
        memory::Arena& arena,
        layout::Node* node,
        const memory::Location location,
        const bool block = false
    ) noexcept {
        auto* carrier = arena.compose<syntax::Node>(
            syntax::Node::Type::Directive, std::string_view{}, location);
        carrier->directive = node;
        carrier->display = block;
        return carrier;
    }

    /// @brief Records which face a run of parsed nodes is to be set in.
    ///
    /// Text is read long before it is shaped, and the face in use while it was
    /// read is gone by the time anything looks at it. Marking it here is what
    /// carries the choice across that gap.
    ///
    /// Only nodes with no face yet are marked, so an inner group keeps what it
    /// chose and an outer one claims everything the inner one left.
    ///
    /// @param nodes   The parsed children.
    /// @param context Engine services, for the face in use.
    /// @complexity O(n) in the nodes, including those nested inside groups.
    inline void stamp(const memory::Slice<syntax::Node*> nodes, const Context& context) {
        for (std::size_t index = 0; index < nodes.count; ++index) {
            syntax::Node* child = nodes[index];
            if (!child || child->face) continue;

            if (child->type == syntax::Node::Type::Group) {
                stamp(child->nodes, context);
                continue;
            }
            if (child->type == syntax::Node::Type::Text) {
                child->face = context.selection.text();
            } else if (child->type == syntax::Node::Type::Expression) {
                child->face = context.selection.formula();
            }
        }
    }

    /// @brief How wide a line is where the document is being read: `\\linewidth`,
    ///        which a column, a minipage or a list sets for what is inside it,
    ///        in points.
    /// @param context Engine services, for the register.
    /// @return The width.
    /// @complexity O(1).
    [[nodiscard]] inline float breadth(const Context& context) noexcept {
        using Registers = syntax::semantics::Registers;
        return static_cast<float>(context.registers.get(Registers::Type::Dimension, Registers::reserved + 1)) / 65536.0f;
    }

    /// @brief A length as a document writes one -- `3cm`, `12pt`, `0.4\\linewidth`
    ///        -- in points.
    ///
    /// The page's widths -- `\\textwidth`, `\\linewidth` and the rest -- are
    /// registers the dimension reader takes as units itself; a multiple of
    /// `\\parindent`, which is assigned as TeX assigns it rather than held
    /// in a register, is worked out here.
    ///
    /// @param text    The length, as written.
    /// @param mouth   Expander, for the dimension reader.
    /// @param context Engine services, for the page's widths.
    /// @return The length, or 0 when it does not read as one.
    /// @complexity O(n) in the text's length.
    inline float measure(const std::string_view text, syntax::Mouth& mouth, const Context& context) {
        std::string_view trimmed = text;
        while (!trimmed.empty() && trimmed.front() == ' ') trimmed.remove_prefix(1);
        while (!trimmed.empty() && trimmed.back() == ' ') trimmed.remove_suffix(1);
        if (trimmed.empty()) return 0.0f;

        if (constexpr std::string_view name = "\\parindent"; trimmed.ends_with(name)) {
            std::string_view factor = trimmed.substr(0, trimmed.size() - name.size());
            while (!factor.empty() && factor.back() == ' ') factor.remove_suffix(1);
            float amount = 1.0f;
            if (factor == "-") {
                amount = -1.0f;
            } else if (!factor.empty() &&
                       std::from_chars(factor.data(), factor.data() + factor.size(), amount).ec != std::errc{}) {
                amount = 1.0f;
            }
            return amount * context.document.configuration().indent;
        }

        // Read as an expression, so a length may be written as the calc
        // package lets it be: `\\textwidth-2cm`, `0.5\\linewidth+1em`. Past
        // its end is a mark no document can write, and whatever of the text
        // is left before it -- a glue's `plus 2pt` -- is read and let go,
        // never set as words.
        const syntax::Symbol end = mouth.lexicon().intern("\\measure:end");
        const syntax::Token mark{.symbol = end, .category = syntax::CatCodes::Category::Escape,
                                 .text = mouth.lexicon().resolve(end)};
        mouth.stream().inject(std::span{&mark, 1});
        mouth.ingest(context.arena.copy("\\dimexpr " + std::string(trimmed) + "\\relax "));
        const auto scanned = syntax::Number::dimension(mouth, context.registers);
        for (syntax::Token token = mouth.read(); !token.empty() && token.symbol != end; token = mouth.read()) {}
        return scanned ? static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale) : 0.0f;
    }

    /// @brief Text as it is to be set: TeX's keyboard spellings of the marks a
    ///        keyboard has no key for, turned into the marks themselves.
    ///
    /// Three hyphens are an em dash and two an en dash; a backtick opens a
    /// quotation and an apostrophe closes one, doubled for a double one.
    /// Running text is spelled this way as the document appends it; text a
    /// primitive shapes itself -- a heading, a caption, a table's cell --
    /// comes through here first, so `Navier--Stokes` reads the same in a
    /// heading as in the paragraph under it.
    ///
    /// @param text  The text as written.
    /// @param arena Where a changed copy is kept.
    /// @return The text itself when nothing in it changes, or the changed copy.
    /// @complexity O(n) in the text's length.
    inline std::string_view ligate(const std::string_view text, memory::Arena& arena) {
        if (text.find_first_of("-`'") == std::string_view::npos) return text;

        std::string result;
        result.reserve(text.size() + 8);
        for (std::size_t at = 0; at < text.size();) {
            const char mark = text[at];
            if (mark != '-' && mark != '`' && mark != '\'') {
                result += mark;
                ++at;
                continue;
            }

            std::size_t run = 0;
            while (at < text.size() && text[at] == mark) {
                ++at;
                ++run;
            }
            if (mark == '-') {
                result += run >= 3 ? "—" : run == 2 ? "–" : "-";
            } else if (mark == '`') {
                result += run >= 2 ? "“" : "‘";
            } else {
                result += run >= 2 ? "”" : "’";
            }
        }
        return arena.copy(result);
    }

    /// @brief Turns one parsed child into the boxes it stands for.
    ///
    /// Boxes, tables and list items all parse a group and then need its
    /// contents as layout nodes. This is that step, in one place: text is
    /// shaped, a formula is lowered, and a box a nested primitive already
    /// built is taken as it is.
    ///
    /// @param nodes   Where to put what comes out; appended to.
    /// @param child   The parsed node; null and empty text add nothing.
    /// @param context Engine services, for the font and the two engines.
    /// @complexity O(n) in the characters, plus the shaper's or typesetter's work.
    inline void gather(
        std::vector<layout::Node*>& nodes,
        const syntax::Node* child,
        const Context& context
    ) {
        if (!child) return;

        // A group is not a thing on the page; it is a boundary around things.
        // Its children go in where it stood.
        if (child->type == syntax::Node::Type::Group) {
            for (std::size_t index = 0; index < child->nodes.count; ++index) {
                gather(nodes, child->nodes[index], context);
            }
            return;
        }

        // The face the text was read under, where one was recorded, and
        // whatever is in use otherwise.
        const auto* font = child->face
                               ? static_cast<const typography::Font*>(child->face)
                               : context.selection.text();

        // Set as running text is, its words broken where the language in
        // force lets them break -- a minipage, a paragraph cell -- and a
        // blank at the head of the whole setting nothing.
        if (child->type == syntax::Node::Type::Text && font && !child->value.empty()) {
            const memory::Slice<layout::Node*> set = context.document.set(
                child->value, *font, nodes.empty(), static_cast<const layout::Node::Color*>(child->tint));
            nodes.insert(nodes.end(), set.begin(), set.end());
            return;
        }

        if (child->type == syntax::Node::Type::Directive && child->directive) {
            nodes.push_back(static_cast<layout::Node*>(child->directive));
            return;
        }

        if (child->type == syntax::Node::Type::Expression && child->expression) {
            const auto* maths = child->face
                                    ? static_cast<const typography::Font*>(child->face)
                                    : context.selection.formula();
            if (maths) {
                if (layout::Node* lowered =
                        context.typesetter.lower(child->expression, *maths, 0.0f)) {
                    if (const auto* tint = static_cast<const layout::Node::Color*>(child->tint)) {
                        layout::Typesetter::paint(lowered, *tint);
                    }
                    nodes.push_back(lowered);
                }
            }
        }
    }

}
