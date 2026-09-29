/// @file
/// @brief List primitives: `\\item`, LaTeX's own list, and the three made from it.
///
/// The environments are hooks on the block module rather than primitives of
/// their own, so `\\begin{itemize}` is the same `\\begin` every other block
/// uses and this file only says what happens when that name is the one opened.
/// An item is built from instructions any document could write for itself --
/// a new paragraph, a margin, no indentation, a label -- so nothing about a
/// list is special to the layout engine.
#include "render/primitives/lists.hpp"
#include "render/primitives/numeral.hpp"
#include "render/primitives/styles.hpp"
#include "syntax/semantics/scope.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace render::primitives {

    /// @brief `\\topsep` and `\\itemsep` plus `\\parsep` for a ten-point body,
    ///        by how deep the list is: LaTeX's own, smaller at each level.
    static constexpr std::array<std::pair<float, float>, 4> spacings{{
        {8.0f, 8.0f}, {4.0f, 4.0f}, {2.0f, 2.0f}, {2.0f, 2.0f},
    }};

    Lists::Lists(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\item");
    }

    void Lists::operator()(syntax::Parser& parser, Context& context) const {
        // LaTeX's three, enumitem's starred inline forms, paralist's compact
        // and paragraph-set ones, threeparttable's notes and the acronym
        // package's list -- each set as the one of LaTeX's it is a form of.
        static constexpr std::array<std::pair<std::string_view, Marker>, 17> environments{{
            {"itemize", Marker::Bullet},
            {"enumerate", Marker::Number},
            {"description", Marker::Description},
            {"itemize*", Marker::Bullet},
            {"enumerate*", Marker::Number},
            {"description*", Marker::Description},
            {"compactitem", Marker::Bullet},
            {"compactenum", Marker::Number},
            {"compactdesc", Marker::Description},
            {"asparaitem", Marker::Bullet},
            {"asparaenum", Marker::Number},
            {"asparadesc", Marker::Description},
            {"inparaitem", Marker::Bullet},
            {"inparaenum", Marker::Number},
            {"inparadesc", Marker::Description},
            {"tablenotes", Marker::Description},
            {"acronym", Marker::Description},
        }};

        // The space around a list and between its items, at this depth, for
        // the class's body size.
        const auto measure = [this, &context](const bool between) {
            const std::size_t depth = levels.empty() ? 0 : std::min(levels.size() - 1, spacings.size() - 1);
            const Level& level = levels.back();
            const float own = between ? level.spacing : level.around;
            if (own >= 0.0f) return own;
            const float scale = context.document.configuration().size / 10.0f;
            return (between ? spacings[depth].second : spacings[depth].first) * scale;
        };

        // A list closing: the last item's paragraph ends here, and what
        // follows goes back to the margin of the list around this one --
        // unindented, because it carries on from the list rather than
        // starting afresh.
        const auto leaving = [this, &context, measure](syntax::Mouth& mouth) {
            const float around = levels.empty() ? 0.0f : measure(false);
            if (!levels.empty()) levels.pop_back();

            const typography::Font* font = context.selection.text();
            const float em = font ? font->size() : 10.0f;
            const float margin = em * indent * static_cast<float>(levels.size());
            mouth.ingest(context.arena.copy(
                std::format("\\par\\leftskip={:.2f}pt{}\\noindent ", margin,
                            around > 0.0f ? std::format("\\vskip {:.2f}pt plus 2pt minus 2pt", around)
                                          : std::string{})));
        };

        // enumitem's \setlist[itemize]{noitemsep}, and with no kind named,
        // for every list: kept to be read before each list's own options.
        // A level after the kind, `[itemize,1]`, is read as the kind's.
        parser.mouth().bind("\\setlist", [this](syntax::Mouth& mouth) {
            if (mouth.lookahead().is('*')) mouth.read();
            std::string kinds;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                kinds += token.text;
            }
            std::string options;
            for (const syntax::Token& token : mouth.argument({}, 0)) options += token.text;
            bool named = false;
            for (const auto piece : std::views::split(std::string_view(kinds), ',')) {
                std::string kind(piece.begin(), piece.end());
                std::erase(kind, ' ');
                if (kind.empty() || (kind[0] >= '0' && kind[0] <= '9')) continue;
                defaults[kind] += "," + options;
                named = true;
            }
            if (!named) defaults[""] += "," + options;
        });

        for (const auto& [name, marker] : environments) {
            context.blocks.watch(
                name,
                [this, &context, marker, measure, name](syntax::Mouth& mouth) {
                    Level level{.marker = marker};
                    std::string options = defaults[""] + "," + defaults[std::string(name)] + ",";
                    for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                        options += token.text;
                    }
                    // The options, split at the commas outside braces so a
                    // label may hold one, each read as enumitem reads it.
                    {
                    const auto trim = [](std::string_view text) {
                        while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.remove_prefix(1);
                        while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.remove_suffix(1);
                        if (text.size() >= 2 && text.front() == '{' && text.back() == '}') text = text.substr(1, text.size() - 2);
                        return text;
                    };

                    // Split at the commas outside braces, so a label may hold one.
                    std::vector<std::string_view> items;
                    int depth = 0;
                    std::size_t start = 0;
                    for (std::size_t index = 0; index <= options.size(); ++index) {
                        if (index == options.size() || (options[index] == ',' && depth == 0)) {
                            items.push_back(trim(std::string_view(options).substr(start, index - start)));
                            start = index + 1;
                        } else if (options[index] == '{') {
                            ++depth;
                        } else if (options[index] == '}') {
                            --depth;
                        }
                    }

                    for (const std::string_view item : items) {
                        if (item.empty()) continue;
                        const std::size_t equals = item.find('=');
                        const std::string_view key = trim(item.substr(0, equals));
                        const std::string_view value = equals == std::string_view::npos ? std::string_view{}
                                                                                         : trim(item.substr(equals + 1));

                        if (key == "label" || key == "label*") {
                            level.label = std::string(value);
                        } else if (key == "start") {
                            int first = 1;
                            std::from_chars(value.data(), value.data() + value.size(), first);
                            level.counter = first - 1;
                        } else if (key == "noitemsep") {
                            level.spacing = 0.0f;
                        } else if (key == "nosep" || key == "nolistsep") {
                            level.spacing = 0.0f;
                            level.around = 0.0f;
                        } else if (key == "itemsep" || key == "topsep") {
                            (key == "itemsep" ? level.spacing : level.around) =
                                std::max(primitives::measure(std::string(value), mouth, context), 0.0f);
                        } else if (equals == std::string_view::npos && key.find_first_of("aAiI1") != std::string_view::npos &&
                                   key.size() <= 6 && key.find('\\') == std::string_view::npos) {
                            // enumitem's short labels: the first a, A, i, I or 1 stands
                            // for the number in that form -- `(a)`, `i.`, `1)`.
                            const std::size_t at = key.find_first_of("aAiI1");
                            static constexpr std::array<std::pair<char, std::string_view>, 5> forms{{
                                {'a', "\\alph*"}, {'A', "\\Alph*"}, {'i', "\\roman*"}, {'I', "\\Roman*"}, {'1', "\\arabic*"},
                            }};
                            std::string label(key.substr(0, at));
                            for (const auto& [letter, command] : forms) {
                                if (key[at] == letter) label += command;
                            }
                            label += key.substr(at + 1);
                            level.label = std::move(label);
                        }
                        // Anything else enumitem takes -- leftmargin, align, wide --
                        // changes nothing a document set here would see.
                    }
                    }
                    levels.push_back(std::move(level));

                    const float around = measure(false);
                    if (around > 0.0f) {
                        mouth.ingest(context.arena.copy(std::format("\\par\\vskip {:.2f}pt plus 2pt minus 2pt ", around)));
                    }
                },
                leaving);
        }

        // LaTeX's own list, which the others are made from:
        // `\\begin{list}{--}{\\setlength{\\leftmargin}{2em}}`. Its items carry
        // the label given, and its settings are read as the block opens, as
        // LaTeX reads them. One that counts its items with \\usecounter is
        // numbered, the count written where its label shows the counter.
        context.blocks.watch(
            "list",
            [this, &context, measure](syntax::Mouth& mouth) {
                // Written back out as text, a name kept apart from what
                // follows it, to be read again.
                const auto written = [&mouth] {
                    std::string text;
                    for (const syntax::Token& token : mouth.argument({}, 1)) {
                        text += token.text;
                        if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
                    }
                    return text;
                };
                Level level{.marker = Marker::Bullet, .label = written()};
                std::string settings = written();

                if (const std::size_t at = settings.find("\\usecounter "); at != std::string::npos) {
                    const std::size_t open = settings.find('{', at);
                    const std::size_t close = settings.find('}', at);
                    if (open != std::string::npos && close != std::string::npos && open < close) {
                        const std::string counter = settings.substr(open + 1, close - open - 1);
                        settings.erase(at, close + 1 - at);
                        level.marker = Marker::Number;
                        static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> forms{{
                            {"\\arabic ", "\\arabic*"}, {"\\alph ", "\\alph*"}, {"\\Alph ", "\\Alph*"},
                            {"\\roman ", "\\roman*"}, {"\\Roman ", "\\Roman*"},
                        }};
                        std::vector<std::pair<std::string, std::string_view>> spellings{
                            {"\\the" + counter + " ", "\\arabic*"}};
                        for (const auto& [form, star] : forms) spellings.emplace_back(std::string(form) + "{" + counter + "}", star);
                        for (const auto& [spelled, star] : spellings) {
                            for (std::size_t found = level.label.find(spelled); found != std::string::npos;
                                 found = level.label.find(spelled, found + star.size())) {
                                level.label.replace(found, spelled.size(), star);
                            }
                        }
                    }
                }
                levels.push_back(std::move(level));

                const float around = measure(false);
                mouth.ingest(context.arena.copy(
                    settings + (around > 0.0f ? std::format("\\par\\vskip {:.2f}pt plus 2pt minus 2pt ", around)
                                              : std::string{})));
            },
            leaving);

        parser.bind("\\item", [this, &context, measure](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            if (levels.empty()) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Environment, origin,
                                         "\\item outside any list");
                // Carried on with a bulleted level so that the rest of the
                // document still sets, as TeX would.
                levels.push_back(Level{});
            }

            Level& level = levels.back();
            const bool first = !std::exchange(level.begun, true);

            // Blanks after `\\item`, and after a description's term, are the
            // source's layout rather than a word space.
            const auto skip = [&mouth] {
                syntax::Token next = mouth.read();
                while (next.category == syntax::CatCodes::Category::Space) next = mouth.read();
                if (!next.empty()) mouth.stream().inject(std::span{&next, 1});
            };
            skip();

            // How deep this list is among lists of its own kind, which is what
            // picks the label: LaTeX's second bullet is a dash and its second
            // number a letter.
            std::size_t depth = 0;
            for (const Level& open : levels) {
                if (open.marker == level.marker) ++depth;
            }

            // beamer's overlay, `\item<2->`, read and let go: every slide of
            // a frame is the one page here.
            if (mouth.lookahead().is('<')) {
                while (!mouth.lookahead().empty() && !mouth.read().is('>')) {}
                skip();
            }

            // An item's own label, `\item[$\star$]`, in any list.
            std::string label;
            bool written = false;
            if (mouth.lookahead().is('[')) {
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    label += token.text;
                    if (token.text.size() > 1 && token.text.front() == '\\') label += ' ';
                }
                written = true;
                skip();
            }

            const bool counted = level.marker == Marker::Number && !written;
            const int value = counted || (!written && level.marker != Marker::Description) ? ++level.counter
                                                                                           : level.counter;

            if (!written && !level.label.empty()) {
                // The label with its counter written in: `\\arabic*` and the
                // rest replaced by the value in that form.
                static constexpr std::array<std::string_view, 5> commands{
                    "\\arabic*", "\\alph*", "\\Alph*", "\\roman*", "\\Roman*",
                };
                label = level.label;
                for (std::size_t index = 0; index < commands.size(); ++index) {
                    const std::string_view command = commands[index];
                    const std::string form = index == 0   ? Numeral::arabic(value)
                                             : index == 1 ? Numeral::alphabetic(value, false)
                                             : index == 2 ? Numeral::alphabetic(value, true)
                                             : index == 3 ? Numeral::roman(value, false)
                                                          : Numeral::roman(value, true);
                    for (std::size_t at = label.find(command); at != std::string::npos;
                         at = label.find(command, at + form.size())) {
                        label.replace(at, command.size(), form);
                    }
                }
                if (level.marker == Marker::Number) {
                    context.anchor = label;
                    context.kind = "item";
                }
            } else if (!written) {
                switch (level.marker) {
                    case Marker::Description:
                        break;
                    case Marker::Number: {
                        std::string number;
                        switch ((depth - 1) % 4) {
                            case 0:
                                number = Numeral::arabic(value);
                                label = number + ".";
                                break;
                            case 1:
                                number = Numeral::alphabetic(value, false);
                                label = "(" + number + ")";
                                break;
                            case 2:
                                number = Numeral::roman(value, false);
                                label = number + ".";
                                break;
                            default:
                                number = Numeral::alphabetic(value, true);
                                label = number + ".";
                                break;
                        }
                        // What a `\\label` after this item refers to.
                        context.anchor = number;
                        context.kind = "item";
                        break;
                    }
                    case Marker::Bullet: {
                        // LaTeX's, unless the document or its class set its
                        // own: `\renewcommand{\labelitemi}{--}`.
                        static constexpr std::array<std::string_view, 4> bullets{"•", "–", "*", "·"};
                        static constexpr std::array<std::string_view, 4> named{"\\labelitemi", "\\labelitemii",
                                                                               "\\labelitemiii", "\\labelitemiv"};
                        const std::size_t at = (depth - 1) % bullets.size();
                        label = mouth.macro(mouth.lexicon().intern(named[at])) ? named[at] : bullets[at];
                        break;
                    }
                }
            }

            const typography::Font* font = context.selection.text();
            const float em = font ? font->size() : 10.0f;
            const float margin = em * indent * static_cast<float>(levels.size());
            const float gap = em * separation;

            // The label is read as text is, so it may hold a formula or a
            // style -- `$\star$`, `\textbf{Step 1:}` -- set bold in a
            // description, as its term is.
            layout::Node* mark = nullptr;
            if (font && !label.empty()) {
                const typography::Font* restore = context.selection.text();
                if (level.marker == Marker::Description) {
                    context.selection.text(Styles::resolve(context, Styles::Cut::Bold, font->size()));
                }
                mouth.ingest(arena.copy("{" + label + "}"));
                mouth.read();
                mouth.push(syntax::semantics::Scope::Type::Group);
                const memory::Slice<syntax::Node*> read = parser.parse('}');
                mouth.pop(syntax::semantics::Scope::Type::Group);
                stamp(read, context);

                std::vector<layout::Node*> nodes;
                for (const syntax::Node* child : read) gather(nodes, child, context);
                context.selection.text(restore);
                const memory::Slice<layout::Node*> shaped = arena.allocate<layout::Node*>(nodes.size());
                std::ranges::copy(nodes, shaped.begin());
                mark = layout::Line::horizontal(arena, shaped, 0.0f);
            }
            const float width = mark ? mark->box().width : 0.0f;

            // The label hangs to the left of the text. A bullet or a number
            // sits right-aligned in the margin, so the line backs up past it
            // and the gap; a description's term starts at the margin of the
            // list around this one and pushes its first line along instead.
            // Right to left, the line's first piece is drawn at its right, and
            // the label hangs from there to the right, the same parts the
            // other way round.
            auto* back = arena.compose<layout::Node>(layout::Node::Type::Kern);
            back->kern({.width = level.marker == Marker::Description ? -em * indent
                                                                     : -(width + gap)});
            auto* after = arena.compose<layout::Node>(layout::Node::Type::Kern);
            after->kern({.width = gap});

            const memory::Slice<layout::Node*> contents = arena.allocate<layout::Node*>(mark ? 3 : 2);
            std::size_t filled = 0;
            contents[filled++] = context.reversed ? after : back;
            if (mark) contents[filled++] = mark;
            contents[filled++] = context.reversed ? back : after;

            auto* shift = arena.compose<layout::Node>(layout::Node::Type::Directive);
            shift->directive({.command = layout::Node::Directive::Command::Margin, .width = margin});

            auto* flush = arena.compose<layout::Node>(layout::Node::Type::Directive);
            flush->directive({.command = layout::Node::Directive::Command::Flush});

            // The break that ends the item before, the space between two
            // items, the margin and the missing indentation of this one, and
            // its label. Its text is whatever follows, up to the next item or
            // the end of the list.
            const float between = first ? 0.0f : measure(true);
            const memory::Slice<syntax::Node*> item = arena.allocate<syntax::Node*>(5);
            std::size_t placed = 0;
            item[placed++] = arena.compose<syntax::Node>(syntax::Node::Type::Paragraph, std::string_view{}, origin);
            if (between > 0.0f) {
                auto* space = arena.compose<layout::Node>(layout::Node::Type::Glue);
                space->glue({.width = between, .stretch = between * 0.25f, .shrink = between * 0.125f});
                item[placed++] = directive(arena, space, origin, true);
            }
            item[placed++] = directive(arena, shift, origin);
            item[placed++] = directive(arena, flush, origin);
            item[placed++] = directive(arena, layout::Line::horizontal(arena, contents, 0.0f), origin);

            return arena.compose<syntax::Node>(
                syntax::Node::Type::Group, std::string_view{}, origin, memory::Slice{item.data, placed});
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound list primitives");
    }

}
