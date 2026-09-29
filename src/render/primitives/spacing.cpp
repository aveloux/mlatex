/// @file
/// @brief Spacing primitives: glue and kerns.
///
/// The spaces are bound from two tables rather than one closure each: they
/// differ only in which kind of node they make, how wide it is, and whether it
/// goes across the line or down the page. Writing that as a row keeps the
/// dimensioned ones honest with each other. `~` is bound on its own, since it
/// alone measures itself from the face rather than from a dimension or an em.
#include "render/primitives/spacing.hpp"
#include "layout/line.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include "syntax/number.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <span>
#include <string>
#include <string_view>

namespace render::primitives {

    /// @brief One side of glue's flexibility, as TeX writes it after `plus` or
    ///        `minus`: a length, or a number of `fil`, `fill` or `filll`.
    /// @param mouth     Expander to read from.
    /// @param registers Bank a length may resolve against.
    /// @param order     Set to the order of infinity it was written at.
    /// @return The amount: points, or a weight at an infinite order.
    static float flexibility(syntax::Mouth& mouth, const syntax::semantics::Registers& registers,
                             layout::Node::Order& order) {
        // Looked at as written first, since a weight in `fil` is no length
        // the dimension reader would take.
        std::size_t offset = 0;
        std::string digits;
        while (true) {
            const syntax::Token token = mouth.lookahead(offset);
            if (token.category == syntax::CatCodes::Category::Space && digits.empty()) {
                ++offset;
                continue;
            }
            if (token.text.size() == 1 && ((token.text[0] >= '0' && token.text[0] <= '9') ||
                                           token.text[0] == '.' || token.text[0] == '-')) {
                digits += token.text;
                ++offset;
                continue;
            }
            break;
        }
        std::size_t letters = 0;
        while (letters < 5 && mouth.lookahead(offset + letters).text.size() == 1 &&
               (mouth.lookahead(offset + letters).text[0] | 0x20) == "filll"[letters]) {
            ++letters;
        }
        if (letters >= 3) {
            for (std::size_t index = 0; index < offset + letters; ++index) mouth.read();
            order = letters == 3   ? layout::Node::Order::Fil
                    : letters == 4 ? layout::Node::Order::Fill
                                   : layout::Node::Order::Filll;
            float amount = 1.0f;
            if (!digits.empty()) std::from_chars(digits.data(), digits.data() + digits.size(), amount);
            return amount;
        }

        order = layout::Node::Order::Normal;
        const auto scanned = syntax::Number::dimension(mouth, registers);
        return scanned ? static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale) : 0.0f;
    }

    Spacing::Spacing(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\hskip");
    }

    void Spacing::operator()(syntax::Parser& parser, Context& context) const {
        /// @brief A space whose size the document writes out.
        struct Measured {
            std::string_view name;   ///< What the document writes.
            bool elastic;            ///< Glue, which flexes and may be broken at; else a kern.
            bool block;              ///< True when it goes down the page, not across the line.
        };

        static constexpr std::array<Measured, 3> measured{{
            {"\\hskip", true, false},
            {"\\vskip", true, true},
            {"\\kern", false, false},
        }};

        for (const auto& [name, elastic, block] : measured) {
            parser.bind(name, [&context, elastic, block](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth();
                memory::Arena& arena = parser.arena();
                const memory::Location origin = mouth.lookahead().location;

                // A glue register -- `\\vskip\\topsep` -- brings what it may
                // stretch and shrink by with it, unless `plus` or `minus`
                // after it says otherwise.
                using Registers = syntax::semantics::Registers;
                const auto held = context.registers.target(mouth.lookahead().symbol);
                const bool glued = held && held->type == Registers::Type::Glue;
                const auto scanned = syntax::Number::dimension(mouth, context.registers);
                const float width = scanned
                                        ? static_cast<float>(*scanned) /
                                              static_cast<float>(syntax::Number::scale)
                                        : 0.0f;

                auto* node = arena.compose<layout::Node>();
                if (elastic) {
                    // `plus` and `minus`, as TeX's glue is written:
                    // `\vskip 8pt plus 2pt minus 4pt`, `\hskip 0pt plus 1fill`.
                    layout::Node::Order expand = layout::Node::Order::Normal;
                    layout::Node::Order limit = layout::Node::Order::Normal;
                    float stretch = 0.0f;
                    float shrink = 0.0f;
                    if (glued) {
                        const std::int32_t orders = context.registers.get(Registers::Type::Order, held->slot);
                        stretch = static_cast<float>(context.registers.get(Registers::Type::Stretch, held->slot)) / 65536.0f;
                        shrink = static_cast<float>(context.registers.get(Registers::Type::Shrink, held->slot)) / 65536.0f;
                        expand = static_cast<layout::Node::Order>(orders & 3);
                        limit = static_cast<layout::Node::Order>((orders >> 2) & 3);
                    }
                    if (syntax::Argument::keyword(mouth, "plus")) stretch = flexibility(mouth, context.registers, expand);
                    if (syntax::Argument::keyword(mouth, "minus")) shrink = flexibility(mouth, context.registers, limit);
                    node->glue({.width = width, .stretch = stretch, .shrink = shrink, .expand = expand, .limit = limit});
                } else {
                    node->kern({.width = width});
                }
                return directive(arena, node, origin, block);
            });
        }

        /// @brief A space whose size is fixed by its name.
        struct Named {
            std::string_view name;       ///< What the document writes.
            float width;                 ///< Fixed width, in ems of the text face.
            float stretch;               ///< How much it may grow; 0 makes it a kern.
            float shrink;                ///< How much it may give, the same way.
            layout::Node::Order order;   ///< At which order of infinity it grows.
            bool block;                  ///< True when it goes down the page, not across.
        };

        // A finite width makes a kern unless it also carries stretch or
        // shrink, which makes it glue instead; an infinite stretch is glue
        // that fills whatever room is left, and the higher order takes all
        // the slack when both appear in the same line. The three skips keep
        // TeX's own ratio of a third of the width each way. The small spaces
        // are a formula's thin, medium and thick -- three, four and five
        // eighteenths of an em -- written in the text, where LaTeX gives
        // them the same widths.
        static constexpr std::array<Named, 22> named{{
            {"\\quad", 1.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\qquad", 2.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\enspace", 0.5f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\enskip", 0.5f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\thinspace", 3.0f / 18.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\negthinspace", -3.0f / 18.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\,", 3.0f / 18.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\:", 4.0f / 18.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\>", 4.0f / 18.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\;", 5.0f / 18.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\!", -3.0f / 18.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},
            {"\\/", 0.0f, 0.0f, 0.0f, layout::Node::Order::Normal, false},   // the faces' own kerning corrects italics
            {"\\hfil", 0.0f, 1.0f, 0.0f, layout::Node::Order::Fil, false},
            {"\\hfill", 0.0f, 1.0f, 0.0f, layout::Node::Order::Fill, false},
            {"\\vfil", 0.0f, 1.0f, 0.0f, layout::Node::Order::Fil, true},
            {"\\vfill", 0.0f, 1.0f, 0.0f, layout::Node::Order::Fill, true},
            {"\\hss", 0.0f, 1.0f, 1.0f, layout::Node::Order::Fil, false},
            {"\\vss", 0.0f, 1.0f, 1.0f, layout::Node::Order::Fil, true},
            {"\\hfilneg", 0.0f, -1.0f, 0.0f, layout::Node::Order::Fil, false},
            {"\\smallskip", 0.3f, 0.1f, 0.1f, layout::Node::Order::Normal, true},
            {"\\medskip", 0.6f, 0.2f, 0.2f, layout::Node::Order::Normal, true},
            {"\\bigskip", 1.2f, 0.4f, 0.4f, layout::Node::Order::Normal, true},
        }};

        for (const auto& [name, width, stretch, shrink, order, block] : named) {
            parser.bind(name, [&context, width, stretch, shrink, order, block](syntax::Parser& parser) -> syntax::Node* {
                memory::Arena& arena = parser.arena();
                const memory::Location origin = parser.mouth().lookahead().location;

                // An em of whatever face the text is in where the space is
                // written, so a quad in a heading is a heading's quad.
                const float em = context.selection.text() ? context.selection.text()->size() : 10.0f;

                // Fil and fill orders are infinite: #stretch there is a
                // dimensionless weight against other infinite glue on the
                // same line, not a length, so only a finite (Normal) amount
                // scales with the face -- an \\hfil is the same weight in
                // any size text.
                const bool finite = order == layout::Node::Order::Normal;

                auto* node = arena.compose<layout::Node>();
                if (stretch != 0.0f || shrink != 0.0f) {
                    node->glue({.width = width * em, .stretch = finite ? stretch * em : stretch,
                               .shrink = finite ? shrink * em : shrink, .expand = order,
                               .limit = finite || shrink == 0.0f ? layout::Node::Order::Normal : order});
                } else {
                    node->kern({.width = width * em});
                }
                return directive(arena, node, origin, block);
            });
        }

        // TeX's tie: an ordinary word space that a line may not break at,
        // for "Mr.~Smith" and "Figure~3". Measured from the face itself
        // rather than an em fraction, so it matches the spaces around it.
        parser.bind("~", [&context](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            const typography::Font* font = context.selection.text();
            float width = font ? font->size() * 0.25f : 2.5f;   // a plausible space, if there is no face yet

            if (font) {
                const typography::Font* fonts[] = {font};
                const memory::Slice<layout::Node*> shaped =
                    context.shaper.shape(memory::Slice{fonts, 1uz}, " ", {});
                if (shaped.count == 1 && shaped[0]->type() == layout::Node::Type::Glue) {
                    width = shaped[0]->glue().width;
                }
            }

            auto* node = arena.compose<layout::Node>();
            node->kern({.width = width});
            return directive(arena, node, origin);
        });

        // LaTeX's leaders: glue that fills what is left of the line as \hfill
        // does, drawn along -- a rule for \hrulefill, dots for \dotfill, each
        // in a box .44em wide, as LaTeX's own.
        for (const bool dotted : {false, true}) {
            parser.bind(dotted ? "\\dotfill" : "\\hrulefill", [&context, dotted](syntax::Parser& parser) -> syntax::Node* {
                memory::Arena& arena = parser.arena();
                const memory::Location origin = parser.mouth().lookahead().location;

                layout::Node* leader = arena.compose<layout::Node>(layout::Node::Type::Rule);
                leader->rule({.height = 0.4f});
                if (const typography::Font* font = context.selection.text(); dotted && font) {
                    const typography::Font* fonts[] = {font};
                    const memory::Slice<layout::Node*> dot = context.shaper.shape(memory::Slice{fonts, 1uz}, ".", {});
                    const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(dot.count + 2);
                    for (const std::size_t end : {0uz, dot.count + 1}) {
                        row[end] = arena.compose<layout::Node>(layout::Node::Type::Glue);
                        row[end]->glue({.stretch = 1.0f, .shrink = 1.0f, .expand = layout::Node::Order::Fil,
                                        .limit = layout::Node::Order::Fil});
                    }
                    std::ranges::copy(dot, row.begin() + 1);
                    leader = layout::Line::horizontal(arena, row, font->size() * 0.44f);
                }

                auto* node = arena.compose<layout::Node>(layout::Node::Type::Glue);
                node->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fill, .leader = leader});
                return directive(arena, node, origin);
            });
        }

        // TeX's control space, `\ `: an ordinary word space written out --
        // after an abbreviation, "Mr.\ Smith", or after a control word that
        // swallowed the space typed after it. It is the very glue the shaper
        // makes for a space, so it stretches, shrinks and breaks as the
        // spaces around it do. A backslash ending a line is the same thing,
        // the line's end standing for the space.
        const auto spaced = [&context](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;

            if (const typography::Font* font = context.selection.text()) {
                const typography::Font* fonts[] = {font};
                const memory::Slice<layout::Node*> shaped =
                    context.shaper.shape(memory::Slice{fonts, 1uz}, " ", {});
                if (shaped.count == 1 && shaped[0]->type() == layout::Node::Type::Glue) {
                    return directive(arena, shaped[0], origin);
                }
            }

            // No face yet: a third of an em, with TeX's own give either way.
            auto* node = arena.compose<layout::Node>();
            node->glue({.width = 4.0f, .stretch = 2.0f, .shrink = 1.33f});
            return directive(arena, node, origin);
        };
        parser.bind("\\ ", spaced);

        // TeX's \ignorespaces: the blanks after it are the source's layout,
        // and are read past -- expanding as it goes, so a `\label` standing
        // between them is still run.
        parser.bind("\\ignorespaces", [](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            for (syntax::Token token = mouth.expand(); !token.empty(); token = mouth.expand()) {
                if (token.category == syntax::CatCodes::Category::Space) continue;
                mouth.stream().inject(std::span{&token, 1});
                break;
            }
            return nullptr;
        });
        parser.bind("\\\n", spaced);
        parser.bind("\\\r\n", spaced);

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound spacing primitives");
    }

}
