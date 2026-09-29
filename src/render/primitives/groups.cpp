/// @file
/// @brief The groups: `{` and everything up to its `}`, TeX's
///        `\\begingroup` ... `\\endgroup`, and `\\bgroup` ... `\\egroup`.
///
/// The contents come back as a group node rather than a box, because a box
/// could not be broken across a line and a sentence in braces must be. What
/// the document gets is the children themselves, with the face they were read
/// under stamped onto them.
#include "render/primitives/groups.hpp"
#include "logger.hpp"
#include "syntax/semantics/scope.hpp"
#include "syntax/semantics/union.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace render::primitives {

    Groups::Groups(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("{");
        close = lexicon.intern("\\egroup");
        end = lexicon.intern("\\endgroup");
    }

    void Groups::operator()(syntax::Parser& parser, Context& context) const {
        // One group, however it opened: read in a scope of its own up to its
        // close -- a brace, or the name that closes its kind -- and what it
        // selected put back on the way out. A brace and \bgroup close each
        // other, as they do in TeX; \begingroup closes only at \endgroup.
        const auto grouped = [&context](syntax::Parser& parser, const char closing, const syntax::Symbol stop) {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            // What the group is entered with, so that whatever it selects
            // inside is put back on the way out.
            const Selection kept = context.selection;

            mouth.push(syntax::semantics::Scope::Type::Group);
            syntax::Symbol matched = syntax::none;
            const std::array<syntax::Symbol, 1> stops{stop};
            const memory::Slice<syntax::Node*> children = parser.parse(closing, stops, matched);
            mouth.pop(syntax::semantics::Scope::Type::Group);

            // Stamped while the selection is still the group's own.
            stamp(children, context);
            context.selection = kept;

            // A paragraph's shape set inside -- `{\centering ...\par}`,
            // `{\raggedleft ...\par}` -- is the group's own too: kept on the
            // way in and put back on the way out, so it shapes the
            // paragraphs ended inside the group and none after, as TeX's
            // local \leftskip and \rightskip do.
            using Command = layout::Node::Directive::Command;
            const bool shaped = std::ranges::any_of(children, [](const syntax::Node* child) {
                if (!child || child->type != syntax::Node::Type::Directive || !child->directive) return false;
                const auto* node = static_cast<const layout::Node*>(child->directive);
                return node->type() == layout::Node::Type::Directive &&
                       (node->directive().command == Command::Align || node->directive().command == Command::Margin);
            });
            if (!shaped) {
                return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, children);
            }
            const memory::Slice<syntax::Node*> wrapped = arena.allocate<syntax::Node*>(children.count + 2);
            for (const auto [at, command] : {std::pair{0uz, Command::Save}, std::pair{children.count + 1, Command::Restore}}) {
                auto* order = arena.compose<layout::Node>(layout::Node::Type::Directive);
                order->directive({.command = command});
                wrapped[at] = directive(arena, order, origin);
            }
            std::ranges::copy(children, wrapped.begin() + 1);
            return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, wrapped);
        };

        parser.bind("{", [this, grouped](syntax::Parser& parser) -> syntax::Node* { return grouped(parser, '}', close); });
        parser.bind("\\bgroup", [this, grouped](syntax::Parser& parser) -> syntax::Node* {
            return grouped(parser, '}', close);
        });
        parser.bind("\\begingroup", [this, grouped](syntax::Parser& parser) -> syntax::Node* {
            return grouped(parser, 0, end);
        });

        // A block is a group too: a face or a color chosen inside one --
        // `\begin{quote}\itshape` -- ends where the block does.
        context.blocks.watch([this, &context](syntax::Mouth&) { kept.push_back(context.selection); },
                             [this, &context](syntax::Mouth&) {
                                 if (kept.empty()) return;
                                 context.selection = kept.back();
                                 kept.pop_back();
                             });

        // Every run of text marked as it is read with the face and color in
        // use, so a switch part way through a paragraph -- `\bfseries`,
        // `\color{red}` -- changes only what follows it.
        parser.stamp = [&context](syntax::Node& node) {
            node.face = context.selection.text();
            node.tint = context.selection.color();
        };
        parser.changes = &context.selection.changes();

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound the groups");
    }

}
