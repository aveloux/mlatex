/// @file
/// @brief Cross-reference primitives: `\\label`, `\\ref`, `\\eqref`, `\\cref`,
///        `\\Cref`, `\\pageref` and `\\nameref`.
///
/// `\\label` is bound in the expander, not the parser, so that it can stand
/// anywhere -- inside a formula, which the expression parser reads, as easily
/// as in running text -- and leave nothing behind there. The references are
/// parser primitives, because they produce text.
#include "render/primitives/references.hpp"
#include "render/primitives/colors.hpp"
#include "syntax/argument.hpp"
#include "syntax/semantics/scope.hpp"
#include "logger.hpp"

#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace render::primitives {

    References::References(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\label");
        lexicon.intern("\\ref");
        lexicon.intern("\\eqref");
        lexicon.intern("\\cref");
        lexicon.intern("\\Cref");
        // No document can write this name: a colon ends a control word.
        place = lexicon.intern("\\label:place");
    }

    std::string References::write(const Mark& mark, const Form form, const Context& context) {
        if (form == Form::Title) return mark.title;
        if (form == Form::Page) {
            return context.folios && mark.anchor < context.folios->size() && !(*context.folios)[mark.anchor].empty()
                       ? (*context.folios)[mark.anchor]
                       : std::string("??");
        }

        const std::string number = mark.kind == "equation" && form != Form::Number ? "(" + mark.text + ")" : mark.text;
        if (form == Form::Number || mark.kind.empty()) return number;

        // The name the document or cleveref gave the kind, or the kind itself.
        const std::string* named = context.variables.find((form == Form::Capital ? "Cref." : "cref.") + mark.kind);
        std::string name = named ? *named : mark.kind;
        if (!named && form == Form::Capital && !name.empty() && name[0] >= 'a' && name[0] <= 'z') {
            name[0] = static_cast<char>(name[0] - 'a' + 'A');
        }
        return name + " " + number;
    }

    References::Hyperlink References::hyperlink(Context& context, const std::string_view target, const std::size_t anchor,
                                                const std::string_view kind) {
        if (!context.variables.find("links")) return {};
        const auto option = [&context](const std::string_view key) {
            return context.variables.find("hyperref." + std::string(key));
        };
        const auto on = [&option](const std::string_view key) {
            const std::string* value = option(key);
            return value && *value != "false";
        };
        const std::string* border = option("pdfborder");
        const bool hidden = on("hidelinks") || (border && border->find_first_not_of("0 {}") == std::string::npos);
        const bool colored = on("colorlinks") && !hidden;

        using Color = layout::Node::Color;
        const Color frame = kind == "url" ? Color{0.0f, 1.0f, 1.0f} : kind == "cite" ? Color{0.0f, 1.0f, 0.0f}
                                                                                     : Color{1.0f, 0.0f, 0.0f};
        Hyperlink made;
        made.open = context.arena.compose<layout::Node>(layout::Node::Type::Directive);
        made.open->directive({.command = layout::Node::Directive::Command::Link, .target = context.arena.copy(target),
                              .border = hidden || colored ? Color{0.0f, 0.0f, 0.0f, 0.0f} : frame, .index = anchor});
        made.close = context.arena.compose<layout::Node>(layout::Node::Type::Directive);
        made.close->directive({.command = layout::Node::Directive::Command::Unlink});
        if (colored) {
            const std::string* named = option(std::string(kind) + "color");
            if (!named) named = option("allcolors");
            const std::string_view fallback = kind == "url" ? "magenta" : kind == "cite" ? "green" : "red";
            const graphics::Color color = Colors::resolve(named ? std::string_view(*named) : fallback, context.variables);
            made.tint = context.arena.compose<Color>(Color{color.r, color.g, color.b, color.alpha});
        }
        return made;
    }

    syntax::Node* References::wrapped(memory::Arena& arena, syntax::Node* content, const Hyperlink& link,
                                      const memory::Location origin) {
        if (!link.open || !content) return content;
        if (link.tint) {
            [&link](this const auto& self, syntax::Node* node) -> void {
                if (node->type == syntax::Node::Type::Text || node->type == syntax::Node::Type::Expression) {
                    node->tint = link.tint;
                }
                for (syntax::Node* child : node->nodes) {
                    if (child) self(child);
                }
            }(content);
        }
        const memory::Slice<syntax::Node*> parts = arena.allocate<syntax::Node*>(3);
        parts[0] = directive(arena, link.open, origin);
        parts[1] = content;
        parts[2] = directive(arena, link.close, origin);
        return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, parts);
    }

    void References::operator()(syntax::Parser& parser, Context& context) const {
        parser.mouth().bind("\\label", [this, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = syntax::Argument::text(mouth);

            Mark& mark = marks[name];
            if (mark.defined) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Warning, origin,
                                         "Label `" + name + "' multiply defined");
                return;
            }

            mark.text = context.anchor;
            mark.kind = context.kind;
            mark.title = context.title;
            mark.defined = true;

            // Its page is where it stands: in the text, an anchor of its own,
            // set down by a mark the parser reads next; in a formula, which
            // the parser does not read, the anchor before it.
            if (mouth.formula()) {
                if (context.anchors > 0) mark.anchor = context.anchors - 1;
            } else {
                mark.anchor = context.anchors++;
                waiting.push_back(mark.anchor);
                const syntax::Token token{.symbol = place, .category = syntax::CatCodes::Category::Escape,
                                          .text = mouth.lexicon().resolve(place)};
                mouth.stream().inject(std::span{&token, 1});
            }

            // Every reference that came first gets its text now, from the
            // arena, so it outlives this table's own copy, and every link its
            // place.
            for (const auto& [reference, form] : mark.references) {
                reference->value = context.arena.copy(write(mark, form, context));
            }
            mark.references.clear();
            for (layout::Node* link : mark.links) {
                layout::Node::Directive order = link->directive();
                order.index = mark.anchor;
                link->directive(order);
            }
            mark.links.clear();
        });

        // A text given further on than it is used, by key: \@forward leaves
        // it waiting, as a reference waits for its label, and \@fulfil
        // gives it, expanded.
        parser.mouth().bind("\\@fulfil", [this, &context](syntax::Mouth& mouth) {
            Mark& mark = forwards[syntax::Argument::text(mouth)];
            mark.title = syntax::Argument::expanded(mouth);
            mark.defined = true;
            for (const auto& [reference, form] : mark.references) reference->value = context.arena.copy(mark.title);
            mark.references.clear();
        });
        parser.bind("\\@forward", [this](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            Mark& mark = forwards[syntax::Argument::text(parser.mouth())];
            auto* text = arena.compose<syntax::Node>(syntax::Node::Type::Text,
                                                     mark.defined ? arena.copy(mark.title) : std::string_view{"??"},
                                                     parser.mouth().lookahead().location);
            if (!mark.defined) mark.references.emplace_back(text, Form::Title);
            return text;
        });

        parser.bind(place, [this](syntax::Parser& parser) -> syntax::Node* {
            if (waiting.empty()) return nullptr;
            memory::Arena& arena = parser.arena();
            auto* anchor = arena.compose<layout::Node>(layout::Node::Type::Directive);
            anchor->directive({.command = layout::Node::Directive::Command::Anchor, .index = waiting.back()});
            waiting.pop_back();
            return directive(arena, anchor, parser.mouth().lookahead().location);
        });

        // `\\ref` is the number alone; `\\eqref` puts it in the parentheses an
        // equation's number is printed in; `\\cref` and `\\Cref` say what it
        // numbers first, as cleveref does; `\\pageref` is its page and
        // `\\nameref` its title.
        struct Spelling {
            std::string_view name;   ///< The command.
            Form form;               ///< How it writes the label out.
            bool enclosed;           ///< In parentheses, as \\eqref's is.
        };
        static constexpr std::array<Spelling, 6> forms{{
            {"\\ref", Form::Number, false},
            {"\\eqref", Form::Number, true},
            {"\\cref", Form::Named, false},
            {"\\Cref", Form::Capital, false},
            {"\\pageref", Form::Page, false},
            {"\\nameref", Form::Title, false},
        }};

        for (const auto& [name, form, enclosed] : forms) {
            parser.bind(name, [this, &context, form, enclosed](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth();
                memory::Arena& arena = parser.arena();
                const memory::Location origin = mouth.lookahead().location;

                // hyperref's starred forms are the same reference, unlinked.
                const bool unlinked = mouth.lookahead().is('*');
                if (unlinked) mouth.read();
                const std::string label = syntax::Argument::text(mouth);
                Mark& mark = marks[label];
                if (form == Form::Page) context.paged = true;
                if (!mark.defined) awaited.emplace_back(label, origin);

                auto* number = arena.compose<syntax::Node>(
                    syntax::Node::Type::Text,
                    mark.defined ? arena.copy(write(mark, form, context)) : std::string_view{"??"},
                    origin);
                if (!mark.defined) mark.references.emplace_back(number, form);

                // With hyperref, a link to where the label stands.
                const Hyperlink link = unlinked ? Hyperlink{} : hyperlink(context, {}, mark.anchor, "link");
                if (link.open && !mark.defined) mark.links.push_back(link.open);
                if (!enclosed) return wrapped(arena, number, link, origin);

                const memory::Slice<syntax::Node*> parts = arena.allocate<syntax::Node*>(3);
                parts[0] = arena.compose<syntax::Node>(syntax::Node::Type::Text, std::string_view{"("}, origin);
                parts[1] = number;
                parts[2] = arena.compose<syntax::Node>(syntax::Node::Type::Text, std::string_view{")"}, origin);
                return wrapped(arena, arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, parts),
                               link, origin);
            });
        }

        // An address and the text that goes to it, hyperref's \href and
        // \url: `\@link{https://...}{text}`; and a label's place, its
        // \hyperref[label]{text} and \hyperlink: `\@linkto{label}{text}`.
        // Without hyperref, the text alone.
        for (const bool address : {true, false}) {
            parser.bind(address ? "\\@link" : "\\@linkto", [this, &context, address](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth();
                memory::Arena& arena = parser.arena();
                const memory::Location origin = mouth.lookahead().location;
                // The address as written, `\#` and `\_` their characters.
                std::string target;
                for (const syntax::Token& token : mouth.argument({}, 0)) {
                    target += token.category == syntax::CatCodes::Category::Escape && token.text.size() == 2
                                  ? token.text.substr(1) : token.text;
                }
                syntax::Token open = mouth.read();
                while (open.category == syntax::CatCodes::Category::Space) open = mouth.read();
                if (!open.is(syntax::CatCodes::Category::Group, '{')) {
                    if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                    return nullptr;
                }
                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node*> children = parser.parse('}');
                mouth.pop(syntax::semantics::Scope::Type::Group);
                stamp(children, context);
                auto* content = arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, children);

                Mark* mark = address ? nullptr : &marks[target];
                const Hyperlink link = hyperlink(context, address ? target : std::string{}, mark ? mark->anchor : 0,
                                                 address ? "url" : "link");
                if (link.open && mark && !mark->defined) mark->links.push_back(link.open);
                return wrapped(arena, content, link, origin);
            });
        }

        // A reference whose label never came, named as LaTeX names it, once.
        context.blocks.watch("document", {}, [this](syntax::Mouth&) {
            std::vector<std::string> said;
            for (const auto& [label, origin] : awaited) {
                if (marks[label].defined || std::ranges::contains(said, label)) continue;
                said.push_back(label);
                tracebacks_.emplace_back(syntax::Traceback::Type::Warning, origin,
                                         "Reference `" + label + "' undefined");
            }
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound reference primitives");
    }

}
