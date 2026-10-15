/// @file
/// @brief Expression primitives: `$`, `\\(` and `\\[`, the operator table, and
///        amsmath's environments.
///
/// An expression gets an expression parser of its own, taught the operators,
/// run to its closing delimiter and then destroyed -- which is what returns
/// the token after it to the expander instead of losing it.
#include "render/primitives/mathematics/formulas.hpp"
#include "render/primitives/referencing/counters.hpp"
#include "render/primitives/text/styles.hpp"
#include "render/primitives/structure/tables.hpp"
#include "syntax/argument.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include "syntax/expression/node.hpp"
#include "syntax/semantics/scope.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <span>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace render::primitives::mathematics {
    using referencing::Counters;
    using structure::Tables;
    using text::Styles;

    /// @brief A display as the document takes it: the space LaTeX keeps above
    ///        and below one, and the display between.
    /// @param context Engine services, for the body's size.
    /// @param arena   Allocator for the nodes.
    /// @param box     The display, as wide as the column.
    /// @param origin  Where in the source it came from.
    /// @return The three, as one group.
    static syntax::Node *displayed(const Context &context, memory::Arena &arena, layout::Node *box,
                                   const memory::Location origin) {
        const float skip = context.document.configuration.size;
        const memory::Slice<syntax::Node *> parts = arena.allocate<syntax::Node *>(3);
        for (const std::size_t index: {0uz, 2uz}) {
            auto *glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
            glue->glue({.width = skip, .stretch = skip * 0.2f, .shrink = skip * 0.5f});
            parts[index] = directive(arena, glue, origin, true);
        }
        parts[1] = directive(arena, box, origin, true);
        return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, parts);
    }

    Formulas::Formulas(syntax::Lexicon &lexicon) : grammar(lexicon) {
        lexicon.intern("$");
        lexicon.intern("\\(");
        lexicon.intern("\\[");
        lexicon.intern("\\nonumber");
        lexicon.intern("\\notag");
        lexicon.intern("\\tag");

        using Type = syntax::expression::Node::Type;

        /// @brief One operator: what it builds and how tightly it binds.
        struct Rule {
            std::string_view name; ///< What the document writes.
            Type type; ///< What kind of node it makes.
            int weight; ///< How tightly it binds; 0 for a structure.
            bool structural; ///< True when it takes brace arguments, not operands.
        };

        // Loosest first. A relation binds least tightly, so `a + b = c` reads
        // as `(a + b) = c` and not as `a + (b = c)`.
        static constexpr std::array<Rule, 102> table{
            {
                // Relations, which everything else binds tighter than.
                {.name = "=", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "<", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = ">", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\le", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\ge", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\leq", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\geq", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\neq", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\ne", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\equiv", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\approx", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\sim", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\simeq", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\cong", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\propto", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\ll", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\gg", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\lesssim", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\gtrsim", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\in", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\notin", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\subset", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\subseteq", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\supset", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\supseteq", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\to", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\gets", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\mapsto", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\iff", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\implies", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\Rightarrow", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\Leftarrow", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\Leftrightarrow", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\rightarrow", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\leftarrow", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\longrightarrow", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\perp", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\parallel", .type = Type::Binary, .weight = 1, .structural = false},
                {.name = "\\mid", .type = Type::Binary, .weight = 1, .structural = false},

                // Addition, and the set operations that read like it.
                {.name = "+", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "-", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "\\pm", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "\\mp", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "\\cup", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "\\cap", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "\\setminus", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "\\oplus", .type = Type::Binary, .weight = 2, .structural = false},
                {.name = "\\ominus", .type = Type::Binary, .weight = 2, .structural = false},

                // Multiplication, which binds tighter than addition.
                {.name = "*", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "/", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\times", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\cdot", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\div", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\otimes", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\wedge", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\vee", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\circ", .type = Type::Binary, .weight = 3, .structural = false},
                {.name = "\\ast", .type = Type::Binary, .weight = 3, .structural = false},

                // Postfix, which binds tighter than any infix operator.
                {.name = "!", .type = Type::Unary, .weight = 10, .structural = false},

                // Structures, which take brace arguments instead of operands and
                // so sit outside the ordering entirely.
                {.name = "\\frac", .type = Type::Fraction, .weight = 0, .structural = true},
                {.name = "\\dfrac", .type = Type::Fraction, .weight = 0, .structural = true},
                {.name = "\\tfrac", .type = Type::Fraction, .weight = 0, .structural = true},
                {.name = "\\binom", .type = Type::Fraction, .weight = 0, .structural = true},
                {.name = "\\dbinom", .type = Type::Fraction, .weight = 0, .structural = true},
                {.name = "\\tbinom", .type = Type::Fraction, .weight = 0, .structural = true},
                {.name = "\\sqrt", .type = Type::Radical, .weight = 0, .structural = true},

                // Marks over a symbol, the wide ones drawn with the same marks.
                {.name = "\\hat", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\widehat", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\vec", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\overrightarrow", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\bar", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\dot", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\ddot", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\tilde", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\widetilde", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\check", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\breve", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\acute", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\grave", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\mathring", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\dddot", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\ddddot", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\overleftarrow", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\overleftrightarrow", .type = Type::Accent, .weight = 0, .structural = true},

                // A brace, a bracket or a parenthesis stretched over or under what
                // it spans, its label written as a script and set as a limit.
                {.name = "\\overbrace", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\underbrace", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\overbracket", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\underbracket", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\overparen", .type = Type::Accent, .weight = 0, .structural = true},
                {.name = "\\underparen", .type = Type::Accent, .weight = 0, .structural = true},

                // Words inside a formula, read with their spaces and set upright
                // or in the style their name says.
                {.name = "\\text", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textrm", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textup", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textnormal", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textmd", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textbf", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textit", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textsl", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\textsf", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\texttt", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\emph", .type = Type::Text, .weight = 0, .structural = true},
                {.name = "\\mbox", .type = Type::Text, .weight = 0, .structural = true},
            }
        };

        for (const auto &[name, type, weight, structural]: table) {
            grammar.bind(lexicon.intern(name), type, weight, false, structural);
        }

        // The sixteen fixed delimiter sizes: a delimiter follows each one
        // straight after, and is drawn at that size whatever it encloses.
        static constexpr std::array<std::string_view, 16> sizes{
            "\\big", "\\bigl", "\\bigr", "\\bigm", "\\Big", "\\Bigl", "\\Bigr", "\\Bigm",
            "\\bigg", "\\biggl", "\\biggr", "\\biggm", "\\Bigg", "\\Biggl", "\\Biggr", "\\Biggm",
        };
        for (const std::string_view name: sizes) {
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
        for (const std::string_view name: spanning) {
            grammar.declare(lexicon.intern(name), 1);
        }

        // A mark over or under something: two groups, the mark first.
        for (const std::string_view name: {"\\overset", "\\underset", "\\stackrel"}) {
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
        for (const std::string_view name: functions) {
            grammar.declare(lexicon.intern(name), 0);
        }

        // The spaces, which take nothing: `\\!{}_{2}` is a space taken back
        // and then an empty base with a subscript, never a space over `{}`.
        for (const std::string_view name: {"\\,", "\\:", "\\;", "\\!", "\\ ", "\\\n", "\\\r\n", "\\quad", "\\qquad"}) {
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
        for (const std::string_view name: silent) {
            grammar.silence(lexicon.intern(name));
        }
    }

    layout::Node *Formulas::label(const Context &context, memory::Arena &arena, const std::string_view text) {
        if (text.empty()) return nullptr;

        // Upright whatever the text around it is: an equation's number in a
        // theorem is not set in the theorem's italic.
        const typography::Font *around = context.selection.text();
        if (!around) return nullptr;
        const typography::Font *font = Styles::resolve(context, Styles::Cut::Normal, around->size());
        if (!font) font = around;

        const typography::Font *fonts[] = {font};
        const memory::Slice<layout::Node *> shaped =
                context.shaper.shape(memory::Slice{.data = fonts, .count = 1uz}, arena.copy(text), {});
        return layout::Line::horizontal(arena, shaped, 0.0f);
    }

    void Formulas::count(syntax::Mouth &mouth, Context &context) const {
        if (!context.counters) return;
        context.counters->step("equation");
        numbered = Counters::print(mouth, "equation");
        context.anchor = numbered;
        context.kind = "equation";
    }

    void Formulas::retract(const Context &context) {
        if (context.counters) context.counters->set("equation", context.counters->value("equation") - 1);
    }

    layout::Node *Formulas::line(memory::Arena &arena, layout::Node *body, layout::Node *mark,
                                    const float balance, const float column) {
        // A kern as wide as the widest number, on the left, balances the
        // number on the right, so the expression is centred on the column
        // itself and not on what the number leaves of it -- and every row of
        // a numbered grid is centred the same way, whichever row has a number.
        auto *left = arena.compose<layout::Node>(layout::Node::Type::Kern);
        left->kern({.width = balance});

        auto *before = arena.compose<layout::Node>(layout::Node::Type::Glue);
        before->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
        auto *after = arena.compose<layout::Node>(layout::Node::Type::Glue);
        after->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});

        // Too wide to be centred with room for the number either side, it
        // moves left only as far as it has to, half an em short of its
        // number; too wide even for that, its number goes on a line of its
        // own beneath it, flush right, as TeX's \eqno and amsmath set one.
        if (mark && body && body->box().width + 2.0f * balance > column) {
            const float gap = mark->box().height * 0.67f;
            auto *apart = arena.compose<layout::Node>(layout::Node::Type::Kern);
            apart->kern({.width = gap});
            if (body->box().width + mark->box().width + gap <= column) {
                const memory::Slice<layout::Node *> row = arena.allocate<layout::Node *>(4);
                row[0] = before;
                row[1] = body;
                row[2] = apart;
                row[3] = mark;
                return layout::Line::horizontal(arena, row, column);
            }
            const memory::Slice<layout::Node *> top = arena.allocate<layout::Node *>(3);
            top[0] = before;
            top[1] = body;
            top[2] = after;
            auto *push = arena.compose<layout::Node>(layout::Node::Type::Glue);
            push->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
            const memory::Slice<layout::Node *> bottom = arena.allocate<layout::Node *>(2);
            bottom[0] = push;
            bottom[1] = mark;
            const memory::Slice<layout::Node *> both = arena.allocate<layout::Node *>(2);
            both[0] = layout::Line::horizontal(arena, top, column);
            both[1] = layout::Line::horizontal(arena, bottom, column);
            return layout::Line::vertical(arena, both, 2.0f);
        }

        auto *right = arena.compose<layout::Node>(layout::Node::Type::Kern);
        right->kern({.width = balance - (mark ? mark->box().width : 0.0f)});

        const memory::Slice<layout::Node *> row = arena.allocate<layout::Node *>(mark ? 6 : 5);
        row[0] = left;
        row[1] = before;
        row[2] = body;
        row[3] = after;
        row[4] = right;
        if (mark) row[5] = mark;
        return layout::Line::horizontal(arena, row, column);
    }

    syntax::Node *Formulas::enter(
        syntax::Parser &parser,
        Context &context,
        const bool display,
        const char delimiter,
        const syntax::Symbol stop
    ) const {
        syntax::Mouth &mouth = parser.mouth;
        memory::Arena &arena = parser.arena;
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
            static constexpr std::array<std::string_view, 5> pictures{
                "tikzcd", "CD", "quantikz", "tikzpicture", "forest"
            };
            // xy-pic's spacing may run on from its name: `\xymatrix@C=1em`.
            if (std::ranges::contains(pictures, name) || first.text.starts_with("\\xymatrix") ||
                first.text.starts_with("\\Qcircuit")) {
                setting = Layout::Single;
                const syntax::Symbol closer = delimiter == '$' ? mouth.lexicon.intern("$") : stop;
                syntax::Symbol matched = syntax::none;
                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node *> read = parser.parse(0, std::span{&closer, 1}, matched);
                stamp(read, context);
                mouth.pop(syntax::semantics::Scope::Type::Group);
                if (delimiter == '$' && mouth.lookahead().text == "$") static_cast<void>(mouth.read());

                std::vector<layout::Node *> set;
                for (const syntax::Node *child: read) compose(set, child, context);
                const memory::Slice<layout::Node *> row = arena.allocate<layout::Node *>(set.size());
                std::ranges::copy(set, row.begin());
                layout::Node *body = layout::Line::horizontal(arena, row, 0.0f);
                const std::string text = !tagged.empty()
                                             ? std::exchange(tagged, std::string{})
                                             : !numbered.empty()
                                                   ? std::format("({})", std::exchange(numbered, std::string{}))
                                                   : std::string{};
                layout::Node *mark = text.empty() ? nullptr : label(context, arena, text);
                return displayed(context, arena,
                                 line(arena, body, mark, mark ? mark->box().width : 0.0f, breadth(context)),
                                 origin);
            }
        }

        mouth.push(syntax::semantics::Scope::Type::Equations);

        syntax::expression::Node *tree = nullptr;
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
                mouth, grammar, arena,
                display
                    ? syntax::expression::Node::Style::Display
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
                            if (counting &&context.counters)
                            {
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

            for (const syntax::Traceback &fault: inner.traceback()) {
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
        const typography::Font *maths = context.selection.formula();
        if (const typography::Font *text = context.selection.text();
            maths && text && std::abs(text->size() - maths->size()) > 0.01f) {
            if (const typography::Font *sized = context.registry.get({
                .family = maths->family(), .size = text->size()
            })) {
                maths = sized;
            }
        }
        const float column = breadth(context);

        // Each row of an `align` or a `gather` on a line of its own, its
        // number -- or its tag -- flush with the right edge.
        if (shape == Layout::Rows && tree && maths &&
            tree->type == syntax::expression::Node::Type::Matrix) {
            const memory::Slice<layout::Node *> built = context.typesetter.rows(tree, *maths);
            if (!built.empty()) {
                const memory::Slice<layout::Node *> labels = arena.allocate<layout::Node *>(built.count);
                float widest = 0.0f;
                for (std::size_t index = 0; index < built.count; ++index) {
                    labels[index] = nullptr;
                    if (index >= lines.size()) continue;
                    const Row &row = lines[index];
                    const std::string text = !row.tag.empty()
                                                 ? row.tag
                                                 : !row.number.empty()
                                                       ? std::format("({})", row.number)
                                                       : std::string{};
                    labels[index] = label(context, arena, text);
                    if (labels[index]) widest = std::max(widest, labels[index]->box().width);
                }

                const memory::Slice<layout::Node *> set = arena.allocate<layout::Node *>(built.count);
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
            const std::string text = !tagged.empty()
                                         ? std::exchange(tagged, std::string{})
                                         : !numbered.empty()
                                               ? std::format("({})", std::exchange(numbered, std::string{}))
                                               : std::string{};
            layout::Node *mark = label(context, arena, text);
            const float gap = maths->size();

            const memory::Slice<layout::Node *> set = arena.allocate<layout::Node *>(count);
            std::size_t filled = 0;
            for (std::size_t index = 0; index < count; ++index) {
                layout::Node *body = context.typesetter.lower(tree->arguments[index * tree->columns], *maths, 0.0f);
                if (!body) continue;

                const bool first = index == 0 && count > 1;
                const bool last = index + 1 == count;
                std::array<layout::Node *, 5> parts{};
                std::size_t used = 0;

                const auto kern = [&arena](const float width) {
                    auto *node = arena.compose<layout::Node>(layout::Node::Type::Kern);
                    node->kern({.width = width});
                    return node;
                };
                const auto fill = [&arena] {
                    auto *node = arena.compose<layout::Node>(layout::Node::Type::Glue);
                    node->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
                    return node;
                };

                parts[used++] = first ? kern(gap) : fill();
                parts[used++] = body;
                parts[used++] = last && count > 1 ? kern(gap) : fill();
                if (last && mark) parts[used++] = mark;

                const memory::Slice<layout::Node *> row = arena.allocate<layout::Node *>(used);
                std::copy_n(parts.begin(), used, row.begin());
                set[filled++] = layout::Line::horizontal(arena, row, column);
            }
            if (filled > 0) {
                const float jot = maths->size() * 0.3f;
                const memory::Slice<layout::Node *> stacked{.data = set.data, .count = filled};
                return displayed(context, arena, layout::Line::vertical(arena, stacked, jot), origin);
            }
        }

        // A numbered equation is set here rather than by the document,
        // because its number shares its line: the expression centred in the
        // column as any display is, and the number flush with the right edge.
        if (display && (!numbered.empty() || !tagged.empty())) {
            const std::string text = !tagged.empty()
                                         ? std::exchange(tagged, std::string{})
                                         : std::format("({})", numbered);
            numbered.clear();

            layout::Node *expression = tree && maths
                                           ? context.typesetter.lower(tree, *maths, 0.0f)
                                           : nullptr;
            if (layout::Node *mark = label(context, arena, text); expression && mark) {
                return displayed(context, arena, line(arena, expression, mark, mark->box().width, column), origin);
            }
        }

        auto *node = arena.compose<syntax::Node>(
            syntax::Node::Type::Expression, std::string_view{}, origin);
        node->expression = tree;
        node->display = display;
        node->face = maths;
        node->tint = context.selection.color();
        return node;
    }

    void Formulas::operator()(syntax::Parser &parser, Context &context) const {
        parser.bind("$", [this, &context](syntax::Parser &parser) {
            syntax::Mouth &mouth = parser.mouth;

            // A second dollar makes it displayed; anything else belongs to the
            // formula and goes back.
            syntax::Token next = mouth.read();
            const bool display = next.text == "$";
            if (!display && !next.empty()) mouth.stream().inject(std::span{&next, 1});

            return enter(parser, context, display, '$', syntax::none);
        });

        parser.bind("\\(", [this, &context](syntax::Parser &parser) {
            return enter(parser, context, false, 0, parser.mouth.lexicon.intern("\\)"));
        });

        parser.bind("\\[", [this, &context](syntax::Parser &parser) {
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
        for (const bool counted: {true, false}) {
            context.environments.watch(
                counted ? "equation" : "equation*",
                [this, &context, counted](syntax::Mouth &mouth) {
                    if (counted) count(mouth, context);
                    mouth.ingest("\\[");
                },
                [](syntax::Mouth &mouth) { mouth.ingest("\\]"); },
                /*transparent=*/true);
        }

        // amsmath's displays of several lines. Each is a display around one
        // grid -- rows paired around their relations for `align`, one
        // centred column for `gather` and `multline` -- and `\end` closes the
        // grid and the display in one. The grids are the same ones the
        // environments inside a formula open, below.
        struct Display {
            std::string_view name; ///< The environment.
            std::string_view opening; ///< What it reads as.
            Layout layout; ///< How its lines are set.
            bool counted; ///< True when its lines are numbered.
            bool argued; ///< True when it takes an argument first, as `alignat` does.
        };
        static constexpr std::array<Display, 14> displays{
            {
                {.name = "align", .opening = "\\[\\aligned{", .layout = Layout::Rows,
                 .counted = true, .argued = false},
                {.name = "align*", .opening = "\\[\\aligned{", .layout = Layout::Rows,
                 .counted = false, .argued = false},
                {.name = "flalign", .opening = "\\[\\aligned{", .layout = Layout::Rows,
                 .counted = true, .argued = false},
                {.name = "flalign*", .opening = "\\[\\aligned{", .layout = Layout::Rows,
                 .counted = false, .argued = false},
                {.name = "alignat", .opening = "\\[\\aligned{", .layout = Layout::Rows,
                 .counted = true, .argued = true},
                {.name = "alignat*", .opening = "\\[\\aligned{", .layout = Layout::Rows,
                 .counted = false, .argued = true},
                {.name = "xalignat", .opening = "\\[\\aligned{", .layout = Layout::Rows,
                 .counted = true, .argued = true},
                {.name = "gather", .opening = "\\[\\gathered{", .layout = Layout::Rows,
                 .counted = true, .argued = false},
                {.name = "gather*", .opening = "\\[\\gathered{", .layout = Layout::Rows,
                 .counted = false, .argued = false},
                {.name = "eqnarray", .opening = "\\[\\array{rcl}{", .layout = Layout::Rows,
                 .counted = true, .argued = false},
                {.name = "eqnarray*", .opening = "\\[\\array{rcl}{", .layout = Layout::Rows,
                 .counted = false, .argued = false},
                {.name = "multline", .opening = "\\[\\gathered{", .layout = Layout::Multline,
                 .counted = true, .argued = false},
                {.name = "multline*", .opening = "\\[\\gathered{", .layout = Layout::Multline,
                 .counted = false, .argued = false},
                {.name = "math", .opening = "\\(", .layout = Layout::Single,
                 .counted = false, .argued = false},
            }
        };

        for (const auto &[name, opening, kind, counted, argued]: displays) {
            const std::string_view closing = kind == Layout::Single ? "\\)" : "}\\]";
            context.environments.watch(
                name,
                [this, &context, opening, kind, counted, argued](syntax::Mouth &mouth) {
                    if (argued) static_cast<void>(syntax::Argument::text(mouth));
                    setting = kind;
                    counting = counted;
                    // One number for the whole of a `multline`, on its last line.
                    if (kind == Layout::Multline && counted) count(mouth, context);
                    mouth.ingest(opening);
                },
                [closing](syntax::Mouth &mouth) { mouth.ingest(closing); },
                /*transparent=*/true);
        }
        context.environments.watch(
            "displaymath", [](syntax::Mouth &mouth) { mouth.ingest("\\["); },
            [](syntax::Mouth &mouth) { mouth.ingest("\\]"); }, /*transparent=*/true);

        // The grids amsmath writes as environments inside a formula, each the
        // same grid its command form reads: `\begin{pmatrix}` is `\pmatrix{`,
        // and its `\end` the brace that closes it.
        // nicematrix's matrices are amsmath's, their own keys aside.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 22> grids{
            {
                {"pmatrix", "\\pmatrix{"}, {"bmatrix", "\\bmatrix{"}, {"Bmatrix", "\\Bmatrix{"},
                {"vmatrix", "\\vmatrix{"}, {"Vmatrix", "\\Vmatrix{"}, {"matrix", "\\matrix{"},
                {"smallmatrix", "\\smallmatrix{"}, {"cases", "\\cases{"}, {"dcases", "\\dcases{"},
                {"rcases", "\\rcases{"}, {"aligned", "\\aligned{"}, {"split", "\\aligned{"},
                {"gathered", "\\gathered{"}, {"alignedat", "\\aligned{"}, {"subarray", "\\substack{"},
                {"multlined", "\\gathered{"}, {"NiceMatrix", "\\matrix{"}, {"pNiceMatrix", "\\pmatrix{"},
                {"bNiceMatrix", "\\bmatrix{"}, {"BNiceMatrix", "\\Bmatrix{"}, {"vNiceMatrix", "\\vmatrix{"},
                {"VNiceMatrix", "\\Vmatrix{"},
            }
        };
        for (const auto &[name, opening]: grids) {
            const bool argued = name == "alignedat" || name == "subarray";
            const bool keyed = name.ends_with("NiceMatrix");
            context.environments.watch(
                name,
                [opening, argued, keyed](syntax::Mouth &mouth) {
                    if (argued) static_cast<void>(syntax::Argument::text(mouth));
                    if (keyed) static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                    mouth.ingest(opening);
                },
                [](syntax::Mouth &mouth) { mouth.ingest("}"); },
                /*transparent=*/true);
        }

        // `array` takes its preamble first, as `tabular` does, and reads it
        // the same way: only which columns are left, centred and right
        // matters to a formula.
        context.environments.watch(
            "array",
            [&context](syntax::Mouth &mouth) {
                static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                std::string letters;
                for (const Tables::Column &column: Tables::preamble(mouth.argument({}, 0))) {
                    letters += column.align;
                }
                mouth.ingest(context.arena.copy(std::format("\\array{{{}}}{{", letters)));
            },
            [](syntax::Mouth &mouth) { mouth.ingest("}"); },
            /*transparent=*/true);

        // A line of an `align` left out of the numbering, and one given a
        // tag of the document's own instead: `\tag{$*$}` prints `(*)`, and
        // `\tag*{A}` prints `A` alone.
        const syntax::Mouth::Handler unnumbered = [this, &context](syntax::Mouth &) {
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

        parser.mouth.bind("\\tag", [this, &context](syntax::Mouth &mouth) {
            const bool bare = mouth.lookahead().is('*');
            if (bare) mouth.read();
            const std::string text = syntax::Argument::text(mouth);
            const std::string shown = bare ? text : std::format("({})", text);
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
