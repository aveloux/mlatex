/*#include "render/primitives/structure/list.hpp"
#include "render/primitives/structure/content.hpp"
#include "render/primitives/directive.hpp"

#include "render/layout/line.hpp"
#include "logger.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"

#include <format>
#include <span>
#include <string>
#include <vector>

namespace render::primitives::structure {

    void Lists::ingest(
        syntax::Parser& parser,
        syntax::primitives::scaffold::Environments& environments,
        const typography::Shaper& shaper,
        const layout::Typesetter& typesetter,
        const configuration::Selection& selection
    ) {
        environments.define(
            "itemize",
            [this](syntax::Mouth&) { levels.push_back(Level{ Type::Bullet, 0 }); },
            [this](syntax::Mouth&) { if (!levels.empty()) levels.pop_back(); }
        );

        environments.define(
            "enumerate",
            [this](syntax::Mouth&) { levels.push_back(Level{ Type::Number, 0 }); },
            [this](syntax::Mouth&) { if (!levels.empty()) levels.pop_back(); }
        );

        environments.define(
            "description",
            [this](syntax::Mouth&) { levels.push_back(Level{ Type::Description, 0 }); },
            [this](syntax::Mouth&) { if (!levels.empty()) levels.pop_back(); }
        );

        parser.bind("\\item", [this, &shaper, &typesetter, &selection](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            if (levels.empty()) {
                Logger::log(Logger::Type::Semantics, Logger::Level::Warning, "\\item used outside a list environment");
                levels.push_back(Level{ Type::Bullet, 0 });
            }

            Level& level = levels.back();

            std::string label;
            if (level.type == Type::Description) {
                syntax::Token open = mouth.read();
                if (open.values == "[") {
                    while (true) {
                        const syntax::Token token = mouth.read();
                        if (token.values.empty() || token.values == "]") break;
                        label += token.values;
                    }
                } else if (!open.empty()) {
                    mouth.inject(std::span{&open, 1});
                }
            } else if (level.type == Type::Number) {
                ++level.index;
                label = std::format("{}.", level.index);
            } else {
                label = "*";
            }

            syntax::Token brace = mouth.read();
            if (brace.values != "{" && !brace.empty()) {
                mouth.inject(std::span{&brace, 1});
            }

            const memory::Slice<syntax::Node*> children = parser.parse('}');

            std::vector<layout::Node*> nodes;
            const float indent = 18.0f * static_cast<float>(levels.size());

            auto* margin = arena.compose<layout::Node>(layout::Node::Type::Kern);
            margin->kern({ .width = indent });
            nodes.push_back(margin);

            if (!label.empty()) {
                if (const typography::Font* font = selection.font()) {
                    const typography::Font* fonts[] = { font };
                    const memory::Slice<layout::Node*> shaped = shaper.shape(memory::Slice{fonts, 1}, label + " ", {});
                    for (std::size_t index = 0; index < shaped.size(); ++index) {
                        nodes.push_back(shaped[index]);
                    }
                }
            }

            for (std::size_t index = 0; index < children.size(); ++index) {
                append(nodes, children[index], shaper, selection.font(), typesetter);
            }

            memory::Slice<layout::Node*> slice = arena.allocate<layout::Node*>(nodes.size());
            for (std::size_t index = 0; index < nodes.size(); ++index) {
                slice[index] = nodes[index];
            }

            layout::Node* box = layout::Line::horizontal(arena, slice, 0.0f);
            return directive(arena, box, origin);
        });
    }

}*/