/// @file
/// @brief Box primitives: TeX's `\\hbox`, `\\vbox` and `\\vtop`, and LaTeX's
///        boxes over them -- `\\mbox`, `\\makebox`, `\\fbox`, `\\framebox`,
///        `\\colorbox`, `\\fcolorbox`, `\\raisebox`, `\\parbox`, `minipage`,
///        the phantoms, the saved boxes and the lengths measured off a box,
///        and the framed blocks and columns of the packages and of beamer.
///
/// A box opens a scope of its own before parsing its contents, so a font or a
/// register set inside one is set back when it closes -- which is what makes a
/// box safe to write anything in.
#include "render/primitives/boxes.hpp"
#include "render/primitives/colors.hpp"
#include "layout/line.hpp"
#include "layout/paragraph.hpp"
#include "logger.hpp"

#include "syntax/argument.hpp"
#include "syntax/number.hpp"
#include "syntax/semantics/scope.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace render::primitives {

    Boxes::Boxes(syntax::Lexicon& lexicon) noexcept {
        made = {lexicon.intern("\\hbox"), lexicon.intern("\\vbox"), lexicon.intern("\\vtop"),
                lexicon.intern("\\mbox")};
        // No document can write this name: a colon ends a control word.
        finish = lexicon.intern("\\minipage:end");
        framing = lexicon.intern("\\framed:begin");
        framed = lexicon.intern("\\framed:end");
        start = lexicon.intern("\\minipage:begin");
    }

    void Boxes::operator()(syntax::Parser& parser, Context& context) const {
        using Registers = syntax::semantics::Registers;
        using Justification = layout::Node::Justification;
        using Command = layout::Node::Directive::Command;
        syntax::Lexicon& lexicon = parser.mouth.lexicon;

        // \fboxsep and \fboxrule, as registers, so \setlength reaches them:
        // the room a frame leaves around what it holds, and its thickness.
        const std::size_t separation = Registers::reserved + 8;
        const std::size_t thickness = Registers::reserved + 9;
        context.registers.bind(lexicon.intern("\\fboxsep"), Registers::Type::Dimension, separation);
        context.registers.bind(lexicon.intern("\\fboxrule"), Registers::Type::Dimension, thickness);
        context.registers.set(Registers::Type::Dimension, separation, 3 * 65536, true);
        context.registers.set(Registers::Type::Dimension, thickness, 26214, true);   // 0.4pt
        // \linewidth, which a box sets for what is inside it.
        const std::size_t line = Registers::reserved + 1;

        // What a box holds: one brace group, read in a scope of its own, and
        // set in the face it was read in -- which is then put back, since a
        // face is not a register the scope restores.
        const auto contents = [this, &context](syntax::Parser& parser, std::string_view name,
                                               std::optional<float> width = std::nullopt) {
            syntax::Mouth& mouth = parser.mouth;
            while (mouth.lookahead().category == syntax::Catcodes::Category::Space) mouth.read();

            syntax::Token open = mouth.read();
            if (!open.is(syntax::Catcodes::Category::Group, '{')) {
                if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                tracebacks.emplace_back(syntax::Traceback::Type::Group, open.location,
                                         std::format("{} needs a brace group for its contents", name));
                return memory::Slice<syntax::Node*>{};
            }

            const typography::Font* text = context.selection.text();
            const typography::Font* formula = context.selection.formula();
            mouth.push(syntax::semantics::Scope::Type::Box);
            if (width) {
                context.registers.set(Registers::Type::Dimension, line,
                                      static_cast<std::int32_t>(*width * 65536.0f), false);
            }
            const memory::Slice<syntax::Node*> children = parser.parse('}');
            mouth.pop(syntax::semantics::Scope::Type::Box);
            stamp(children, context);
            context.selection.text(text);
            context.selection.formula(formula);
            return children;
        };

        // A row of what was read: the contents as layout nodes, set to a
        // width or at their own.
        const auto row = [&context](memory::Arena& arena, const memory::Slice<syntax::Node*> children,
                                    const float target = 0.0f) {
            std::vector<layout::Node*> nodes;
            nodes.reserve(children.count);
            for (const syntax::Node* child : children) gather(nodes, child, context);
            const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(nodes.size());
            std::ranges::copy(nodes, list.begin());
            return layout::Line::horizontal(arena, list, target);
        };

        // The same, in a row of the given width with the contents where the
        // position letter puts them -- c centred, l and r at one side, s
        // spread by their own spaces. For the first three the contents keep
        // their own width and kerns make up the rest, negative where the box
        // is the narrower, so one narrower than its contents overlaps what is
        // beside it -- `\\makebox[0pt][l]` included, which a row set to a
        // width of nothing would take for its natural width: LaTeX's \makebox.
        const auto placed = [&context](memory::Arena& arena, const memory::Slice<syntax::Node*> children,
                                       const float target, const char position) {
            std::vector<layout::Node*> nodes;
            for (const syntax::Node* child : children) gather(nodes, child, context);
            memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(nodes.size());
            std::ranges::copy(nodes, list.begin());
            if (position == 's') return layout::Line::horizontal(arena, list, target);

            layout::Node* inner = layout::Line::horizontal(arena, list, 0.0f);
            const float room = target - inner->box().width;
            const float before = position == 'r' ? room : position == 'c' ? room / 2.0f : 0.0f;
            list = arena.allocate<layout::Node*>(3);
            list[0] = arena.compose<layout::Node>(layout::Node::Type::Kern);
            list[0]->kern({.width = before});
            list[1] = inner;
            list[2] = arena.compose<layout::Node>(layout::Node::Type::Kern);
            list[2]->kern({.width = room - before});
            return layout::Line::horizontal(arena, list, target);
        };

        // `[width][position]`, as \makebox, \framebox and \savebox read them.
        const auto options = [&context](syntax::Mouth& mouth) -> std::pair<std::optional<float>, char> {
            const syntax::Mouth::Parameter optional{.optional = true};
            std::string written;
            for (const syntax::Token& token : mouth.argument(optional, 0)) written += token.text;
            std::string position;
            for (const syntax::Token& token : mouth.argument(optional, 0)) position += token.text;
            return {written.empty() ? std::nullopt : std::optional(measure(written, mouth, context)),
                    position.empty() ? 'c' : position.front()};
        };

        // A frame around a box, a fill behind it, or both: \fboxsep of room
        // all round, then \fboxrule of rule. Four rules and the box, drawn
        // as one row: the top and bottom edges are rules of the frame's full
        // width lifted or dropped to their place, the pen moved back after
        // each.
        const auto frame = [&context](memory::Arena& arena, layout::Node* inner, const float rule,
                                      const layout::Node::Color edge,
                                      const std::optional<layout::Node::Color> fill) {
            const float room = static_cast<float>(context.registers.get(Registers::Type::Dimension, separation)) /
                               65536.0f;
            const layout::Node::Box& shape = inner->box();
            const float width = shape.width + 2.0f * (room + rule);
            const float height = shape.height + room + rule;
            const float depth = shape.depth + room + rule;
            const float across = width - 2.0f * rule;

            std::vector<layout::Node*> nodes;
            const auto bar = [&](const layout::Node::Rule& value) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Rule);
                node->rule(value);
                nodes.push_back(node);
            };
            const auto back = [&](const float amount) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
                node->kern({.width = amount});
                nodes.push_back(node);
            };

            if (fill) {
                bar({.width = width, .height = height, .depth = depth, .color = *fill});
                back(-width);
            }
            if (rule > 0.0f) {
                bar({.width = rule, .height = height, .depth = depth, .color = edge});
                bar({.width = across, .height = height, .depth = rule - height, .color = edge});
                back(-across);
                bar({.width = across, .height = rule - depth, .depth = depth, .color = edge});
                back(-across);
            }
            back(room);
            nodes.push_back(inner);
            back(room);
            if (rule > 0.0f) bar({.width = rule, .height = height, .depth = depth, .color = edge});

            const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(nodes.size());
            std::ranges::copy(nodes, list.begin());
            return layout::Line::horizontal(arena, list, 0.0f);
        };
        const auto rule = [&context] {
            return static_cast<float>(context.registers.get(Registers::Type::Dimension, thickness)) / 65536.0f;
        };
        const auto tint = [&context](syntax::Mouth& mouth) {
            // `[model]{spec}` names its model the way \textcolor's does;
            // the spec is read as a name or a mixture either way.
            static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
            const graphics::Color color = Colors::resolve(syntax::Argument::text(mouth), context.variables);
            return layout::Node::Color{color.r, color.g, color.b, color.alpha};
        };

        // A column of paragraphs, set from what was parsed the way a
        // document's own text is: a paragraph break ends one, the switches
        // -- \centering, \raggedright, \leftskip -- shape the ones after
        // them, and a display or a vertical space stands between them as it
        // is. Consecutive blocks sit a baseline apart where they can, and a
        // point apart where they cannot.
        const auto column = [&context](memory::Arena& arena, const memory::Slice<syntax::Node*> children,
                                       const float width) {
            const float leading = context.document.configuration.leading;
            std::vector<layout::Node*> blocks;
            std::vector<layout::Node*> material;
            std::vector<std::tuple<Justification, float, float>> kept;
            Justification justification = Justification::Full;
            float left = 0.0f;
            float right = 0.0f;

            // The baseline at a block's top and at its bottom: its first
            // line's height and its last line's depth.
            const auto top = [](const layout::Node* block) {
                if (block->type != layout::Node::Type::Box) return layout::Line::extent(block);
                const layout::Node::Box& shape = block->box();
                if (shape.alignment == layout::Node::Alignment::Vertical && !shape.list.empty() && shape.list[0] &&
                    shape.list[0]->type == layout::Node::Type::Box) {
                    return shape.list[0]->box().height;
                }
                return shape.height;
            };
            const auto bottom = [](const layout::Node* block) {
                if (block->type != layout::Node::Type::Box) return 0.0f;
                const layout::Node::Box& shape = block->box();
                if (shape.alignment == layout::Node::Alignment::Vertical && !shape.list.empty()) {
                    const layout::Node* last = shape.list[shape.list.size() - 1];
                    return last && last->type == layout::Node::Type::Box ? last->box().depth : 0.0f;
                }
                return shape.depth;
            };
            const auto append = [&](layout::Node* block) {
                const bool space = block->type == layout::Node::Type::Glue ||
                                   block->type == layout::Node::Type::Kern;
                if (!blocks.empty() && !space) {
                    const layout::Node* before = blocks.back();
                    if (before->type == layout::Node::Type::Box) {
                        auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
                        glue->glue({.width = std::max(leading - bottom(before) - top(block), 1.0f)});
                        blocks.push_back(glue);
                    }
                }
                blocks.push_back(block);
            };
            const auto settle = [&] {
                // Blanks at either end of a paragraph are the source's layout.
                while (!material.empty() && material.back()->type == layout::Node::Type::Glue) material.pop_back();
                std::size_t first = 0;
                while (first < material.size() && material[first]->type == layout::Node::Type::Glue) ++first;
                if (first == material.size()) {
                    material.clear();
                    return;
                }
                const memory::Slice<layout::Node*> pieces = arena.allocate<layout::Node*>(material.size() - first);
                std::copy(material.begin() + static_cast<std::ptrdiff_t>(first), material.end(), pieces.begin());
                material.clear();
                layout::Paragraph set(arena, pieces, justification, left, right);
                set.layout(arena, width, leading);
                if (set.node()) append(set.node());
            };

            const auto walk = [&](this const auto& self, const memory::Slice<syntax::Node*> nodes) -> void {
                std::vector<layout::Node*> gathered;
                for (const syntax::Node* child : nodes) {
                    if (!child) continue;
                    if (child->type == syntax::Node::Type::Group) {
                        self(child->nodes);
                        continue;
                    }
                    if (child->type == syntax::Node::Type::Paragraph) {
                        settle();
                        continue;
                    }
                    gathered.clear();
                    gather(gathered, child, context);
                    for (layout::Node* node : gathered) {
                        if (node->type == layout::Node::Type::Directive) {
                            const layout::Node::Directive& order = node->directive();
                            if (order.command == Command::Align) justification = order.justification;
                            else if (order.command == Command::Margin) (order.trailing ? right : left) = order.width;
                            else if (order.command == Command::Save) kept.emplace_back(justification, left, right);
                            else if (order.command == Command::Restore && !kept.empty()) {
                                std::tie(justification, left, right) = kept.back();
                                kept.pop_back();
                            }
                            continue;
                        }
                        if (child->display) {
                            settle();
                            append(node);
                            continue;
                        }
                        material.push_back(node);
                    }
                }
            };
            walk(children);
            settle();

            const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(blocks.size());
            std::ranges::copy(blocks, list.begin());
            layout::Node* box = layout::Line::vertical(arena, list, 0.0f);
            layout::Node::Box shape = box->box();
            shape.width = width;
            box->box(shape);
            return box;
        };

        // A column hung on the line by the position letter: its first
        // baseline on the line's for t, its last for b, and its middle on the
        // formula axis otherwise, a quarter of an em up -- LaTeX's \vcenter.
        const auto hung = [&context](memory::Arena& arena, layout::Node* box, const char position) {
            layout::Node::Box shape = box->box();
            const float total = shape.height + shape.depth;
            const auto baseline = [&](const bool first) {
                if (shape.list.empty()) return 0.0f;
                if (first) {
                    const layout::Node* head = shape.list[0];
                    return head && head->type == layout::Node::Type::Box ? head->box().height : 0.0f;
                }
                const layout::Node* tail = shape.list[shape.list.size() - 1];
                return total - (tail && tail->type == layout::Node::Type::Box ? tail->box().depth : 0.0f);
            };
            const float em = context.selection.text() ? context.selection.text()->size() : 10.0f;
            shape.shift = position == 't' ? -baseline(true)
                        : position == 'b' ? -baseline(false)
                                          : -(total * 0.5f + em * 0.25f);
            box->box(shape);
            const memory::Slice<layout::Node*> only = arena.allocate<layout::Node*>(1);
            only[0] = box;
            return layout::Line::horizontal(arena, only, 0.0f);
        };

        parser.bind("\\hbox", [this, &context, contents, row](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            const memory::Location origin = mouth.lookahead().location;

            // `to <dimension>`. The lexer emits one token per character, so
            // the keyword is two of them; neither is consumed unless both are
            // there, so a box that does not say `to` keeps its contents.
            while (mouth.lookahead().category == syntax::Catcodes::Category::Space) mouth.read();
            float target = 0.0f;
            if (mouth.lookahead(0).is('t') && mouth.lookahead(1).is('o')) {
                mouth.read();
                mouth.read();
                if (const auto scanned = syntax::Number::dimension(mouth, context.registers)) {
                    target = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
                } else {
                    tracebacks.emplace_back(syntax::Traceback::Type::Dimension, origin, "\\hbox to needs a dimension");
                }
            }
            return directive(parser.arena, row(parser.arena, contents(parser, "\\hbox"), target), origin);
        });

        // `\\vbox` and `\\vtop` differ in where their reference point sits,
        // and a vertical box here always hangs from its top edge -- which is
        // what `\\vtop` asks for. They are the same primitive until a box's
        // depth can be moved.
        const syntax::Parser::Handler stacked = [&context, contents](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            std::vector<layout::Node*> nodes;
            for (const syntax::Node* child : contents(parser, "\\vbox")) gather(nodes, child, context);
            const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(nodes.size());
            std::ranges::copy(nodes, list.begin());
            return directive(arena, layout::Line::vertical(arena, list, 0.0f), origin);
        };
        parser.bind("\\vbox", stacked);
        parser.bind("\\vtop", stacked);

        // LaTeX's boxes. \mbox is a row at its own width, and \makebox one
        // of a given width with its contents placed in it.
        parser.bind("\\mbox", [contents, row](syntax::Parser& parser) -> syntax::Node* {
            const memory::Location origin = parser.mouth.lookahead().location;
            return directive(parser.arena, row(parser.arena, contents(parser, "\\mbox")), origin);
        });

        parser.bind("\\makebox", [contents, row, placed, options](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            const auto [width, position] = options(parser.mouth);
            const memory::Slice<syntax::Node*> children = contents(parser, "\\makebox");
            return directive(arena, width ? placed(arena, children, *width, position) : row(arena, children), origin);
        });

        parser.bind("\\fbox", [contents, row, frame, rule](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            return directive(arena, frame(arena, row(arena, contents(parser, "\\fbox")), rule(), {}, std::nullopt),
                             origin);
        });

        parser.bind("\\framebox", [contents, row, placed, options, frame, rule](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            const auto [width, position] = options(parser.mouth);
            const memory::Slice<syntax::Node*> children = contents(parser, "\\framebox");
            layout::Node* inner = width ? placed(arena, children, *width, position) : row(arena, children);
            return directive(arena, frame(arena, inner, rule(), {}, std::nullopt), origin);
        });

        parser.bind("\\colorbox", [contents, row, frame, tint](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            const layout::Node::Color fill = tint(parser.mouth);
            return directive(arena, frame(arena, row(arena, contents(parser, "\\colorbox")), 0.0f, {}, fill),
                             origin);
        });

        parser.bind("\\fcolorbox", [contents, row, frame, rule, tint](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            const layout::Node::Color edge = tint(parser.mouth);
            const layout::Node::Color fill = tint(parser.mouth);
            return directive(arena, frame(arena, row(arena, contents(parser, "\\fcolorbox")), rule(), edge, fill),
                             origin);
        });

        // \raisebox{lift}[height][depth]{...}: the row moved up by the lift,
        // and said to be as tall and as deep as the two options, when given.
        parser.bind("\\raisebox", [&context, contents, row](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            const float lift = measure(syntax::Argument::text(mouth), mouth, context);
            std::array<std::optional<float>, 2> claimed{};
            for (std::optional<float>& value : claimed) {
                std::string written;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    written += token.text;
                }
                if (!written.empty()) value = measure(written, mouth, context);
            }

            layout::Node* inner = row(arena, contents(parser, "\\raisebox"));
            layout::Node::Box shape = inner->box();
            shape.shift = -lift;
            inner->box(shape);
            const memory::Slice<layout::Node*> only = arena.allocate<layout::Node*>(1);
            only[0] = inner;
            layout::Node* outer = layout::Line::horizontal(arena, only, 0.0f);
            layout::Node::Box around = outer->box();
            if (claimed[0]) around.height = *claimed[0];
            if (claimed[1]) around.depth = *claimed[1];
            outer->box(around);
            return directive(arena, outer, origin);
        });

        // The phantoms: a box as big as its contents with nothing in it --
        // \phantom both ways, \hphantom only across and \vphantom only up
        // and down.
        for (const auto& [name, across, upright] : {std::tuple{"\\phantom", true, true},
                                                    std::tuple{"\\hphantom", true, false},
                                                    std::tuple{"\\vphantom", false, true}}) {
            parser.bind(name, [contents, row, name, across, upright](syntax::Parser& parser) -> syntax::Node* {
                memory::Arena& arena = parser.arena;
                const memory::Location origin = parser.mouth.lookahead().location;
                layout::Node* box = row(arena, contents(parser, name));
                layout::Node::Box shape = box->box();
                shape.list = {};
                if (!across) shape.width = 0.0f;
                if (!upright) shape.height = shape.depth = 0.0f;
                box->box(shape);
                return directive(arena, box, origin);
            });
        }

        // \parbox[position]{width}{...}: paragraphs set to a width, hung on
        // the line by the position letter. The height and inner position a
        // LaTeX \parbox may also take are read and let go.
        parser.bind("\\parbox", [&context, contents, column, hung](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            std::string position;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                position += token.text;
            }
            for (int skipped = 0; skipped < 2 && mouth.lookahead().is('['); ++skipped) {
                static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
            }
            const float width = measure(syntax::Argument::text(mouth), mouth, context);
            const memory::Slice<syntax::Node*> children = contents(parser, "\\parbox", width);
            return directive(arena, hung(arena, column(arena, children, width), position.empty() ? 'c' : position[0]),
                             origin);
        });

        // The framed blocks: framed's framed, shaded and leftbar, mdframed's
        // and tcolorbox's. Each is a column the width of the line less its
        // frame, set as a minipage is and framed as \fbox frames, in the
        // colors its options name:
        //   \begin{mdframed}[backgroundcolor=yellow!10, linecolor=red]
        //   \begin{tcolorbox}[colback=white, colframe=blue, title=Note]
        // tcolorbox's title sits in a bar of the frame's color above the body,
        // as beamer's blocks' do: a block's in the structure's color, an
        // alert's red and an example's green, over a tint of it and no rule,
        // with a little room above and below.
        // A block's option, the last `key=value` among its commas that names
        // it -- so a box's own options win over \tcbset's before them.
        const auto setting = [](const std::string& settings, const std::string_view key) -> std::string {
            std::string found;
            std::size_t depth = 0;
            std::size_t begin = 0;
            for (std::size_t at = 0; at <= settings.size(); ++at) {
                if (at < settings.size()) {
                    if (settings[at] == '{') ++depth;
                    else if (settings[at] == '}' && depth > 0) --depth;
                    if (settings[at] != ',' || depth > 0) continue;
                }
                std::string_view piece = std::string_view(settings).substr(begin, at - begin);
                begin = at + 1;
                while (!piece.empty() && piece.front() == ' ') piece.remove_prefix(1);
                const std::size_t equals = piece.find('=');
                std::string_view name = piece.substr(0, equals);
                while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
                if (name != key || equals == std::string_view::npos) continue;
                std::string_view value = piece.substr(equals + 1);
                while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
                while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
                if (value.size() >= 2 && value.front() == '{' && value.back() == '}') value = value.substr(1, value.size() - 2);
                found = value;
            }
            return found;
        };
        // What an argument holds, a control word keeping the space that ends it.
        const auto spelled = [](const std::vector<syntax::Token>& tokens) {
            std::string text;
            for (const syntax::Token& token : tokens) {
                text += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
            }
            return text;
        };

        // A framed block opening. Its rule and the room inside it, as each
        // package draws them: a shaded block has no rule, a left bar a thick
        // one, and tcolorbox's what `boxrule` and `left` say; the column
        // inside as wide as the line leaves, or as tcolorbox's `width` does.
        const auto enclose = [this, &context, setting](syntax::Mouth& mouth, const std::string_view kind,
                                                       std::string options, const bool beamer) {
            const bool box = kind == "tcolorbox";
            const bool shaded = kind.starts_with("shaded") || kind.starts_with("snugshade") || beamer;
            float rule = shaded ? 0.0f : box ? 0.5f : kind == "leftbar" ? 3.0f : 0.4f;
            float room = beamer ? 4.0f : box || kind == "mdframed" ? 6.0f : 9.0f;
            const float line = static_cast<float>(context.registers.get(
                                   Registers::Type::Dimension, Registers::reserved + 1)) / 65536.0f;
            float outer = line;
            if (box) {
                if (const std::string written = setting(options, "boxrule"); !written.empty()) {
                    rule = std::max(measure(written, mouth, context), 0.0f);
                }
                if (const std::string written = setting(options, "left"); !written.empty()) {
                    room = std::max(measure(written, mouth, context), 0.0f);
                } else if (const std::string sep = setting(options, "boxsep"); !sep.empty()) {
                    room = std::max(measure(sep, mouth, context), 0.0f) + 4.0f;
                }
                if (const std::string written = setting(options, "width"); !written.empty()) {
                    outer = measure(written, mouth, context);
                }
            }
            const float width = std::max(outer - 2.0f * (room + rule), 20.0f);
            frames.push_back({std::string(kind), std::move(options), width, rule, room});
            // Inside, a line is the frame's inside.
            context.registers.set(Registers::Type::Dimension, Registers::reserved + 1,
                                  static_cast<std::int32_t>(width * 65536.0f), false);
            const syntax::Token mark{.symbol = framing, .category = syntax::Catcodes::Category::Escape,
                                     .text = mouth.lexicon.resolve(framing)};
            mouth.stream().inject(std::span{&mark, 1});
            if (beamer) mouth.ingest("\\par\\smallskip ");
        };
        const auto closing = [this](syntax::Mouth& mouth, const bool beamer) {
            if (beamer) mouth.ingest("\\par\\smallskip ");
            const syntax::Token mark{.symbol = framed, .category = syntax::Catcodes::Category::Escape,
                                     .text = mouth.lexicon.resolve(framed)};
            mouth.stream().inject(std::span{&mark, 1});
        };

        for (const std::string_view name : {"framed", "oframed", "shaded", "shaded*", "snugshade", "snugshade*",
                                            "leftbar", "mdframed", "tcolorbox", "block", "alertblock",
                                            "exampleblock"}) {
            const bool beamer = name.ends_with("block");
            context.blocks.watch(
                name,
                [this, name, beamer, enclose, spelled](syntax::Mouth& mouth) {
                    std::string options;
                    if (name == "mdframed" || name == "tcolorbox") {
                        options = spelled(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                    }
                    if (name == "tcolorbox") options = preset + "," + options;
                    if (beamer) {
                        // An overlay, `<2->`, is read and let go: every slide
                        // of a frame is the one page here.
                        if (mouth.lookahead().is('<')) {
                            while (!mouth.lookahead().empty() && !mouth.read().is('>')) {}
                        }
                        const std::string_view color = name == "alertblock"     ? "alerted"
                                                       : name == "exampleblock" ? "example"
                                                                                : "structure";
                        options = std::format("title={{{}}},colframe={},colback={}!10", syntax::Argument::text(mouth),
                                              color, color);
                    }
                    enclose(mouth, beamer ? "tcolorbox" : name, std::move(options), beamer);
                },
                [closing, beamer](syntax::Mouth& mouth) { closing(mouth, beamer); });
        }

        // tcolorbox's own: options every box reads before its own,
        // `\tcbset{colback=white}`, and a box of a name of the document's,
        // its options written once with its arguments in them --
        //   \newtcolorbox{note}[1]{colframe=blue, title=#1}
        //   \begin{note}{Careful} ... \end{note}
        // the first argument optional when a default is given.
        parser.mouth.bind("\\tcbset", [this, spelled](syntax::Mouth& mouth) {
            preset += "," + spelled(mouth.argument({}, 0));
        });
        for (const std::string_view command : {"\\newtcolorbox", "\\renewtcolorbox", "\\DeclareTColorBox"}) {
            parser.mouth.bind(command, [this, &context, enclose, closing, spelled](syntax::Mouth& mouth) {
                static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                const std::string name = syntax::Argument::text(mouth);
                int count = 0;
                const std::string written = spelled(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                std::from_chars(written.data(), written.data() + written.size(), count);
                const bool defaulted = mouth.lookahead().is('[');
                const std::string fallback = spelled(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                const std::string options = spelled(mouth.argument({}, 0));
                if (name.empty() || boxed.contains(name)) return;
                boxed.insert(name);
                context.blocks.watch(
                    name,
                    [this, enclose, spelled, count, defaulted, fallback, options](syntax::Mouth& mouth) {
                        std::string filled = options;
                        for (int index = 1; index <= count; ++index) {
                            std::string value;
                            if (index == 1 && defaulted) {
                                const std::vector<syntax::Token> given =
                                    mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0);
                                value = given.empty() ? fallback : spelled(given);
                            } else {
                                value = spelled(mouth.argument({}, 0));
                            }
                            const std::string mark = "#" + std::to_string(index);
                            for (std::size_t at = filled.find(mark); at != std::string::npos;
                                 at = filled.find(mark, at + value.size())) {
                                filled.replace(at, mark.size(), value);
                            }
                        }
                        enclose(mouth, "tcolorbox", preset + "," + filled, false);
                    },
                    [closing](syntax::Mouth& mouth) { closing(mouth, false); });
            });
        }

        parser.bind(framing, [this, &context, column, setting](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            if (frames.empty()) return nullptr;
            const auto [kind, settings, span, stroke, room] = frames.back();
            frames.pop_back();

            syntax::Symbol matched = syntax::none;
            const std::array<syntax::Symbol, 1> stops{framed};
            const memory::Slice<syntax::Node*> children = parser.parse(0, stops, matched);

            const auto option = [&settings, setting](const std::string_view key) { return setting(settings, key); };
            const auto colored = [&context](const std::string& written, const layout::Node::Color fallback) {
                if (written.empty()) return fallback;
                const graphics::Color color = Colors::resolve(written, context.variables);
                return layout::Node::Color{color.r, color.g, color.b, color.alpha};
            };

            const bool box = kind == "tcolorbox";
            const bool bar = kind == "leftbar";
            const layout::Node::Color edge = colored(option(box ? "colframe" : "linecolor"),
                                                     box ? layout::Node::Color{0.25f, 0.25f, 0.25f}
                                                         : layout::Node::Color{});
            const layout::Node::Color back = colored(option(box ? "colback" : "backgroundcolor"),
                                                     box ? layout::Node::Color{0.95f, 0.95f, 0.95f}
                                                         : layout::Node::Color{1.0f, 1.0f, 1.0f});
            layout::Node* body = column(arena, children, span);

            // The column as a row as tall as it is, which the frame goes round.
            const auto upright = [&arena](layout::Node* stack) {
                layout::Node::Box shape = stack->box();
                shape.shift = -(shape.height + shape.depth);
                stack->box(shape);
                const memory::Slice<layout::Node*> only = arena.allocate<layout::Node*>(1);
                only[0] = stack;
                return layout::Line::horizontal(arena, only, 0.0f);
            };
            const auto row = [&arena](std::vector<layout::Node*> nodes) {
                const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(nodes.size());
                std::ranges::copy(nodes, list.begin());
                return layout::Line::horizontal(arena, list, 0.0f);
            };
            const auto kern = [&arena](const float width) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
                node->kern({.width = width});
                return node;
            };
            const auto paint = [&arena](const layout::Node::Rule& value) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Rule);
                node->rule(value);
                return node;
            };

            layout::Node* inner = upright(body);
            const layout::Node::Box& shape = inner->box();
            const float width = shape.width + 2.0f * (room + stroke);
            const float height = shape.height + room + stroke;
            const float depth = shape.depth + room + stroke;
            const float across = width - 2.0f * stroke;

            std::vector<layout::Node*> pieces;
            if (bar) {
                pieces = {paint({.width = stroke, .height = height, .depth = depth, .color = edge}), kern(room), inner};
            } else {
                pieces = {paint({.width = width, .height = height, .depth = depth, .color = back}), kern(-width)};
                if (stroke > 0.0f) {
                    for (layout::Node* piece : {paint({.width = stroke, .height = height, .depth = depth, .color = edge}),
                                                paint({.width = across, .height = height, .depth = stroke - height, .color = edge}),
                                                kern(-across),
                                                paint({.width = across, .height = stroke - depth, .depth = depth, .color = edge}),
                                                kern(-across)}) {
                        pieces.push_back(piece);
                    }
                } else {
                    pieces.push_back(kern(stroke));
                }
                for (layout::Node* piece : {kern(room), inner, kern(room)}) pieces.push_back(piece);
                if (stroke > 0.0f) pieces.push_back(paint({.width = stroke, .height = height, .depth = depth, .color = edge}));
            }
            layout::Node* whole = row(pieces);

            // tcolorbox's title: a bar of the frame's color, the title in
            // white bold on it, then the body under it.
            std::vector<syntax::Node*> parts;
            parts.push_back(arena.compose<syntax::Node>(syntax::Node::Type::Paragraph, std::string_view{}, origin));
            // The title read as any text is -- a formula, a macro -- in
            // `fonttitle`'s face and `coltitle`'s color, white unless it says,
            // on a bar of `colbacktitle`'s, the frame's unless it says.
            if (const std::string title = option("title"); box && !title.empty()) {
                syntax::Mouth& mouth = parser.mouth;
                const typography::Font* restore = context.selection.text();
                const layout::Node::Color* painted = context.selection.color();
                context.selection.color(arena.compose<layout::Node::Color>(
                    colored(option("coltitle"), layout::Node::Color{1.0f, 1.0f, 1.0f})));
                mouth.ingest(arena.copy("{" + option("fonttitle") + " " + title + "}"));
                mouth.read();
                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node*> heading = parser.parse('}');
                mouth.pop(syntax::semantics::Scope::Type::Group);
                stamp(heading, context);
                context.selection.text(restore);
                context.selection.color(painted);

                std::vector<layout::Node*> written;
                for (const syntax::Node* child : heading) gather(written, child, context);
                layout::Node* words = row(written);
                const float tall = words->box().height + room;
                const float low = words->box().depth + room;
                const layout::Node::Color banner = colored(option("colbacktitle"), edge);
                parts.push_back(directive(arena, row({paint({.width = width, .height = tall, .depth = low, .color = banner}),
                                                      kern(-width + room + stroke), words}),
                                          origin, true));
            }
            parts.push_back(directive(arena, whole, origin, true));

            const memory::Slice<syntax::Node*> nodes = arena.allocate<syntax::Node*>(parts.size());
            std::ranges::copy(parts, nodes.begin());
            return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, nodes);
        });

        // minipage, and subcaption's subfigure and subtable, which are one
        // with a caption of their own kind: the block reads its position and
        // width and hands over to a mark that reads the paragraphs as far as
        // the one its end leaves. beamer's column is one too, hung as its
        // columns block says, and the room left beside it spread out.
        for (const std::string_view name : {"minipage", "subfigure", "subtable", "column"}) {
            context.blocks.watch(
                name,
                [this, &context, name](syntax::Mouth& mouth) {
                    std::string position;
                    for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                        position += token.text;
                    }
                    for (int skipped = 0; skipped < 2 && mouth.lookahead().is('['); ++skipped) {
                        static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                    }
                    const float width = measure(syntax::Argument::text(mouth), mouth, context);
                    const char fallback = name == "column" && !spreads.empty() ? spreads.back().position : 'c';
                    openings.push_back({position.empty() ? fallback : position[0], width});
                    // Inside, \linewidth is the box's own, and paragraphs open
                    // flush, as LaTeX's minipage sets them.
                    context.registers.set(Registers::Type::Dimension, Registers::reserved + 1,
                                          static_cast<std::int32_t>(width * 65536.0f), false);
                    const syntax::Token mark{.symbol = start, .category = syntax::Catcodes::Category::Escape,
                                             .text = mouth.lexicon.resolve(start)};
                    mouth.stream().inject(std::span{&mark, 1});
                },
                [this, name](syntax::Mouth& mouth) {
                    if (name == "column") mouth.ingest("\\hfill\\ignorespaces ");
                    const syntax::Token mark{.symbol = finish, .category = syntax::Catcodes::Category::Escape,
                                             .text = mouth.lexicon.resolve(finish)};
                    mouth.stream().inject(std::span{&mark, 1});
                });
        }

        // beamer's columns: one line of them, the room they leave spread
        // before, between and after them, each hung by its middle unless the
        // block's `[t]` or `[b]` says otherwise. A column is its block, or
        // runs from a `\column{width}` to the next or to the block's end, in
        // a scope of its own opened and closed here.
        context.blocks.watch(
            "columns",
            [this](syntax::Mouth& mouth) {
                std::string options;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    options += token.text;
                }
                const bool top = options.find('t') != std::string::npos || options.find('T') != std::string::npos;
                spreads.push_back({top ? 't' : options.find('b') != std::string::npos ? 'b' : 'c', false});
                mouth.ingest("\\par\\noindent\\hfill\\ignorespaces ");
            },
            [this](syntax::Mouth& mouth) {
                if (spreads.empty()) return;
                const bool open = spreads.back().open;
                spreads.pop_back();
                mouth.ingest("\\hbox{}\\par ");
                if (!open) return;
                mouth.pop(syntax::semantics::Scope::Type::Box);
                const std::array<syntax::Token, 2> marks{
                    syntax::Token{.symbol = finish, .category = syntax::Catcodes::Category::Escape,
                                  .text = mouth.lexicon.resolve(finish)},
                    syntax::Token{.symbol = mouth.lexicon.intern("\\hfill"),
                                  .category = syntax::Catcodes::Category::Escape,
                                  .text = mouth.lexicon.resolve(mouth.lexicon.intern("\\hfill"))}};
                mouth.stream().inject(std::span{marks});
            },
            /*transparent=*/true);
        parser.mouth.bind("\\column", [this, &context, &lexicon](syntax::Mouth& mouth) {
            std::string position;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                position += token.text;
            }
            const float width = measure(syntax::Argument::text(mouth), mouth, context);
            if (spreads.empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Environment, mouth.lookahead().location,
                                         "\\column outside a columns block");
                return;
            }
            Spread& spread = spreads.back();
            std::vector<syntax::Token> marks;
            if (std::exchange(spread.open, true)) {
                mouth.pop(syntax::semantics::Scope::Type::Box);
                marks.push_back({.symbol = finish, .category = syntax::Catcodes::Category::Escape,
                                 .text = lexicon.resolve(finish)});
                marks.push_back({.symbol = lexicon.intern("\\hfill"), .category = syntax::Catcodes::Category::Escape,
                                 .text = lexicon.resolve(lexicon.intern("\\hfill"))});
            }
            mouth.push(syntax::semantics::Scope::Type::Box);
            openings.push_back({position.empty() ? spread.position : position[0], width});
            context.registers.set(Registers::Type::Dimension, Registers::reserved + 1,
                                  static_cast<std::int32_t>(width * 65536.0f), false);
            marks.push_back({.symbol = start, .category = syntax::Catcodes::Category::Escape,
                             .text = lexicon.resolve(start)});
            mouth.stream().inject(std::span{marks});
        });

        parser.bind(start, [this, column, hung](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            if (openings.empty()) return nullptr;
            const auto [position, width] = openings.back();
            openings.pop_back();

            syntax::Symbol matched = syntax::none;
            const std::array<syntax::Symbol, 1> stops{finish};
            const memory::Slice<syntax::Node*> children = parser.parse(0, stops, matched);
            return directive(arena, hung(arena, column(arena, children, width), position), origin);
        });

        // TeX's box registers: `\setbox3=\hbox{...}` keeps a box, `\copy3`
        // puts it down and keeps it, `\box3` puts it down and empties the
        // register, and \wd, \ht and \dp read -- and may set -- its three
        // measurements, which it is put down with. LaTeX's saved boxes are
        // the same registers under a name \newsavebox gives out.
        using Registers = syntax::semantics::Registers;
        const auto number = [&context](syntax::Mouth& mouth) -> std::optional<std::size_t> {
            const auto index = syntax::Number::integer(mouth, context.registers);
            if (!index || *index < 0 || *index >= static_cast<std::int32_t>(Registers::slots)) return std::nullopt;
            return static_cast<std::size_t>(*index);
        };
        // TeX's tests of a box register: whether it is empty, or holds a box
        // running across or down -- each answered as \iftrue or \iffalse, so
        // a test skipped over still counts as one.
        for (const auto& [name, kind] : {std::pair{"\\ifvoid", 0}, std::pair{"\\ifhbox", 1}, std::pair{"\\ifvbox", 2}}) {
            parser.mouth.bind(name, [this, number, kind](syntax::Mouth& mouth) {
                const auto slot = number(mouth);
                const layout::Node* box = slot ? stored[*slot] : nullptr;
                const bool across = box && box->type == layout::Node::Type::Box &&
                                    box->box().alignment == layout::Node::Alignment::Horizontal;
                const bool holds = kind == 0 ? !box : kind == 1 ? box && across : box && !across;
                mouth.ingest(holds ? "\\iftrue " : "\\iffalse ");
            });
        }
        const auto keep = [this, &context](const std::size_t slot, layout::Node* box) {
            stored[slot] = box;
            const layout::Node::Box* shape = box ? &box->box() : nullptr;
            for (const auto& [type, value] : {std::pair{Registers::Type::Width, shape ? shape->width : 0.0f},
                                              std::pair{Registers::Type::Height, shape ? shape->height - shape->shift : 0.0f},
                                              std::pair{Registers::Type::Depth, shape ? shape->depth + shape->shift : 0.0f}}) {
                context.registers.set(type, slot, static_cast<std::int32_t>(value * 65536.0f), false);
            }
        };
        // A register's box as its measurements now say: a box whose \wd was
        // set to nothing overlaps what follows it, the way TeX's does.
        const auto take = [this, &context](memory::Arena& arena, const std::size_t slot) -> layout::Node* {
            layout::Node* box = stored[slot];
            if (!box) return nullptr;
            const auto held = [&context, slot](const Registers::Type type) {
                return static_cast<float>(context.registers.get(type, slot)) / 65536.0f;
            };
            const layout::Node::Box& shape = box->box();
            if (std::abs(held(Registers::Type::Width) - shape.width) < 0.001f &&
                std::abs(held(Registers::Type::Height) - (shape.height - shape.shift)) < 0.001f &&
                std::abs(held(Registers::Type::Depth) - (shape.depth + shape.shift)) < 0.001f) {
                return box;
            }
            auto* resized = arena.compose<layout::Node>(layout::Node::Type::Box);
            layout::Node::Box measured = shape;
            measured.width = held(Registers::Type::Width);
            measured.height = held(Registers::Type::Height) + shape.shift;
            measured.depth = held(Registers::Type::Depth) - shape.shift;
            resized->box(measured);
            return resized;
        };

        parser.bind("\\setbox", [this, &context, number, keep, contents, row](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const auto slot = number(mouth);
            while (mouth.lookahead().category == syntax::Catcodes::Category::Space || mouth.lookahead().is('=')) {
                mouth.read();
            }
            // The box: made here by the name that makes one, or another
            // register's, taken or copied.
            const syntax::Token kind = mouth.expand();
            layout::Node* box = nullptr;
            if (kind.symbol == made[0] || kind.symbol == made[3]) {
                float target = 0.0f;
                if (kind.symbol == made[0]) {
                    while (mouth.lookahead().category == syntax::Catcodes::Category::Space) mouth.read();
                    if (mouth.lookahead(0).is('t') && mouth.lookahead(1).is('o')) {
                        mouth.read();
                        mouth.read();
                        target = measure(syntax::Argument::text(mouth), mouth, context);
                    }
                }
                box = row(arena, contents(parser, "\\setbox"), target);
            } else if (kind.symbol == made[1] || kind.symbol == made[2]) {
                std::vector<layout::Node*> nodes;
                for (const syntax::Node* child : contents(parser, "\\setbox")) gather(nodes, child, context);
                const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(nodes.size());
                std::ranges::copy(nodes, list.begin());
                box = layout::Line::vertical(arena, list, 0.0f);
            } else {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, kind.location,
                                         "\\setbox needs \\hbox, \\vbox or \\mbox after its number");
                if (!kind.empty()) mouth.stream().inject(std::span{&kind, 1});
            }
            if (slot) keep(*slot, box);
            return nullptr;
        });
        for (const auto& [name, emptied] : {std::pair{"\\box", true}, std::pair{"\\copy", false},
                                            std::pair{"\\unhbox", true}, std::pair{"\\unhcopy", false},
                                            std::pair{"\\unvbox", true}, std::pair{"\\unvcopy", false}}) {
            parser.bind(name, [number, keep, take, emptied](syntax::Parser& parser) -> syntax::Node* {
                const memory::Location origin = parser.mouth.lookahead().location;
                const auto slot = number(parser.mouth);
                if (!slot) return nullptr;
                layout::Node* box = take(parser.arena, *slot);
                if (emptied) keep(*slot, nullptr);
                return box ? directive(parser.arena, box, origin) : nullptr;
            });
        }

        // LaTeX's names for the same: a register given out and named, a row
        // or a sized row kept in it, and put down again wherever it is used.
        const auto slot = [number](syntax::Mouth& mouth) {
            const std::vector<syntax::Token> name = mouth.argument({}, 0);
            if (!name.empty()) mouth.stream().inject(std::span{name});
            return number(mouth);
        };
        parser.bind("\\newsavebox", [](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            std::vector<syntax::Token> name = mouth.argument({}, 0);
            const syntax::Symbol symbol = mouth.lexicon.intern("\\newbox");
            name.insert(name.begin(), syntax::Token{.symbol = symbol, .category = syntax::Catcodes::Category::Escape,
                                                   .text = mouth.lexicon.resolve(symbol)});
            mouth.stream().inject(std::span{name});
            return nullptr;
        });
        parser.bind("\\sbox", [slot, keep, contents, row](syntax::Parser& parser) -> syntax::Node* {
            const auto index = slot(parser.mouth);
            layout::Node* box = row(parser.arena, contents(parser, "\\sbox"));
            if (index) keep(*index, box);
            return nullptr;
        });
        parser.bind("\\savebox", [slot, keep, contents, row, placed, options](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const auto index = slot(parser.mouth);
            const auto [width, position] = options(parser.mouth);
            const memory::Slice<syntax::Node*> children = contents(parser, "\\savebox");
            layout::Node* box = width ? placed(arena, children, *width, position) : row(arena, children);
            if (index) keep(*index, box);
            return nullptr;
        });
        parser.bind("\\usebox", [slot, take](syntax::Parser& parser) -> syntax::Node* {
            const memory::Location origin = parser.mouth.lookahead().location;
            const auto index = slot(parser.mouth);
            layout::Node* box = index ? take(parser.arena, *index) : nullptr;
            return box ? directive(parser.arena, box, origin) : nullptr;
        });

        // graphicx's transforms: a row drawn turned, scaled, stretched to a
        // size or mirrored, in a box as large as what that makes. The map
        // turns about the row's reference point, and the box's left edge is
        // the leftmost ink of the result, on the same baseline.
        const auto transformed = [](memory::Arena& arena, layout::Node* inner, const float a, const float b,
                                    const float c, const float d) {
            const layout::Node::Box& shape = inner->box();
            float left = 0.0f;
            float right = 0.0f;
            float top = 0.0f;
            float bottom = 0.0f;
            bool first = true;
            for (const auto& [x, y] : {std::pair{0.0f, -shape.height}, std::pair{shape.width, -shape.height},
                                       std::pair{0.0f, shape.depth}, std::pair{shape.width, shape.depth}}) {
                const float across = a * x + c * y;
                const float down = b * x + d * y;
                left = first ? across : std::min(left, across);
                right = first ? across : std::max(right, across);
                top = first ? down : std::min(top, down);
                bottom = first ? down : std::max(bottom, down);
                first = false;
            }
            auto* map = arena.compose<layout::Node::Transform>();
            *map = {.a = a, .b = b, .c = c, .d = d, .x = -left};

            auto* box = arena.compose<layout::Node>(layout::Node::Type::Box);
            layout::Node::Box outer = shape;
            outer.width = right - left;
            outer.height = std::max(-top, 0.0f);
            outer.depth = std::max(bottom, 0.0f);
            outer.shift = 0.0f;
            outer.offset = 0.0f;
            outer.transform = map;
            box->box(outer);
            return box;
        };
        // The options graphicx's transforms take in brackets, which set where
        // a turn is about; read and let go, every turn here being about the
        // reference point.
        const auto bracketed = [](syntax::Mouth& mouth) {
            static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
        };

        parser.bind("\\rotatebox", [contents, row, transformed, bracketed](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            bracketed(mouth);
            float degrees = 0.0f;
            const std::string written = syntax::Argument::expanded(mouth);
            std::from_chars(written.data(), written.data() + written.size(), degrees);
            const float angle = degrees * 3.14159265358979f / 180.0f;
            layout::Node* inner = row(arena, contents(parser, "\\rotatebox"));
            return directive(arena, transformed(arena, inner, std::cos(angle), -std::sin(angle), std::sin(angle),
                                                std::cos(angle)), origin);
        });
        parser.bind("\\scalebox", [contents, row, transformed](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            float across = 1.0f;
            const std::string written = syntax::Argument::expanded(mouth);
            std::from_chars(written.data(), written.data() + written.size(), across);
            float down = across;
            std::string vertical;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                vertical += token.text;
            }
            if (!vertical.empty()) std::from_chars(vertical.data(), vertical.data() + vertical.size(), down);
            layout::Node* inner = row(arena, contents(parser, "\\scalebox"));
            return directive(arena, transformed(arena, inner, across, 0.0f, 0.0f, down), origin);
        });
        parser.bind("\\reflectbox", [contents, row, transformed](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            layout::Node* inner = row(arena, contents(parser, "\\reflectbox"));
            return directive(arena, transformed(arena, inner, -1.0f, 0.0f, 0.0f, 1.0f), origin);
        });
        // \resizebox{width}{height}: stretched to both, or to one with the
        // other kept in proportion when it is written `!`; the starred form
        // measures height and depth together.
        parser.bind("\\resizebox", [&context, contents, row, transformed](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            const bool total = mouth.lookahead().is('*');
            if (total) mouth.read();
            std::array<std::optional<float>, 2> wanted{};
            for (std::optional<float>& size : wanted) {
                const std::string written = syntax::Argument::text(mouth);
                if (written != "!") size = measure(written, mouth, context);
            }
            layout::Node* inner = row(arena, contents(parser, "\\resizebox"));
            const layout::Node::Box& shape = inner->box();
            const float tall = total ? shape.height + shape.depth : shape.height;
            float across = wanted[0] && shape.width > 0.0f ? *wanted[0] / shape.width : 1.0f;
            float down = wanted[1] && tall > 0.0f ? *wanted[1] / tall : across;
            if (!wanted[0]) across = down;
            return directive(arena, transformed(arena, inner, across, 0.0f, 0.0f, down), origin);
        });

        // \settowidth, \settoheight and \settodepth: a length set to what a
        // row of the given text measures.
        for (const auto& [name, extent] : {std::pair{"\\settowidth", 0}, std::pair{"\\settoheight", 1},
                                           std::pair{"\\settodepth", 2}}) {
            parser.bind(name, [contents, row, name, extent](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth;
                std::string length;
                for (const syntax::Token& token : mouth.argument({}, 0)) length += token.text;
                const layout::Node::Box& shape = row(parser.arena, contents(parser, name))->box();
                const float value = extent == 0 ? shape.width : extent == 1 ? shape.height : shape.depth;
                mouth.ingest(parser.arena.copy(std::format("\\setlength{{{}}}{{{}pt}}", length, value)));
                return nullptr;
            });
        }

        // calc's \widthof{text} and the rest: what a row of the text would
        // measure, as a length a \setlength or a \dimexpr reads on. Bound in
        // the expander, so a length written in terms of one reads it as it
        // reads any other; the text is set as it is written, in the face in
        // use.
        for (const auto& [name, extent] : {std::pair{"\\widthof", 0}, std::pair{"\\heightof", 1},
                                           std::pair{"\\depthof", 2}, std::pair{"\\totalheightof", 3}}) {
            parser.mouth.bind(name, [&context, extent](syntax::Mouth& mouth) {
                const std::string text = syntax::Argument::expanded(mouth);
                float value = 0.0f;
                if (const typography::Font* font = context.selection.text(); font && !text.empty()) {
                    const typography::Font* fonts[] = {font};
                    const layout::Node::Box& shape = layout::Line::horizontal(
                        context.arena, context.shaper.shape(memory::Slice{fonts, 1uz}, context.arena.copy(text), {}),
                        0.0f)->box();
                    value = extent == 0 ? shape.width : extent == 1 ? shape.height : extent == 2 ? shape.depth
                                                                                              : shape.height + shape.depth;
                }
                mouth.ingest(context.arena.copy(std::format("{}pt ", value)));
            });
        }

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound box primitives");
    }

}
