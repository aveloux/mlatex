#include "render/primitives/expression/entry.hpp"
#include "render/primitives/expression/operator.hpp"

#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/semantics/scope.hpp"
#include "syntax/semantics/union.hpp"
#include "syntax/tokens.hpp"

#include <span>

namespace render::primitives::expression {

    static syntax::Node* enter(
        const syntax::Parser& parser,
        const syntax::expression::Unicodes& unicodes,
        const bool display,
        const char delimiter,
        const syntax::Symbol stop
    ) {
        syntax::Mouth& mouth = parser.mouth();
        memory::Arena& arena = parser.arena();
        const memory::Location origin = mouth.lookahead().location;

        mouth.state().scope().push(syntax::semantics::Scope::Type::Equations);

        syntax::expression::Parser inner(
            mouth, unicodes, arena,
            display ? syntax::expression::Node::Style::Display
                    : syntax::expression::Node::Style::Inline
        );
        rules(inner, mouth.lexicon());

        syntax::expression::Node* tree = delimiter != 0 ? inner.parse(delimiter) : inner.parse(stop);
        syntax::Token leftover = inner.pending();

        mouth.state().scope().pop();

        if (display && delimiter == '$' && leftover.values == "$") {
            leftover = syntax::Token{};
        }

        if (!leftover.empty()) {
            mouth.inject(std::span{&leftover, 1});
        }

        auto* node = arena.compose<syntax::Node>(syntax::Node::Type::Expression, std::string_view{}, origin);
        node->expression = tree;
        return node;
    }

    void ingest(syntax::Parser& parser, const syntax::expression::Unicodes& unicodes) {
        parser.bind("$", [&unicodes](const syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            syntax::Token next = mouth.read();
            bool display = false;

            if (next.values == "$") {
                display = true;
            } else if (!next.empty()) {
                mouth.inject(std::span{&next, 1});
            }

            return enter(parser, unicodes, display, '$', syntax::kInvalidSymbol);
        });

        parser.bind("\\(", [&unicodes](const syntax::Parser& parser) -> syntax::Node* {
            const syntax::Symbol stop = parser.mouth().lexicon().intern("\\)");
            return enter(parser, unicodes, false, 0, stop);
        });

        parser.bind("\\[", [&unicodes](const syntax::Parser& parser) -> syntax::Node* {
            const syntax::Symbol stop = parser.mouth().lexicon().intern("\\]");
            return enter(parser, unicodes, true, 0, stop);
        });
    }

}