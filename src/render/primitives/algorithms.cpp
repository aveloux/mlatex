/// @file
/// @brief Pseudocode implementation: numbered lines in blocks that step in.
#include "render/primitives/algorithms.hpp"
#include "render/primitives/numeral.hpp"
#include "render/primitives/styles.hpp"
#include "syntax/argument.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace render::primitives {

    Algorithms::Algorithms(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\State");
        lexicon.intern("\\Statex");
        lexicon.intern("\\algorithmopen");
        lexicon.intern("\\algorithmclose");
    }

    syntax::Node* Algorithms::line(syntax::Parser& parser, Context& context, const bool numbered) const {
        memory::Arena& arena = parser.arena;
        const memory::Location origin = parser.mouth.lookahead().location;

        if (blocks.empty()) {
            tracebacks.emplace_back(syntax::Traceback::Type::Environment, origin,
                                     "A pseudocode line outside an algorithmic block");
            blocks.push_back(Block{});
        }
        Block& block = blocks.back();

        const float em = context.selection.text() ? context.selection.text()->size()
                                                  : context.document.configuration.size;
        const bool counted = numbered && block.every > 0;
        if (numbered) ++block.lines;

        // Room at the left for the numbers, when the block has them, and a
        // step in for each block the line is inside.
        const float numbers = block.every > 0 ? em * 1.5f : 0.0f;
        const float gap = block.every > 0 ? em * 0.5f : 0.0f;
        const float step = em * indent * static_cast<float>(block.depth);
        const float margin = numbers + gap + step;

        // The number, right-aligned in its room: algorithmicx's
        // \alglinenumber, `\footnotesize 3:`.
        layout::Node* label = nullptr;
        if (counted && block.lines % block.every == 0) {
            const float small = Styles::measure(context.document.configuration.size, Styles::Size::Footnote);
            if (const typography::Font* font = Styles::resolve(context, Styles::Cut::Normal, small)) {
                const typography::Font* fonts[] = {font};
                const memory::Slice<layout::Node*> shaped = context.shaper.shape(
                    memory::Slice{fonts, 1uz}, arena.copy(Numeral::arabic(block.lines) + ":"), {});
                label = layout::Line::horizontal(arena, shaped, 0.0f);
            }
        }

        const auto kern = [&arena](const float width) {
            auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
            node->kern({.width = width});
            return node;
        };

        // Back from the margin to the block's edge, the number filling its
        // room from the right, and on to where the text starts.
        const float width = label ? label->box().width : 0.0f;
        const memory::Slice<layout::Node*> hang = arena.allocate<layout::Node*>(4);
        std::size_t filled = 0;
        hang[filled++] = kern(-margin + std::max(numbers - width, 0.0f));
        if (label) hang[filled++] = label;
        hang[filled++] = kern(gap + step + std::min(numbers - width, 0.0f));

        auto* shift = arena.compose<layout::Node>(layout::Node::Type::Directive);
        shift->directive({.command = layout::Node::Directive::Command::Margin, .width = margin});
        auto* flush = arena.compose<layout::Node>(layout::Node::Type::Directive);
        flush->directive({.command = layout::Node::Directive::Command::Flush});

        const memory::Slice<syntax::Node*> parts = arena.allocate<syntax::Node*>(4);
        parts[0] = arena.compose<syntax::Node>(syntax::Node::Type::Paragraph, std::string_view{}, origin);
        parts[1] = directive(arena, shift, origin);
        parts[2] = directive(arena, flush, origin);
        parts[3] = directive(arena, layout::Line::horizontal(arena, memory::Slice{hang.data, filled}, 0.0f), origin);
        return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, parts);
    }

    void Algorithms::operator()(syntax::Parser& parser, Context& context) const {
        // `[n]`: every nth line numbered, `[1]` every line, none by default.
        context.blocks.watch(
            "algorithmic",
            [this](syntax::Mouth& mouth) {
                std::string every;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    every += token.text;
                }
                Block block{};
                std::from_chars(every.data(), every.data() + every.size(), block.every);
                blocks.push_back(block);
                mouth.ingest("\\par\\vskip 2pt ");
            },
            [this](syntax::Mouth& mouth) {
                if (!blocks.empty()) blocks.pop_back();
                mouth.ingest("\\par\\vskip 2pt\\leftskip=0pt\\noindent ");
            });

        parser.bind("\\State", [this, &context](syntax::Parser& parser) { return line(parser, context, true); });
        parser.bind("\\Statex", [this, &context](syntax::Parser& parser) { return line(parser, context, false); });

        // A block's opening line is set before its lines step in, and its
        // closing line after they step back out: the package writes these
        // after the one and before the other.
        parser.mouth.bind("\\algorithmopen", [this](syntax::Mouth&) {
            if (!blocks.empty()) ++blocks.back().depth;
        });
        parser.mouth.bind("\\algorithmclose", [this](syntax::Mouth&) {
            if (!blocks.empty() && blocks.back().depth > 0) --blocks.back().depth;
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound pseudocode primitives");
    }

}
