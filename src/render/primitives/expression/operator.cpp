#include "render/primitives/expression/operator.hpp"

namespace render::primitives::expression {

    void rules(syntax::expression::Parser& parser, syntax::Lexicon& lexicon) {
        using Type = syntax::expression::Node::Type;

        const auto bind = [&](const std::string_view name, const Type type, const int weight = 0,
                               const bool right = false, const bool structural = false) {
            parser.bind(lexicon.intern(name), type, weight, right, structural);
        };

        bind("=", Type::Binary, 1);
        bind("<", Type::Binary, 1);
        bind(">", Type::Binary, 1);
        bind("\\le", Type::Binary, 1);
        bind("\\ge", Type::Binary, 1);
        bind("\\neq", Type::Binary, 1);
        bind("\\equiv", Type::Binary, 1);

        bind("+", Type::Binary, 2);
        bind("-", Type::Binary, 2);
        bind("\\pm", Type::Binary, 2);
        bind("\\mp", Type::Binary, 2);

        bind("*", Type::Binary, 3);
        bind("/", Type::Binary, 3);
        bind("\\times", Type::Binary, 3);
        bind("\\cdot", Type::Binary, 3);
        bind("\\div", Type::Binary, 3);

        bind("!", Type::Unary, 10);

        bind("\\frac", Type::Fraction, 0, false, true);
        bind("\\sqrt", Type::Radical, 0, false, true);
        bind("\\hat", Type::Accent, 0, false, true);
        bind("\\vec", Type::Accent, 0, false, true);
        bind("\\bar", Type::Accent, 0, false, true);
        bind("\\dot", Type::Accent, 0, false, true);
        bind("\\ddot", Type::Accent, 0, false, true);
        bind("\\tilde", Type::Accent, 0, false, true);
        bind("\\breve", Type::Accent, 0, false, true);
        bind("\\check", Type::Accent, 0, false, true);
        bind("\\acute", Type::Accent, 0, false, true);
        bind("\\grave", Type::Accent, 0, false, true);
    }

}