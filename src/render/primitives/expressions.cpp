/// @file
/// @brief Expression primitives: `$`, `\\(` and `\\[`, the operator table, and
///        amsmath's environments.
///
/// An expression gets an expression parser of its own, taught the operators,
/// run to its closing delimiter and then destroyed -- which is what returns
/// the token after it to the expander instead of losing it.
#include "render/primitives/expressions.hpp"
#include "render/primitives/counters.hpp"
#include "render/primitives/styles.hpp"
#include "render/primitives/tables.hpp"
#include "syntax/argument.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include "syntax/expression/node.hpp"
#include "syntax/semantics/scope.hpp"
#include "syntax/semantics/union.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace render::primitives {

    /// @brief A display as the document takes it: the space LaTeX keeps above
    ///        and below one, and the display between.
    /// @param context Engine services, for the body's size.
    /// @param arena   Allocator for the nodes.
    /// @param box     The display, as wide as the column.
    /// @param origin  Where in the source it came from.
    /// @return The three, as one group.
    static syntax::Node* displayed(const Context& context, memory::Arena& arena, layout::Node* box,
                                   const memory::Location origin) {
        const float skip = context.document.configuration.size;
        const memory::Slice<syntax::Node*> parts = arena.allocate<syntax::Node*>(3);
        for (const std::size_t index : {0uz, 2uz}) {
            auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
            glue->glue({.width = skip, .stretch = skip * 0.2f, .shrink = skip * 0.5f});
            parts[index] = directive(arena, glue, origin, true);
        }
        parts[1] = directive(arena, box, origin, true);
        return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, parts);
    }

    Expressions::Expressions(syntax::Lexicon& lexicon) : grammar(lexicon) {
        lexicon.intern("$");
        lexicon.intern("\\(");
        lexicon.intern("\\[");
        lexicon.intern("\\nonumber");
        lexicon.intern("\\notag");
        lexicon.intern("\\tag");

        using Type = syntax::expression::Node::Type;

        /// @brief One operator: what it builds and how tightly it binds.
        struct Rule {
            std::string_view name;   ///< What the document writes.
            Type type;               ///< What kind of node it makes.
            int weight;              ///< How tightly it binds; 0 for a structure.
            bool structural;         ///< True when it takes brace arguments, not operands.
        };

        // Loosest first. A relation binds least tightly, so `a + b = c` reads
        // as `(a + b) = c` and not as `a + (b = c)`.
        static constexpr std::array<Rule, 96> table{{
            // Relations, which everything else binds tighter than.
            {"=", Type::Binary, 1, false},
            {"<", Type::Binary, 1, false},
            {">", Type::Binary, 1, false},
            {"\\le", Type::Binary, 1, false},
            {"\\ge", Type::Binary, 1, false},
            {"\\leq", Type::Binary, 1, false},
            {"\\geq", Type::Binary, 1, false},
            {"\\neq", Type::Binary, 1, false},
            {"\\ne", Type::Binary, 1, false},
            {"\\equiv", Type::Binary, 1, false},
            {"\\approx", Type::Binary, 1, false},
            {"\\sim", Type::Binary, 1, false},
            {"\\simeq", Type::Binary, 1, false},
            {"\\cong", Type::Binary, 1, false},
            {"\\propto", Type::Binary, 1, false},
            {"\\ll", Type::Binary, 1, false},
            {"\\gg", Type::Binary, 1, false},
            {"\\lesssim", Type::Binary, 1, false},
            {"\\gtrsim", Type::Binary, 1, false},
            {"\\in", Type::Binary, 1, false},
            {"\\notin", Type::Binary, 1, false},
            {"\\subset", Type::Binary, 1, false},
            {"\\subseteq", Type::Binary, 1, false},
            {"\\supset", Type::Binary, 1, false},
            {"\\supseteq", Type::Binary, 1, false},
            {"\\to", Type::Binary, 1, false},
            {"\\gets", Type::Binary, 1, false},
            {"\\mapsto", Type::Binary, 1, false},
            {"\\iff", Type::Binary, 1, false},
            {"\\implies", Type::Binary, 1, false},
            {"\\Rightarrow", Type::Binary, 1, false},
            {"\\Leftarrow", Type::Binary, 1, false},
            {"\\Leftrightarrow", Type::Binary, 1, false},
            {"\\rightarrow", Type::Binary, 1, false},
            {"\\leftarrow", Type::Binary, 1, false},
            {"\\longrightarrow", Type::Binary, 1, false},
            {"\\perp", Type::Binary, 1, false},
            {"\\parallel", Type::Binary, 1, false},
            {"\\mid", Type::Binary, 1, false},

            // Addition, and the set operations that read like it.
            {"+", Type::Binary, 2, false},
            {"-", Type::Binary, 2, false},
            {"\\pm", Type::Binary, 2, false},
            {"\\mp", Type::Binary, 2, false},
            {"\\cup", Type::Binary, 2, false},
            {"\\cap", Type::Binary, 2, false},
            {"\\setminus", Type::Binary, 2, false},
            {"\\oplus", Type::Binary, 2, false},
            {"\\ominus", Type::Binary, 2, false},

            // Multiplication, which binds tighter than addition.
            {"*", Type::Binary, 3, false},
            {"/", Type::Binary, 3, false},
            {"\\times", Type::Binary, 3, false},
            {"\\cdot", Type::Binary, 3, false},
            {"\\div", Type::Binary, 3, false},
            {"\\otimes", Type::Binary, 3, false},
            {"\\wedge", Type::Binary, 3, false},
            {"\\vee", Type::Binary, 3, false},
            {"\\circ", Type::Binary, 3, false},
            {"\\ast", Type::Binary, 3, false},

            // Postfix, which binds tighter than any infix operator.
            {"!", Type::Unary, 10, false},

            // Structures, which take brace arguments instead of operands and
            // so sit outside the ordering entirely.
            {"\\frac", Type::Fraction, 0, true},
            {"\\dfrac", Type::Fraction, 0, true},
            {"\\tfrac", Type::Fraction, 0, true},
            {"\\binom", Type::Fraction, 0, true},
            {"\\dbinom", Type::Fraction, 0, true},
            {"\\tbinom", Type::Fraction, 0, true},
            {"\\sqrt", Type::Radical, 0, true},

            // Marks over a symbol, the wide ones drawn with the same marks.
            {"\\hat", Type::Accent, 0, true},
            {"\\widehat", Type::Accent, 0, true},
            {"\\vec", Type::Accent, 0, true},
            {"\\overrightarrow", Type::Accent, 0, true},
            {"\\bar", Type::Accent, 0, true},
            {"\\dot", Type::Accent, 0, true},
            {"\\ddot", Type::Accent, 0, true},
            {"\\tilde", Type::Accent, 0, true},
            {"\\widetilde", Type::Accent, 0, true},
            {"\\check", Type::Accent, 0, true},
            {"\\breve", Type::Accent, 0, true},
            {"\\acute", Type::Accent, 0, true},
            {"\\grave", Type::Accent, 0, true},
            {"\\mathring", Type::Accent, 0, true},
            {"\\dddot", Type::Accent, 0, true},
            {"\\ddddot", Type::Accent, 0, true},
            {"\\overleftarrow", Type::Accent, 0, true},
            {"\\overleftrightarrow", Type::Accent, 0, true},

            // Words inside a formula, read with their spaces and set upright
            // or in the style their name says.
            {"\\text", Type::Text, 0, true},
            {"\\textrm", Type::Text, 0, true},
            {"\\textup", Type::Text, 0, true},
            {"\\textnormal", Type::Text, 0, true},
            {"\\textmd", Type::Text, 0, true},
            {"\\textbf", Type::Text, 0, true},
            {"\\textit", Type::Text, 0, true},
            {"\\textsl", Type::Text, 0, true},
            {"\\textsf", Type::Text, 0, true},
            {"\\texttt", Type::Text, 0, true},
            {"\\emph", Type::Text, 0, true},
            {"\\mbox", Type::Text, 0, true},
        }};

        for (const auto& [name, type, weight, structural] : table) {
            grammar.bind(lexicon.intern(name), type, weight, false, structural);
        }

        // The sixteen fixed delimiter sizes: a delimiter follows each one
        // straight after, and is drawn at that size whatever it encloses.
        static constexpr std::array<std::string_view, 16> sizes{
            "\\big", "\\bigl", "\\bigr", "\\bigm", "\\Big", "\\Bigl", "\\Bigr", "\\Bigm",
            "\\bigg", "\\biggl", "\\biggr", "\\biggm", "\\Bigg", "\\Biggl", "\\Biggr", "\\Biggm",
        };
        for (const std::string_view name : sizes) {
            grammar.bind(lexicon.intern(name), Type::Variable, 0, false, true);
        }

        // The alphabets, and the marks that span what they are written over.
        // Each takes one brace group, which is what `arity` states -- without
        // it a command with no rule reads its argument only when one happens
        // to be braced, and `\\mathbb R` would set an R in the wrong alphabet
        // rather than the right one.
        static constexpr std::array<std::string_view, 18> spanning{
            "\\mathbb", "\\mathcal", "\\mathfrak", "\\mathrm", "\\mathbf", "\\mathit",
            "\\mathsf", "\\mathtt", "\\overline", "\\underline", "\\operatorname",
            "\\bm", "\\boldsymbol", "\\phantom", "\\hphantom", "\\vphantom", "\\smash",
            "\\mathscr",
        };
        for (const std::string_view name : spanning) {
            grammar.declare(lexicon.intern(name), 1);
        }

        // A mark over or under something: two groups, the mark first.
        for (const std::string_view name : {"\\overset", "\\underset", "\\stackrel"}) {
            grammar.declare(lexicon.intern(name), 2);
        }

        // The named functions. Each is one word set upright, which is the
        // whole difference between `sin` meaning a function and `sin` meaning
        // three variables multiplied together.
        static constexpr std::array<std::string_view, 33> functions{
            "\\sin", "\\cos", "\\tan", "\\cot", "\\sec", "\\csc",
            "\\arcsin", "\\arccos", "\\arctan", "\\sinh", "\\cosh", "\\tanh", "\\coth",
            "\\log", "\\ln", "\\lg", "\\exp", "\\det", "\\gcd", "\\deg", "\\dim",
            "\\ker", "\\hom", "\\lim", "\\max", "\\min", "\\sup", "\\inf", "\\Pr",
            "\\arg", "\\liminf", "\\limsup", "\\sgn",
        };
        for (const std::string_view name : functions) {
            grammar.declare(lexicon.intern(name), 0);
        }

        // The spaces, which take nothing: `\\!{}_{2}` is a space taken back
        // and then an empty base with a subscript, never a space over `{}`.
        for (const std::string_view name : {"\\,", "\\:", "\\;", "\\!", "\\ ", "\\quad", "\\qquad"}) {
            grammar.declare(lexicon.intern(name), 0);
        }

        // Read and set as nothing. A style switch is the typesetter's to
        // decide -- a displayed formula is display style already -- and the
        // rest are hints to TeX's own breaker, or a table rule that has no
        // meaning inside a formula's grid.
        static constexpr std::array<std::string_view, 12> silent{
            "\\displaystyle", "\\textstyle", "\\scriptstyle", "\\scriptscriptstyle",
            "\\limits", "\\nolimits", "\\allowbreak", "\\relax", "\\protect", "\\hline",
            "\\mathstrut", "\\strut",
        };
        for (const std::string_view name : silent) {
            grammar.silence(lexicon.intern(name));
        }
    }

    layout::Node* Expressions::label(Context& context, memory::Arena& arena, const std::string_view text) {
        if (text.empty()) return nullptr;

        // Upright whatever the text around it is: an equation's number in a
        // theorem is not set in the theorem's italic.
        const typography::Font* around = context.selection.text();
        if (!around) return nullptr;
        const typography::Font* font = Styles::resolve(context, Styles::Cut::Normal, around->size());
        if (!font) font = around;

        const typography::Font* fonts[] = {font};
        const memory::Slice<layout::Node*> shaped =
            context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy(text), {});
        return layout::Line::horizontal(arena, shaped, 0.0f);
    }

    void Expressions::count(syntax::Mouth& mouth, Context& context) const {
        if (!context.counters) return;
        context.counters->step("equation");
        numbered = Counters::print(mouth, "equation");
        context.anchor = numbered;
        context.kind = "equation";
    }

    void Expressions::retract(const Context& context) {
        if (context.counters) context.counters->set("equation", context.counters->value("equation") - 1);
    }

    layout::Node* Expressions::line(memory::Arena& arena, layout::Node* body, layout::Node* mark,
                                    const float balance, const float column) {
        // A kern as wide as the widest number, on the left, balances the
        // number on the right, so the expression is centred on the column
        // itself and not on what the number leaves of it -- and every row of
        // a numbered grid is centred the same way, whichever row has a number.
        auto* left = arena.compose<layout::Node>(layout::Node::Type::Kern);
        left->kern({.width = balance});

        auto* before = arena.compose<layout::Node>(layout::Node::Type::Glue);
        before->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
        auto* after = arena.compose<layout::Node>(layout::Node::Type::Glue);
        after->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});

        // Too wide to be centred with room for the number either side, it
        // moves left only as far as it has to, half an em short of its
        // number; too wide even for that, its number goes on a line of its
        // own beneath it, flush right, as TeX's \eqno and amsmath set one.
        if (mark && body && body->box().width + 2.0f * balance > column) {
            const float gap = mark->box().height * 0.67f;
            auto* apart = arena.compose<layout::Node>(layout::Node::Type::Kern);
            apart->kern({.width = gap});
            if (body->box().width + mark->box().width + gap <= column) {
                const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(4);
                row[0] = before;
                row[1] = body;
                row[2] = apart;
                row[3] = mark;
                return layout::Line::horizontal(arena, row, column);
            }
            const memory::Slice<layout::Node*> top = arena.allocate<layout::Node*>(3);
            top[0] = before;
            top[1] = body;
            top[2] = after;
            auto* push = arena.compose<layout::Node>(layout::Node::Type::Glue);
            push->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
            const memory::Slice<layout::Node*> bottom = arena.allocate<layout::Node*>(2);
            bottom[0] = push;
            bottom[1] = mark;
            const memory::Slice<layout::Node*> both = arena.allocate<layout::Node*>(2);
            both[0] = layout::Line::horizontal(arena, top, column);
            both[1] = layout::Line::horizontal(arena, bottom, column);
            return layout::Line::vertical(arena, both, 2.0f);
        }

        auto* right = arena.compose<layout::Node>(layout::Node::Type::Kern);
        right->kern({.width = balance - (mark ? mark->box().width : 0.0f)});

        const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(mark ? 6 : 5);
        row[0] = left;
        row[1] = before;
        row[2] = body;
        row[3] = after;
        row[4] = right;
        if (mark) row[5] = mark;
        return layout::Line::horizontal(arena, row, column);
    }

    syntax::Node* Expressions::enter(
        syntax::Parser& parser,
        Context& context,
        const bool display,
        const char delimiter,
        const syntax::Symbol stop
    ) const {
        syntax::Mouth& mouth = parser.mouth;
        memory::Arena& arena = parser.arena;
        const memory::Location origin = mouth.lookahead().location;

        // A display that holds a picture -- a tikz-cd, amscd or xy-pic
        // diagram, a circuit, a tree, a TikZ picture -- as papers write
        // one: read as the text it is, the picture centred on a line of its
        // own with its number beside it, as LaTeX sets the box it makes.
        if (display) {
            std::size_t ahead = 0;
            while (mouth.lookahead(ahead).category == syntax::Catcodes::Category::Space) ++ahead;
            const syntax::Token first = mouth.lookahead(ahead);
            std::string name;
            if (first.text == "\\begin" && mouth.lookahead(ahead + 1).is('{')) {
                for (std::size_t index = ahead + 2; index < ahead + 16; ++index) {
                    const syntax::Token letter = mouth.lookahead(index);
                    if (letter.empty() || letter.is('}')) break;
                    name += letter.text;
                }
            }
            static constexpr std::array<std::string_view, 5> pictures{"tikzcd", "CD", "quantikz", "tikzpicture", "forest"};
            // xy-pic's spacing may run on from its name: `\xymatrix@C=1em`.
            if (std::ranges::contains(pictures, name) || first.text.starts_with("\\xymatrix") ||
                first.text.starts_with("\\Qcircuit")) {
                setting = Layout::Single;
                const syntax::Symbol closer = delimiter == '$' ? mouth.lexicon.intern("$") : stop;
                syntax::Symbol matched = syntax::none;
                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node*> read = parser.parse(0, std::span{&closer, 1}, matched);
                stamp(read, context);
                mouth.pop(syntax::semantics::Scope::Type::Group);
                if (delimiter == '$' && mouth.lookahead().text == "$") static_cast<void>(mouth.read());

                std::vector<layout::Node*> set;
                for (const syntax::Node* child : read) gather(set, child, context);
                const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(set.size());
                std::ranges::copy(set, row.begin());
                layout::Node* body = layout::Line::horizontal(arena, row, 0.0f);
                const std::string text = !tagged.empty() ? std::exchange(tagged, std::string{})
                                         : !numbered.empty() ? "(" + std::exchange(numbered, std::string{}) + ")"
                                                         : std::string{};
                layout::Node* mark = text.empty() ? nullptr : label(context, arena, text);
                return displayed(context, arena, line(arena, body, mark, mark ? mark->box().width : 0.0f, breadth(context)),
                                 origin);
            }
        }

        mouth.push(syntax::semantics::Scope::Type::Equations);

        syntax::expression::Node* tree = nullptr;
        syntax::Token leftover{};

        // What this display was opened as: an `align` or a `gather` numbers
        // its rows, and is told about them as the grid is read. An inline
        // formula inside one is none of that.
        const Layout shape = display ? std::exchange(setting, Layout::Single) : Layout::Single;
        if (shape == Layout::Rows) lines.clear();

        // Scoped so the expression parser is destroyed -- and gives back the
        // token it had read ahead -- before that token is looked at.
        {
            syntax::expression::Parser inner(
                mouth, context.unicodes, grammar, arena,
                display ? syntax::expression::Node::Style::Display
                        : syntax::expression::Node::Style::Inline);

            // A row's number is given out as the row opens, so a `\\label` in
            // it already refers to it, and kept only once the row closes
            // unnumbered by nothing -- `\\nonumber` and `\\tag` take it back,
            // and so does the empty row after a last `\\\\`.
            if (shape == Layout::Rows) {
                inner.watch([this, &context, &mouth](const syntax::expression::Parser::Edge edge) {
                    using Edge = syntax::expression::Parser::Edge;
                    switch (edge) {
                        case Edge::Opened: {
                            Row row{};
                            if (counting && context.counters) {
                                context.counters->step("equation");
                                row.number = Counters::print(mouth, "equation");
                                context.anchor = row.number;
                                context.kind = "equation";
                            }
                            lines.push_back(std::move(row));
                            break;
                        }
                        case Edge::Closed:
                            break;
                        case Edge::Dropped:
                            if (!lines.empty()) {
                                if (!lines.back().number.empty()) retract(context);
                                lines.pop_back();
                            }
                            break;
                    }
                });
            }

            tree = delimiter != 0 ? inner.parse(delimiter) : inner.parse(stop);
            leftover = inner.pending();

            for (const syntax::Traceback& fault : inner.traceback()) {
                tracebacks.push_back(fault);
            }
        }

        mouth.pop(syntax::semantics::Scope::Type::Equations);

        // `$$` closes with two dollars and the parser stopped at the first, so
        // the second is this formula's and not the next one's. It is read
        // raw, one token and no further, so that a blank after the formula
        // stays in the text as the word space it is.
        if (display && delimiter == '$' && leftover.empty()) {
            leftover = mouth.read();
            if (leftover.text == "$") leftover = syntax::Token{};
        }
        if (!leftover.empty()) {
            mouth.stream().inject(std::span{&leftover, 1});
        }

        // Set at the size of the text around it, as LaTeX's formulas are:
        // `{\\small $x$}` is small, and a heading's formula a heading's size.
        const typography::Font* maths = context.selection.formula();
        if (const typography::Font* text = context.selection.text();
            maths && text && std::abs(text->size() - maths->size()) > 0.01f) {
            if (const typography::Font* sized = context.registry.get({.family = maths->family(), .size = text->size()})) {
                maths = sized;
            }
        }
        const float column = breadth(context);

        // Each row of an `align` or a `gather` on a line of its own, its
        // number -- or its tag -- flush with the right edge.
        if (shape == Layout::Rows && tree && maths &&
            tree->type == syntax::expression::Node::Type::Matrix) {
            const memory::Slice<layout::Node*> built = context.typesetter.rows(tree, *maths);
            if (!built.empty()) {
                const memory::Slice<layout::Node*> labels = arena.allocate<layout::Node*>(built.count);
                float widest = 0.0f;
                for (std::size_t index = 0; index < built.count; ++index) {
                    labels[index] = nullptr;
                    if (index >= lines.size()) continue;
                    const Row& row = lines[index];
                    const std::string text = !row.tag.empty() ? row.tag
                                             : !row.number.empty() ? "(" + row.number + ")"
                                                               : std::string{};
                    labels[index] = label(context, arena, text);
                    if (labels[index]) widest = std::max(widest, labels[index]->box().width);
                }

                const memory::Slice<layout::Node*> set = arena.allocate<layout::Node*>(built.count);
                for (std::size_t index = 0; index < built.count; ++index) {
                    set[index] = line(arena, built[index], labels[index], widest, column);
                }
                lines.clear();
                const float jot = maths->size() * 0.3f;
                return displayed(context, arena, layout::Line::vertical(arena, set, jot), origin);
            }
        }
        if (shape == Layout::Rows) lines.clear();

        // `multline`: one formula over several lines, the first flush left,
        // the last flush right with the number, any between centred.
        if (shape == Layout::Multline && tree && maths &&
            tree->type == syntax::expression::Node::Type::Matrix && tree->columns > 0) {
            const std::size_t count = tree->arguments.count / tree->columns;
            const std::string text = !tagged.empty() ? std::exchange(tagged, std::string{})
                                     : !numbered.empty() ? "(" + std::exchange(numbered, std::string{}) + ")"
                                                     : std::string{};
            layout::Node* mark = label(context, arena, text);
            const float gap = maths->size();

            const memory::Slice<layout::Node*> set = arena.allocate<layout::Node*>(count);
            std::size_t filled = 0;
            for (std::size_t index = 0; index < count; ++index) {
                layout::Node* body = context.typesetter.lower(tree->arguments[index * tree->columns], *maths, 0.0f);
                if (!body) continue;

                const bool first = index == 0 && count > 1;
                const bool last = index + 1 == count;
                std::array<layout::Node*, 5> parts{};
                std::size_t used = 0;

                const auto kern = [&arena](const float width) {
                    auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
                    node->kern({.width = width});
                    return node;
                };
                const auto fill = [&arena] {
                    auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
                    node->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
                    return node;
                };

                parts[used++] = first ? kern(gap) : fill();
                parts[used++] = body;
                parts[used++] = last && count > 1 ? kern(gap) : fill();
                if (last && mark) parts[used++] = mark;

                const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(used);
                std::copy_n(parts.begin(), used, row.begin());
                set[filled++] = layout::Line::horizontal(arena, row, column);
            }
            if (filled > 0) {
                const float jot = maths->size() * 0.3f;
                return displayed(context, arena, layout::Line::vertical(arena, memory::Slice{set.data, filled}, jot),
                                 origin);
            }
        }

        // A numbered equation is set here rather than by the document,
        // because its number shares its line: the expression centred in the
        // column as any display is, and the number flush with the right edge.
        if (display && (!numbered.empty() || !tagged.empty())) {
            const std::string text = !tagged.empty() ? std::exchange(tagged, std::string{})
                                                     : "(" + numbered + ")";
            numbered.clear();

            layout::Node* expression = tree && maths ? context.typesetter.lower(tree, *maths, 0.0f)
                                                      : nullptr;
            if (layout::Node* mark = label(context, arena, text); expression && mark) {
                return displayed(context, arena, line(arena, expression, mark, mark->box().width, column), origin);
            }
        }

        auto* node = arena.compose<syntax::Node>(
            syntax::Node::Type::Expression, std::string_view{}, origin);
        node->expression = tree;
        node->display = display;
        node->face = maths;
        node->tint = context.selection.color();
        return node;
    }

    void Expressions::operator()(syntax::Parser& parser, Context& context) const {
        parser.bind("$", [this, &context](syntax::Parser& parser) {
            syntax::Mouth& mouth = parser.mouth;

            // A second dollar makes it displayed; anything else belongs to the
            // formula and goes back.
            syntax::Token next = mouth.read();
            const bool display = next.text == "$";
            if (!display && !next.empty()) mouth.stream().inject(std::span{&next, 1});

            return enter(parser, context, display, '$', syntax::none);
        });

        parser.bind("\\(", [this, &context](syntax::Parser& parser) {
            return enter(parser, context, false, 0, parser.mouth.lexicon.intern("\\)"));
        });

        parser.bind("\\[", [this, &context](syntax::Parser& parser) {
            return enter(parser, context, true, 0, parser.mouth.lexicon.intern("\\]"));
        });

        // The numbered display and its unnumbered form: each block opens a
        // display on the way in and closes it on the way out, so everything
        // between is the formula. The number is taken on the way in.
        //
        // Watched transparent: `\[` opens the display's own Equations scope,
        // and closing that is what `\]` does when the still-open formula
        // parse reaches it. A second, Environment scope layered around that
        // by \begin/\end would have to close before it to stay nested
        // correctly, which is not an order \end{equation} can arrange -- it
        // is itself what causes the formula parse to finish, several frames
        // further in, by injecting the \] that parse is still waiting to read.
        for (const bool counted : {true, false}) {
            context.blocks.watch(
                counted ? "equation" : "equation*",
                [this, &context, counted](syntax::Mouth& mouth) {
                    if (counted) count(mouth, context);
                    mouth.ingest("\\[");
                },
                [](syntax::Mouth& mouth) { mouth.ingest("\\]"); },
                /*transparent=*/true);
        }

        // amsmath's displays of several lines. Each is a display around one
        // grid -- rows paired around their relations for `align`, one
        // centred column for `gather` and `multline` -- and `\end` closes the
        // grid and the display in one. The grids are the same ones the
        // environments inside a formula open, below.
        struct Display {
            std::string_view name;      ///< The environment.
            std::string_view opening;   ///< What it reads as.
            Layout layout;              ///< How its lines are set.
            bool counted;               ///< True when its lines are numbered.
            bool argued;                ///< True when it takes an argument first, as `alignat` does.
        };
        static constexpr std::array<Display, 14> displays{{
            {"align", "\\[\\aligned{", Layout::Rows, true, false},
            {"align*", "\\[\\aligned{", Layout::Rows, false, false},
            {"flalign", "\\[\\aligned{", Layout::Rows, true, false},
            {"flalign*", "\\[\\aligned{", Layout::Rows, false, false},
            {"alignat", "\\[\\aligned{", Layout::Rows, true, true},
            {"alignat*", "\\[\\aligned{", Layout::Rows, false, true},
            {"xalignat", "\\[\\aligned{", Layout::Rows, true, true},
            {"gather", "\\[\\gathered{", Layout::Rows, true, false},
            {"gather*", "\\[\\gathered{", Layout::Rows, false, false},
            {"eqnarray", "\\[\\array{rcl}{", Layout::Rows, true, false},
            {"eqnarray*", "\\[\\array{rcl}{", Layout::Rows, false, false},
            {"multline", "\\[\\gathered{", Layout::Multline, true, false},
            {"multline*", "\\[\\gathered{", Layout::Multline, false, false},
            {"math", "\\(", Layout::Single, false, false},
        }};

        for (const auto& [name, opening, kind, counted, argued] : displays) {
            const std::string_view closing = kind == Layout::Single ? "\\)" : "}\\]";
            context.blocks.watch(
                name,
                [this, &context, opening, kind, counted, argued](syntax::Mouth& mouth) {
                    if (argued) static_cast<void>(syntax::Argument::text(mouth));
                    setting = kind;
                    counting = counted;
                    // One number for the whole of a `multline`, on its last line.
                    if (kind == Layout::Multline && counted) count(mouth, context);
                    mouth.ingest(opening);
                },
                [closing](syntax::Mouth& mouth) { mouth.ingest(closing); },
                /*transparent=*/true);
        }
        context.blocks.watch(
            "displaymath", [](syntax::Mouth& mouth) { mouth.ingest("\\["); },
            [](syntax::Mouth& mouth) { mouth.ingest("\\]"); }, /*transparent=*/true);

        // The grids amsmath writes as environments inside a formula, each the
        // same grid its command form reads: `\begin{pmatrix}` is `\pmatrix{`,
        // and its `\end` the brace that closes it.
        // nicematrix's matrices are amsmath's, their own keys aside.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 22> grids{{
            {"pmatrix", "\\pmatrix{"}, {"bmatrix", "\\bmatrix{"}, {"Bmatrix", "\\Bmatrix{"},
            {"vmatrix", "\\vmatrix{"}, {"Vmatrix", "\\Vmatrix{"}, {"matrix", "\\matrix{"},
            {"smallmatrix", "\\smallmatrix{"}, {"cases", "\\cases{"}, {"dcases", "\\dcases{"},
            {"rcases", "\\rcases{"}, {"aligned", "\\aligned{"}, {"split", "\\aligned{"},
            {"gathered", "\\gathered{"}, {"alignedat", "\\aligned{"}, {"subarray", "\\substack{"},
            {"multlined", "\\gathered{"}, {"NiceMatrix", "\\matrix{"}, {"pNiceMatrix", "\\pmatrix{"},
            {"bNiceMatrix", "\\bmatrix{"}, {"BNiceMatrix", "\\Bmatrix{"}, {"vNiceMatrix", "\\vmatrix{"},
            {"VNiceMatrix", "\\Vmatrix{"},
        }};
        for (const auto& [name, opening] : grids) {
            const bool argued = name == "alignedat" || name == "subarray";
            const bool keyed = name.ends_with("NiceMatrix");
            context.blocks.watch(
                name,
                [opening, argued, keyed](syntax::Mouth& mouth) {
                    if (argued) static_cast<void>(syntax::Argument::text(mouth));
                    if (keyed) static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                    mouth.ingest(opening);
                },
                [](syntax::Mouth& mouth) { mouth.ingest("}"); },
                /*transparent=*/true);
        }

        // `array` takes its preamble first, as `tabular` does, and reads it
        // the same way: only which columns are left, centred and right
        // matters to a formula.
        context.blocks.watch(
            "array",
            [&context](syntax::Mouth& mouth) {
                static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                std::string letters;
                for (const Tables::Column& column : Tables::preamble(mouth.argument({}, 0))) {
                    letters += column.align;
                }
                mouth.ingest(context.arena.copy("\\array{" + letters + "}{"));
            },
            [](syntax::Mouth& mouth) { mouth.ingest("}"); },
            /*transparent=*/true);

        // A line of an `align` left out of the numbering, and one given a
        // tag of the document's own instead: `\tag{$*$}` prints `(*)`, and
        // `\tag*{A}` prints `A` alone.
        const syntax::Mouth::Handler unnumbered = [this, &context](syntax::Mouth&) {
            if (!lines.empty()) {
                if (!lines.back().number.empty()) retract(context);
                lines.back().number.clear();
            } else if (!numbered.empty()) {
                numbered.clear();
                retract(context);
            }
        };
        parser.mouth.bind("\\nonumber", unnumbered);
        parser.mouth.bind("\\notag", unnumbered);

        parser.mouth.bind("\\tag", [this, &context](syntax::Mouth& mouth) {
            const bool bare = mouth.lookahead().is('*');
            if (bare) mouth.read();
            const std::string text = syntax::Argument::text(mouth);
            const std::string shown = bare ? text : "(" + text + ")";
            context.anchor = text;
            context.kind = "equation";

            if (!lines.empty()) {
                if (!lines.back().number.empty()) retract(context);
                lines.back().number.clear();
                lines.back().tag = shown;
                return;
            }
            if (!numbered.empty()) {
                numbered.clear();
                retract(context);
            }
            tagged = shown;
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound formula primitives");
    }

}
