#include "render/primitives/configuration/page.hpp"

#include "syntax/number.hpp"

#include <span>
#include <string>
#include <vector>

namespace render::primitives::configuration {

    void document(syntax::Mouth& mouth, layout::Document& document_, syntax::semantics::Registers& registers) {
        const syntax::Symbol identifier = mouth.lexicon().intern("\\count");
        const syntax::Symbol dimension = mouth.lexicon().intern("\\dimen");

        mouth.bind("\\pagewidth", [&document_, &registers, identifier, dimension](syntax::Mouth& mouth) {
            syntax::Token equals = mouth.read();
            if (equals.values != "=") mouth.inject(std::span{&equals, 1});
            if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                document_.configuration().width = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
            }
        });

        mouth.bind("\\pageheight", [&document_, &registers, identifier, dimension](syntax::Mouth& mouth) {
            syntax::Token equals = mouth.read();
            if (equals.values != "=") mouth.inject(std::span{&equals, 1});
            if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                document_.configuration().height = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
            }
        });

        mouth.bind("\\leftmargin", [&document_, &registers, identifier, dimension](syntax::Mouth& mouth) {
            syntax::Token equals = mouth.read();
            if (equals.values != "=") mouth.inject(std::span{&equals, 1});
            if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                document_.configuration().left = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
            }
        });

        mouth.bind("\\rightmargin", [&document_, &registers, identifier, dimension](syntax::Mouth& mouth) {
            syntax::Token equals = mouth.read();
            if (equals.values != "=") mouth.inject(std::span{&equals, 1});
            if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                document_.configuration().right = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
            }
        });

        mouth.bind("\\topmargin", [&document_, &registers, identifier, dimension](syntax::Mouth& mouth) {
            syntax::Token equals = mouth.read();
            if (equals.values != "=") mouth.inject(std::span{&equals, 1});
            if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                document_.configuration().top = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
            }
        });

        mouth.bind("\\bottommargin", [&document_, &registers, identifier, dimension](syntax::Mouth& mouth) {
            syntax::Token equals = mouth.read();
            if (equals.values != "=") mouth.inject(std::span{&equals, 1});
            if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                document_.configuration().bottom = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
            }
        });

        mouth.bind("\\leading", [&document_, &registers, identifier, dimension](syntax::Mouth& mouth) {
            syntax::Token equals = mouth.read();
            if (equals.values != "=") mouth.inject(std::span{&equals, 1});
            if (const auto value = mouth.dimension(registers, identifier, dimension)) {
                document_.configuration().leading = static_cast<float>(*value) / static_cast<float>(syntax::Number::scale);
            }
        });

        mouth.bind("\\documentclass", [&document_](syntax::Mouth& mouth) {
            const std::vector<syntax::Token> tokens = mouth.argument({}, 0);
            std::string name;
            for (const syntax::Token& token : tokens) name += token.values;

            layout::Document::Configuration settings = document_.configuration();

            if (name == "article" || name == "letter") {
                settings.width = 612.0f;
                settings.height = 792.0f;
                settings.left = 72.0f;
                settings.right = 72.0f;
                settings.top = 72.0f;
                settings.bottom = 72.0f;
            } else if (name == "book" || name == "report") {
                settings.width = 612.0f;
                settings.height = 792.0f;
                settings.left = 90.0f;
                settings.right = 54.0f;
                settings.top = 72.0f;
                settings.bottom = 72.0f;
            } else if (name == "a4paper") {
                settings.width = 595.0f;
                settings.height = 842.0f;
            }

            document_.configuration() = settings;
        });
    }

}