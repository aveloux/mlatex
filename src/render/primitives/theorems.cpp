/// @file
/// @brief Theorem primitives: `\\newtheorem`, `\\theoremstyle`, the proof block
///        and `\\qed`.
///
/// A theorem is read as the source text amsthm would have typeset: its head,
/// then its body inside a group in the body's face. The head is ingested in
/// front of what the document wrote, so the body is parsed exactly as any
/// other text is, labels, formulas and lists included.
#include "render/primitives/theorems.hpp"
#include "render/primitives/counters.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace render::primitives {

    /// \\topsep, the space LaTeX keeps above and below a theorem or a proof.
    static constexpr std::string_view around = "\\par\\vskip 8pt plus 2pt minus 4pt";

    Theorems::Theorems(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\newtheorem");
        lexicon.intern("\\theoremstyle");
        lexicon.intern("\\newtheoremstyle");
        lexicon.intern("\\declaretheorem");
        lexicon.intern("\\qed");
        lexicon.intern("\\qedhere");
    }

    void Theorems::operator()(syntax::Parser& parser, Context& context) const {
        styles.emplace("plain", Style::Plain);
        styles.emplace("definition", Style::Definition);
        styles.emplace("remark", Style::Remark);

        parser.mouth.bind("\\theoremstyle", [this](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            const auto found = styles.find(name);
            if (found == styles.end()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, mouth.lookahead().location,
                                         "No theorem style named '" + name + "'");
                style = Style::Plain;
                return;
            }
            style = found->second;
        });

        // amsthm's own way of making a style: nine arguments, of which the
        // body's font and the head's decide which of the three it reads as.
        parser.mouth.bind("\\newtheoremstyle", [this](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            static_cast<void>(syntax::Argument::text(mouth));   // space above
            static_cast<void>(syntax::Argument::text(mouth));   // space below
            const std::string body = syntax::Argument::text(mouth);
            static_cast<void>(syntax::Argument::text(mouth));   // indentation
            const std::string heading = syntax::Argument::text(mouth);
            for (int remaining = 0; remaining < 3; ++remaining) static_cast<void>(syntax::Argument::text(mouth));

            const bool slanted = body.find("\\itshape") != std::string::npos ||
                                 body.find("\\slshape") != std::string::npos;
            const bool quiet = heading.find("\\itshape") != std::string::npos;
            styles[name] = slanted ? Style::Plain : quiet ? Style::Remark : Style::Definition;
        });

        // One hook serves every theorem kind: it looks the kind up by the
        // name it was watched under, so declaring a kind again replaces what
        // its head and body are, and adds no second hook.
        const auto entering = [this, &context](const std::string& name) {
            return [this, &context, name](syntax::Mouth& mouth) {
                const auto found = kinds.find(name);
                if (found == kinds.end()) return;
                const Kind kind = found->second;

                std::string number;
                if (!kind.counter.empty() && context.counters) {
                    context.counters->step(kind.counter);
                    number = Counters::print(mouth, kind.counter);
                    context.anchor = number;
                    context.kind = name;
                }

                const std::vector<syntax::Token> note =
                    mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0);

                // The head's full stop in the head's own face, then a space
                // and the body in its face, opened here and closed at \end.
                const std::string stop = kind.style == Style::Remark ? "{\\itshape .}" : "{\\bfseries .}";
                const std::string body = kind.style == Style::Plain ? "{\\itshape\\ignorespaces " : "{\\ignorespaces ";
                // The head: the name and its number, bold -- italic in the
                // remark style.
                const std::string title = number.empty() ? kind.title : kind.title + " " + number;
                const std::string opening = std::string(around) + "\\noindent " +
                                            (kind.style == Style::Remark ? "{\\itshape " + title + "}"
                                                                         : "{\\bfseries " + title + "}");

                // Read in the order written: the head, the note in its
                // parentheses, and the body. Each is put in front of the one
                // after it, so they go in last first.
                if (note.empty()) {
                    mouth.ingest(context.arena.copy(opening + stop + "\\ " + body));
                    return;
                }
                mouth.ingest(context.arena.copy("})" + stop + "\\ " + body));
                mouth.stream().inject(std::span{note});
                mouth.ingest(context.arena.copy(opening + "\\ ({\\normalfont "));
            };
        };
        const syntax::Mouth::Handler leaving = [](syntax::Mouth& mouth) {
            mouth.ingest("}\\par\\vskip 8pt plus 2pt minus 4pt\\noindent ");
        };

        // \newtheorem{name}{Title}, \newtheorem{name}[shared]{Title},
        // \newtheorem{name}{Title}[within], and the starred form, which is
        // never numbered.
        parser.mouth.bind("\\newtheorem", [this, &context, entering, leaving](syntax::Mouth& mouth) {
            const bool starred = mouth.lookahead().is('*');
            if (starred) mouth.read();

            const std::string name = syntax::Argument::text(mouth);
            std::string shared;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                shared += token.text;
            }
            const std::string title = syntax::Argument::text(mouth);
            std::string within;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                within += token.text;
            }

            if (name.empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, mouth.lookahead().location,
                                         "\\newtheorem needs a name");
                return;
            }

            Kind kind{.title = title, .style = style};
            if (!starred) {
                kind.counter = shared.empty() ? name : shared;
                if (shared.empty() && context.counters) context.counters->define(mouth, name, within);
            }

            // What \cref calls it, unless the document or cleveref said.
            if (!context.variables.get("cref." + name)) {
                std::string lower = title;
                if (!lower.empty() && lower[0] >= 'A' && lower[0] <= 'Z') lower[0] = static_cast<char>(lower[0] + 32);
                context.variables.define("cref." + name, lower);
            }
            if (!context.variables.get("Cref." + name)) context.variables.define("Cref." + name, title);

            const bool fresh = !kinds.contains(name);
            kinds[name] = std::move(kind);
            if (fresh) context.blocks.watch(name, entering(name), leaving, /*transparent=*/true);
        });

        // thmtools' \declaretheorem[keys]{name}: \newtheorem with its title
        // `name=` or the name capitalised, `numberwithin=`, `sibling=` and
        // `numbered=no` its within, shared and starred forms, and `style=`
        // the style it is set in.
        parser.mouth.bind("\\declaretheorem", [](syntax::Mouth& mouth) {
            std::string keys;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                keys += token.text;
            }
            const std::string name = syntax::Argument::text(mouth);
            std::string title = name;
            if (!title.empty() && title[0] >= 'a' && title[0] <= 'z') title[0] = static_cast<char>(title[0] - 32);
            std::string within;
            std::string shared;
            std::string style;
            bool starred = false;
            for (const auto piece : std::views::split(std::string_view{keys}, ',')) {
                const std::string_view option(piece.begin(), piece.end());
                const std::size_t equals = option.find('=');
                const auto trimmed = [](std::string_view text) {
                    while (!text.empty() && (text.front() == ' ' || text.front() == '{')) text.remove_prefix(1);
                    while (!text.empty() && (text.back() == ' ' || text.back() == '}')) text.remove_suffix(1);
                    return std::string(text);
                };
                const std::string key = trimmed(option.substr(0, equals));
                const std::string value = equals == std::string_view::npos ? std::string{} : trimmed(option.substr(equals + 1));
                if (key == "name" || key == "title" || key == "heading") title = value;
                if (key == "numberwithin" || key == "within" || key == "parent") within = value;
                if (key == "sibling" || key == "numberlike" || key == "sharenumber") shared = value;
                if (key == "style") style = value;
                if (key == "numbered" && value == "no") starred = true;
            }
            std::string call = style.empty() ? std::string{} : "\\theoremstyle{" + style + "}";
            call += starred ? "\\newtheorem*{" : "\\newtheorem{";
            call += name + "}";
            if (!shared.empty()) call += "[" + shared + "]";
            call += "{" + title + "}";
            if (!within.empty()) call += "[" + within + "]";
            mouth.ingest(mouth.arena.copy(call));
        });

        // The proof: its head in italic, `\proofname` unless it names its
        // own, and the box at the end.
        context.blocks.watch(
            "proof",
            [&context](syntax::Mouth& mouth) {
                const std::vector<syntax::Token> note =
                    mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0);
                if (note.empty()) {
                    mouth.ingest(context.arena.copy(std::string(around) +
                                                    "\\noindent{\\itshape\\proofname.}\\ {\\ignorespaces "));
                    return;
                }
                mouth.ingest(".}\\ {\\ignorespaces ");
                mouth.stream().inject(std::span{note});
                mouth.ingest(context.arena.copy(std::string(around) + "\\noindent{\\itshape "));
            },
            [](syntax::Mouth& mouth) { mouth.ingest("\\qed}\\par\\vskip 8pt plus 2pt minus 4pt\\noindent "); },
            /*transparent=*/true);

        // The end of a proof's mark, carried out to the margin by glue of the
        // second infinite order, which outpulls the paragraph's own fill;
        // held to the word before it, and a quad clear of it at the least.
        parser.bind("\\qed", [&context](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            const float em = context.selection.text() ? context.selection.text()->size() : 10.0f;

            auto* hold = arena.compose<layout::Node>(layout::Node::Type::Penalty);
            hold->penalty({.value = 10000});
            auto* fill = arena.compose<layout::Node>(layout::Node::Type::Glue);
            fill->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fill});
            auto* quad = arena.compose<layout::Node>(layout::Node::Type::Kern);
            quad->kern({.width = em});

            const memory::Slice<syntax::Node*> parts = arena.allocate<syntax::Node*>(3);
            parts[0] = directive(arena, hold, origin);
            parts[1] = directive(arena, fill, origin);
            parts[2] = directive(arena, quad, origin);

            // The mark itself is read next, so a document's own \qedsymbol
            // is the one set.
            parser.mouth.ingest("\\qedsymbol ");
            return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, parts);
        });

        // \qedhere puts the box where it stands in LaTeX; here the box stays
        // at the proof's end, which is where it lands in all but a proof
        // ending in a display, and is set once either way.
        parser.mouth.bind("\\qedhere", [](syntax::Mouth&) {});

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound theorem primitives");
    }

}
