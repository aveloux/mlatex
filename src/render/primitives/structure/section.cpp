/*#include "render/primitives/structure/section.hpp"
#include "render/primitives/directive.hpp"

#include "render/layout/line.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"

#include <format>
#include <string>
#include <vector>

namespace render::primitives::structure {

    static syntax::Node* heading(
        const syntax::Parser& parser,
        const typography::Shaper& shaper,
        const configuration::Selection& selection,
        const std::string_view label
    ) {
        syntax::Mouth& mouth = parser.mouth();
        memory::Arena& arena = parser.arena();
        const memory::Location origin = mouth.lookahead().location;

        const std::vector<syntax::Token> title = mouth.argument({}, 0);
        std::string text(label);
        text += ' ';
        for (const syntax::Token& token : title) text += token.values;

        layout::Node* box = nullptr;
        if (const typography::Font* font = selection.font()) {
            const typography::Font* fonts[] = { font };
            const memory::Slice<layout::Node*> shaped = shaper.shape(memory::Slice{fonts, 1}, text, {});
            box = layout::Line::horizontal(arena, shaped, 0.0f);
        }

        return directive(arena, box, origin);
    }

    void Numbering::ingest(
        syntax::Parser& parser,
        const typography::Shaper& shaper,
        const configuration::Selection& selection,
        syntax::primitives::scaffold::References& references
    ) {
        parser.bind("\\section", [this, &shaper, &selection, &references](syntax::Parser& parser) -> syntax::Node* {
            ++primary;
            secondary = 0;
            tertiary = 0;
            const std::string label = std::format("{}", primary);
            references.mark(label);
            return heading(parser, shaper, selection, label);
        });

        parser.bind("\\subsection", [this, &shaper, &selection, &references](syntax::Parser& parser) -> syntax::Node* {
            ++secondary;
            tertiary = 0;
            const std::string label = std::format("{}.{}", primary, secondary);
            references.mark(label);
            return heading(parser, shaper, selection, label);
        });

        parser.bind("\\subsubsection", [this, &shaper, &selection, &references](syntax::Parser& parser) -> syntax::Node* {
            ++tertiary;
            const std::string label = std::format("{}.{}.{}", primary, secondary, tertiary);
            references.mark(label);
            return heading(parser, shaper, selection, label);
        });
    }

}*/