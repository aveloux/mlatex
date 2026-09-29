/// @file
/// @brief Text set as it was typed: `\\verb`, `verbatim`, and the listings
///        package's `lstlisting`, `\\lstinline` and `\\lstinputlisting`.
///
/// The lexer has already read the text as it stands and handed it over as
/// one token; what is left is setting it, a line to a box.
#include "render/primitives/verbatim.hpp"
#include "render/primitives/styles.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include "syntax/argument.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace render::primitives {

    Verbatim::Verbatim(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\verb");
        // No document can write this name: a colon ends a control word.
        start = lexicon.intern("\\verbatim:start");
    }

    void Verbatim::operator()(syntax::Parser& parser, Context& context) const {
        // One of a listing's options: its own, as written between its
        // brackets, and failing that the one \lstset gave every listing. A
        // value in braces loses them, and a key written alone is `true`.
        const auto option = [&context](const std::string_view options, const std::string_view key) -> std::string {
            const auto trim = [](std::string_view text) {
                while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.remove_prefix(1);
                while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.remove_suffix(1);
                return text;
            };
            std::size_t depth = 0;
            std::size_t begin = 0;
            for (std::size_t at = 0; at <= options.size(); ++at) {
                if (at < options.size()) {
                    if (options[at] == '{') ++depth;
                    else if (options[at] == '}' && depth > 0) --depth;
                    if (options[at] != ',' || depth > 0) continue;
                }
                const std::string_view piece = trim(options.substr(begin, at - begin));
                begin = at + 1;
                const std::size_t equals = piece.find('=');
                if (trim(piece.substr(0, equals)) != key) continue;
                if (equals == std::string_view::npos) return "true";
                std::string_view value = trim(piece.substr(equals + 1));
                if (value.size() >= 2 && value.front() == '{' && value.back() == '}') {
                    value = value.substr(1, value.size() - 2);
                }
                return std::string(value);
            }
            const std::string* set = context.variables.get("lst." + std::string(key));
            return set ? *set : std::string{};
        };

        // A line as it stands, in the typewriter face at a size: no
        // ligatures, every space a character wide and never stretched, and
        // a tab as many spaces as reach the next stop.
        const auto typed = [&context](memory::Arena& arena, const std::string_view text, const float size,
                                      const std::size_t tabs) {
            std::string spread;
            spread.reserve(text.size());
            std::size_t column = 0;
            for (const char letter : text) {
                if (letter == '\r') continue;
                if (letter == '\t') {
                    do {
                        spread += ' ';
                        ++column;
                    } while (column % tabs != 0);
                    continue;
                }
                spread += letter;
                if ((static_cast<unsigned char>(letter) & 0xC0) != 0x80) ++column;
            }

            memory::Slice<layout::Node*> shaped{};
            if (const typography::Font* mono = Styles::resolve(context, Styles::Cut::Mono, size);
                mono && !spread.empty()) {
                const typography::Font* fonts[] = {mono};
                shaped = context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy(spread), {});
                for (layout::Node*& node : shaped) {
                    if (!node || node->type != layout::Node::Type::Glue) continue;
                    auto* kern = arena.compose<layout::Node>(layout::Node::Type::Kern);
                    kern->kern({.width = node->glue().width});
                    node = kern;
                }
            }
            return layout::Line::horizontal(arena, shaped, 0.0f);
        };

        const auto current = [&context] {
            return context.selection.text() ? context.selection.text()->size() : context.document.configuration.size;
        };

        // Inline: the text after \verb, \lstinline or \mintinline, one box.
        for (const std::string_view name : {"\\verb", "\\lstinline", "\\mintinline"}) {
            parser.bind(name, [this, typed, current, name](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth;
                const memory::Location origin = mouth.lookahead().location;
                const syntax::Token text = mouth.read();
                if (text.symbol != syntax::none || text.category != syntax::Catcodes::Category::Other) {
                    if (!text.empty()) mouth.stream().inject(std::span{&text, 1});
                    tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                             std::format("{} needs its text between two of one character", name));
                    return nullptr;
                }
                return directive(parser.arena, typed(parser.arena, text.text, current(), 8), origin);
            });
        }

        // The blocks: each leaves a mark, and the mark reads the body the
        // lexer made one token of.
        // minted's block is a listing too, its language read after its
        // options and let go: a listing here is not highlighted.
        for (const std::string_view name : {"verbatim", "verbatim*", "Verbatim", "lstlisting", "comment", "minted", "CCSXML"}) {
            context.blocks.watch(
                name,
                [this, name](syntax::Mouth& mouth) {
                    std::string options;
                    if (name == "lstlisting" || name == "Verbatim" || name == "minted") {
                        for (const syntax::Token& token :
                             mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                            options += token.text;
                        }
                    }
                    if (name == "minted") static_cast<void>(mouth.argument({}, 0));
                    const bool listing = name == "lstlisting" || name == "minted";
                    openings.push_back({listing ? "lstlisting" : name == "comment" || name == "CCSXML" ? "comment" : "verbatim",
                                        std::move(options)});
                    const syntax::Token mark{.symbol = start, .category = syntax::Catcodes::Category::Escape,
                                             .text = mouth.lexicon.resolve(start)};
                    mouth.stream().inject(std::span{&mark, 1});
                },
                {});
        }

        // LaTeX's filecontents: a file the document carries inside itself --
        // a `.bib` most often -- kept for the rest of the run, so the
        // \\bibliography or \\input that names it later reads it. Nothing is
        // set where it stands; the lexer made its body one token.
        for (const std::string_view name : {"filecontents", "filecontents*"}) {
            context.blocks.watch(
                name,
                [&context](syntax::Mouth& mouth) {
                    static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                    const std::string file = syntax::Argument::text(mouth);
                    const syntax::Token body = mouth.read();
                    if (body.symbol != syntax::none || body.category != syntax::Catcodes::Category::Other) {
                        if (!body.empty()) mouth.stream().inject(std::span{&body, 1});
                        return;
                    }
                    if (context.write && !file.empty()) context.write(file, body.text);
                },
                {});
        }

        // \lstinputlisting[options]{file}: a listing of a file handed in.
        parser.bind("\\lstinputlisting", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            const memory::Location origin = mouth.lookahead().location;
            std::string options;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                options += token.text;
            }
            const std::string name = syntax::Argument::text(mouth);
            const std::string* text = nullptr;
            if (context.files) {
                if (const auto found = context.files->find(name); found != context.files->end()) text = &found->second;
            }
            if (!text && context.disk) text = context.disk(name);
            if (!text) {
                tracebacks.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                         std::format("\\lstinputlisting: no file named '{}' was handed in", name));
                return nullptr;
            }
            openings.push_back({"lstlisting", std::move(options)});
            const std::array<syntax::Token, 2> marked{
                syntax::Token{.symbol = start, .category = syntax::Catcodes::Category::Escape,
                              .text = mouth.lexicon.resolve(start)},
                syntax::Token{.symbol = syntax::none, .category = syntax::Catcodes::Category::Other,
                              .location = origin, .text = *text},
            };
            mouth.stream().inject(std::span{marked});
            return nullptr;
        });

        parser.bind(start, [this, &context, option, typed, current](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;
            if (openings.empty()) return nullptr;

            syntax::Token body = mouth.read();
            if (body.symbol != syntax::none || body.category != syntax::Catcodes::Category::Other) {
                if (!body.empty()) mouth.stream().inject(std::span{&body, 1});
                body = {};
            }
            if (openings.back().kind == "comment") {
                openings.pop_back();
                return nullptr;
            }
            const bool listing = openings.back().kind == "lstlisting";
            const std::string options = openings.back().options;

            // A listing's caption heads it: read first, as a caption of its
            // own kind, with the mark and the body put back after it.
            if (listing && !openings.back().headed) {
                const std::string caption = option(options, "caption");
                const std::string label = option(options, "label");
                openings.back().headed = true;
                if (!caption.empty()) {
                    const syntax::Token mark{.symbol = start, .category = syntax::Catcodes::Category::Escape,
                                             .text = mouth.lexicon.resolve(start)};
                    if (!body.empty()) mouth.stream().inject(std::span{&body, 1});
                    mouth.stream().inject(std::span{&mark, 1});
                    mouth.ingest(arena.copy("\\captionof{lstlisting}{" + caption + "}" +
                                            (label.empty() ? std::string{} : "\\label{" + label + "}")));
                    return nullptr;
                }
            }
            openings.pop_back();

            // Its size, as its basic style says; LaTeX's own names for them.
            const float normal = context.document.configuration.size;
            float size = current();
            if (listing) {
                const std::string style = option(options, "basicstyle");
                static constexpr std::array<std::pair<std::string_view, Styles::Size>, 5> sizes{{
                    {"\\tiny", Styles::Size::Tiny}, {"\\scriptsize", Styles::Size::Script},
                    {"\\footnotesize", Styles::Size::Footnote}, {"\\small", Styles::Size::Small},
                    {"\\normalsize", Styles::Size::Normal},
                }};
                for (const auto& [written, step] : sizes) {
                    if (style.contains(written)) size = Styles::measure(normal, step);
                }
            }
            std::size_t tabs = 8;
            if (const std::string written = listing ? option(options, "tabsize") : std::string{}; !written.empty()) {
                std::from_chars(written.data(), written.data() + written.size(), tabs);
                if (tabs == 0) tabs = 8;
            }
            const bool numbered = listing && option(options, "numbers") == "left";
            const std::string frame = listing ? option(options, "frame") : std::string{};
            const bool ruled = !frame.empty() && frame != "none";

            std::vector<syntax::Node*> parts;
            const auto block = [&](layout::Node* node) { parts.push_back(directive(arena, node, origin, true)); };
            const auto space = [&](const float amount) {
                auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
                glue->glue({.width = amount, .stretch = amount / 3.0f, .shrink = amount / 3.0f});
                block(glue);
            };
            const auto rule = [&] {
                auto* bar = arena.compose<layout::Node>(layout::Node::Type::Rule);
                const float width = static_cast<float>(context.registers.get(
                                        syntax::semantics::Registers::Type::Dimension,
                                        syntax::semantics::Registers::reserved + 1)) / 65536.0f;
                bar->rule({.width = width, .height = 0.4f});
                block(bar);
            };

            parts.push_back(arena.compose<syntax::Node>(syntax::Node::Type::Paragraph, std::string_view{}, origin));
            space(listing ? 4.0f : 6.0f);
            if (ruled) {
                rule();
                space(3.0f);
            }

            // Each line its own box, and in a listing numbered in the margin
            // beside it: small upright numerals, each ending ten points clear
            // of the code, so a column of them is flush right.
            std::vector<std::string_view> lines;
            for (std::string_view rest = body.text; !body.text.empty();) {
                const std::size_t end = rest.find('\n');
                lines.push_back(rest.substr(0, end));
                if (end == std::string_view::npos) break;
                rest.remove_prefix(end + 1);
            }

            const typography::Font* small = numbered
                ? Styles::resolve(context, Styles::Cut::Normal, Styles::measure(normal, Styles::Size::Tiny))
                : nullptr;
            for (std::size_t index = 0; index < lines.size(); ++index) {
                layout::Node* code = typed(arena, lines[index], size, tabs);
                if (small) {
                    constexpr float separation = 10.0f;
                    const typography::Font* fonts[] = {small};
                    layout::Node* number = layout::Line::horizontal(
                        arena, context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy(std::to_string(index + 1)), {}),
                        0.0f);
                    const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(4);
                    row[0] = arena.compose<layout::Node>(layout::Node::Type::Kern);
                    row[0]->kern({.width = -(separation + number->box().width)});
                    row[1] = number;
                    row[2] = arena.compose<layout::Node>(layout::Node::Type::Kern);
                    row[2]->kern({.width = separation});
                    row[3] = code;
                    code = layout::Line::horizontal(arena, row, 0.0f);
                }
                block(code);
            }

            if (ruled) {
                space(3.0f);
                rule();
            }
            space(listing ? 4.0f : 6.0f);

            const memory::Slice<syntax::Node*> nodes = arena.allocate<syntax::Node*>(parts.size());
            std::ranges::copy(parts, nodes.begin());
            return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, nodes);
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound verbatim primitives");
    }

}
