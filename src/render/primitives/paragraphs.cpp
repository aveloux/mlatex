/// @file
/// @brief Paragraph shape primitives: indentation, alignment and margin.
///
/// The six switches share one closure, told apart by the instruction each
/// carries, and the three alignment blocks share one pair of hooks. Nothing
/// here measures or draws; everything becomes an instruction to the document.
#include "render/primitives/paragraphs.hpp"
#include "logger.hpp"

#include "syntax/number.hpp"

#include <array>
#include <span>
#include <utility>

namespace render::primitives {

    Paragraphs::Paragraphs(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\noindent");
        lexicon.intern("\\indent");
        lexicon.intern("\\centering");
        lexicon.intern("\\leftskip");
    }

    void Paragraphs::operator()(syntax::Parser& parser, Context& context) const {
        using Command = layout::Node::Directive::Command;
        using Justification = layout::Node::Justification;

        /// @brief One switch: its name, and the instruction it becomes.
        struct Switch {
            std::string_view name;           ///< What the document writes.
            Command command;                 ///< What the document is told.
            Justification justification;     ///< For Command::Align.
        };

        static constexpr std::array<Switch, 6> switches{{
            {"\\indent", Command::Indent, Justification::Full},
            {"\\noindent", Command::Flush, Justification::Full},
            {"\\justifying", Command::Align, Justification::Full},
            {"\\raggedright", Command::Align, Justification::Left},
            {"\\raggedleft", Command::Align, Justification::Right},
            {"\\centering", Command::Align, Justification::Center},
        }};

        for (const auto& [name, command, justification] : switches) {
            parser.bind(name, [command, justification](syntax::Parser& parser) -> syntax::Node* {
                memory::Arena& arena = parser.arena();
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Directive);
                node->directive({.command = command, .justification = justification});
                return directive(arena, node, parser.mouth().lookahead().location);
            });
        }

        // TeX's `\\leftskip` and `\\rightskip`, as dimensions rather than
        // glue: how far in from each edge every line of the paragraphs from
        // here on starts and ends.
        static constexpr std::array<std::pair<std::string_view, bool>, 2> edges{{
            {"\\leftskip", false},
            {"\\rightskip", true},
        }};

        for (const auto& [name, trailing] : edges) {
            parser.bind(name, [this, &context, trailing, name](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth();
                memory::Arena& arena = parser.arena();
                const memory::Location origin = mouth.lookahead().location;

                // The `=` is noise: read it if it is there, put back what is not.
                syntax::Token equals = mouth.read();
                if (equals.text != "=") mouth.stream().inject(std::span{&equals, 1});

                const auto scanned = syntax::Number::dimension(mouth, context.registers);
                if (!scanned) {
                    tracebacks_.emplace_back(syntax::Traceback::Type::Dimension, origin,
                                             std::string(name) + " needs a dimension");
                    return nullptr;
                }

                auto* node = arena.compose<layout::Node>(layout::Node::Type::Directive);
                node->directive({
                    .command = Command::Margin,
                    .width = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale),
                    .trailing = trailing
                });
                return directive(arena, node, origin);
            });
        }

        // The blocks end the paragraph before them and set their own shape,
        // as the switches would; ending one ends its last paragraph and
        // restores whatever the block around it had set. Each opening is
        // written whole -- both margins and the alignment, whether or not
        // this block changes every one of them -- so that returning to it,
        // or to the document's own baseline once none is left open, is never
        // missing a piece another block further out happened to leave set.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> blocks{{
            {"center", "\\par\\leftskip=0pt\\rightskip=0pt\\centering "},
            {"flushleft", "\\par\\leftskip=0pt\\rightskip=0pt\\raggedright "},
            {"flushright", "\\par\\leftskip=0pt\\rightskip=0pt\\raggedleft "},
            // ragged2e's names for the same three, and its justified block.
            {"Center", "\\par\\leftskip=0pt\\rightskip=0pt\\centering "},
            {"FlushLeft", "\\par\\leftskip=0pt\\rightskip=0pt\\raggedright "},
            {"FlushRight", "\\par\\leftskip=0pt\\rightskip=0pt\\raggedleft "},
            {"justify", "\\par\\leftskip=0pt\\rightskip=0pt\\justifying "},
            // A passage set in from both margins: quote for a short one,
            // quotation for one long enough to need its paragraphs indented
            // as body text is, verse for lines of poetry, ragged at the right
            // where a line ends on its own rather than where the column does.
            {"quote", "\\par\\leftskip=2.5em\\rightskip=2.5em\\justifying\\noindent "},
            {"quotation", "\\par\\leftskip=2.5em\\rightskip=2.5em\\justifying "},
            {"verse", "\\par\\leftskip=2.5em\\rightskip=2.5em\\raggedright\\noindent "},
        }};

        // The body's own alignment is the class's \\@bodyshape: justified,
        // or beamer's ragged right.
        static constexpr std::string_view baseline =
            "\\par\\leftskip=0pt\\rightskip=0pt\\@bodyshape\\noindent ";

        for (const auto& [name, opening] : blocks) {
            context.blocks.watch(
                name,
                [this, opening](syntax::Mouth& mouth) {
                    openings.push_back(opening);
                    mouth.ingest(opening);
                },
                [this](syntax::Mouth& mouth) {
                    // Text straight after the block carries on from it, and
                    // so is not indented unless a blank line comes first.
                    if (!openings.empty()) openings.pop_back();
                    mouth.ingest(openings.empty() ? baseline : openings.back());
                });
        }

        // The abstract, and the keywords after it, as the class sets them:
        // their heads and their shape are the core's \\@abstracthead,
        // \\@keywordshead and \\@abstractshape -- the article class's
        // `\\abstractname` centred in bold over the text in `\\small`, set in
        // from both margins -- which a class redefines, as IEEEtran runs its
        // `Abstract---` in. Watched transparent, because the group that keeps
        // `\\small` to them is their own to close.
        static constexpr std::string_view abstract = "\\par\\@abstractshape ";
        for (const auto& [name, head] : {std::pair<std::string_view, std::string_view>{"abstract", "{\\@abstracthead "},
                                         {"keywords", "{\\@keywordshead "},
                                         {"keyword", "{\\@keywordshead "},
                                         {"IEEEkeywords", "{\\@keywordshead "}}) {
            context.blocks.watch(
                name,
                [this, head](syntax::Mouth& mouth) {
                    openings.push_back(abstract);
                    mouth.ingest(head);
                },
                [this](syntax::Mouth& mouth) {
                    // What is ingested last is read first: the block's end,
                    // then the shape of whatever it stood in.
                    if (!openings.empty()) openings.pop_back();
                    mouth.ingest(openings.empty() ? baseline : openings.back());
                    mouth.ingest("\\par}");
                },
                /*transparent=*/true);
        }

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound paragraph primitives");
    }

}
