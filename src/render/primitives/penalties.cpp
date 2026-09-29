/// @file
/// @brief Penalty primitives: `\\penalty`, `\\nobreak` and the forced breaks.
#include "render/primitives/penalties.hpp"
#include "syntax/number.hpp"
#include "logger.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>

namespace render::primitives {

    Penalties::Penalties(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\penalty");
    }

    void Penalties::operator()(syntax::Parser& parser, Context& context) const {
        parser.bind("\\penalty", [&context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            const auto scanned = syntax::Number::integer(mouth, context.registers);

            auto* node = arena.compose<layout::Node>();
            node->penalty({.value = scanned.value_or(0)});
            return directive(arena, node, origin);
        });

        // The fixed ones. A page break is a Pause rather than a Penalty so
        // that the pager can tell the two apart: one ends a line, the other
        // ends a sheet.
        struct Fixed {
            std::string_view name;    ///< What the document writes.
            std::int32_t value;       ///< What a break there costs.
            bool page;                ///< True when it ends the page, not the line.
            bool optional;            ///< True for LaTeX's forms, which take `*` and `[...]`.
            bool sheet;               ///< True when it ends the whole sheet, every column on it.
        };

        // `\\newpage` and `\\clearpage` are LaTeX's names for the same break
        // on a page of one column; on a page of two, the first ends the
        // column and the second the whole sheet. Bound directly rather than left to `\\alias`, which reaches Mouth's
        // own tables and not this one -- a page break is a Node this module
        // builds, and nothing outside it can copy that.
        static constexpr std::array<Fixed, 8> fixed{{
            {"\\nobreak", absolute, false, false, false},
            {"\\break", -absolute, false, false, false},
            {"\\\\", -absolute, false, true, false},
            {"\\newline", -absolute, false, true, false},
            {"\\linebreak", -absolute, false, true, false},
            {"\\pagebreak", -absolute, true, true, false},
            {"\\newpage", -absolute, true, true, false},
            {"\\clearpage", -absolute, true, true, true},
        }};

        for (const auto& [name, value, page, optional, sheet] : fixed) {
            parser.bind(name, [&context, name, value, page, optional, sheet](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth;
                memory::Arena& arena = parser.arena;
                const memory::Location origin = mouth.lookahead().location;

                // LaTeX's optional forms -- `\\*`, `\\[4pt]`, `\\linebreak[3]`
                // -- are read: the break is forced either way, and the space
                // `\\[4pt]` asks for below its line is a rule of no width
                // hanging that far under the line's baseline, past the
                // depth of its letters, which the next line keeps clear of.
                float below = 0.0f;
                if (optional) {
                    syntax::Token next = mouth.read();
                    if (next.text == "*") next = mouth.read();
                    if (next.text == "[") {
                        std::string written;
                        for (next = mouth.read(); !next.empty() && next.text != "]"; next = mouth.read()) {
                            written += next.text;
                        }
                        if (name == "\\\\" || name == "\\newline") below = measure(written, mouth, context);
                    } else if (!next.empty()) {
                        mouth.stream().inject(std::span{&next, 1});
                    }
                }

                auto* node = arena.compose<layout::Node>();
                if (page) {
                    node->pause({.penalty = {.value = value, .flag = sheet}});
                } else {
                    node->penalty({.value = value});
                }
                if (below <= 0.0f) return directive(arena, node, origin, page);

                const float em = context.selection.text() ? context.selection.text()->size() : 10.0f;
                auto* hanging = arena.compose<layout::Node>(layout::Node::Type::Rule);
                hanging->rule({.depth = below + 0.3f * em});
                const memory::Slice<syntax::Node*> parts = arena.allocate<syntax::Node*>(2);
                parts[0] = directive(arena, hanging, origin);
                parts[1] = directive(arena, node, origin);
                return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, parts);
            });
        }

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound penalty primitives");
    }

}
