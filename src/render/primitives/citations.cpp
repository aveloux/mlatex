/// @file
/// @brief Citation primitives: `\\cite` and natbib's and biblatex's forms of
///        it, `\\bibitem`, `thebibliography`, and a `.bib` file read and set
///        as BibTeX sets one.
///
/// A citation list is a numbered list under a different name: `\\bibitem`
/// builds exactly the margin, label and hanging indent `\\item` does, and
/// `\\cite` reaches the number the same way `\\ref` reaches a label's, with
/// its own wait-and-fill map so the two numberings never share a counter.
#include "render/primitives/citations.hpp"
#include "render/primitives/references.hpp"
#include "syntax/argument.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace render::primitives {

    Citations::Citations(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\cite");
        lexicon.intern("\\bibitem");
        lexicon.intern("\\bibliography");
    }

    std::string Citations::print(const Citation& citation, const Context& context) const {
        // natbib's forms name authors and years when natbib is in use without
        // its `numbers` option and every entry cited has both; otherwise every
        // form falls back to the entries' numbers or labels.
        std::vector<const Mark*> found;
        found.reserve(citation.keys.size());
        const std::string* style = context.variables.get("biblatex.style");
        bool yeared = context.variables.get("cite.natbib") && !context.variables.get("natbib.numbers") &&
                      (!style || style->starts_with("authoryear") || style->starts_with("apa"));
        for (const std::string& key : citation.keys) {
            const auto mark = marks.find(key);
            found.push_back(mark != marks.end() && mark->second.defined ? &mark->second : nullptr);
            yeared = yeared && found.back() && !found.back()->author.empty() && !found.back()->year.empty();
        }
        const auto text = [](const Mark* mark) { return mark ? mark->text : std::string("?"); };
        const auto join = [&](const auto& piece, const std::string_view between) {
            std::string joined;
            for (std::size_t index = 0; index < found.size(); ++index) {
                if (index > 0) joined += between;
                joined += piece(found[index]);
            }
            return joined;
        };
        const std::string before = citation.before.empty() ? std::string{} : citation.before + " ";
        const std::string after = citation.note.empty() ? std::string{} : ", " + citation.note;
        const auto numbered = [&] { return "[" + before + join(text, ", ") + after + "]"; };

        switch (citation.form) {
            case Form::Number: return join(text, ", ");
            case Form::Author: return join([&](const Mark* mark) { return mark && !mark->author.empty() ? mark->author : text(mark); }, ", ");
            case Form::Year: return join([&](const Mark* mark) { return mark && !mark->year.empty() ? mark->year : text(mark); }, ", ");
            case Form::Loose:
                return yeared ? before + join([](const Mark* mark) { return mark->author + " " + mark->year; }, "; ") + after
                              : before + join(text, ", ") + after;
            case Form::Comma:
                return yeared ? before + join([](const Mark* mark) { return mark->author + ", " + mark->year; }, "; ") + after
                              : before + join(text, ", ") + after;
            case Form::Parenthetical:
                return yeared ? "(" + before + join([](const Mark* mark) { return mark->author + ", " + mark->year; }, "; ") +
                                    after + ")"
                              : numbered();
            case Form::Plain:
                if (!yeared) return numbered();
                [[fallthrough]];
            case Form::Textual: {
                if (yeared) {
                    // `Knuth (1984)`, the note inside the last parentheses.
                    std::string written;
                    for (std::size_t index = 0; index < found.size(); ++index) {
                        if (index > 0) written += "; ";
                        written += found[index]->author + " (" + (index == 0 ? before : "") + found[index]->year +
                                   (index + 1 == found.size() ? after : "") + ")";
                    }
                    return written;
                }
                // `Knuth [1]` where an author is known, `[1]` where not.
                std::string written;
                for (std::size_t index = 0; index < found.size(); ++index) {
                    if (index > 0) written += "; ";
                    const Mark* mark = found[index];
                    if (mark && !mark->author.empty()) written += mark->author + " ";
                    written += "[" + text(mark) + (index + 1 == found.size() ? after : "") + "]";
                }
                return written;
            }
        }
        return numbered();
    }

    void Citations::operator()(syntax::Parser& parser, Context& context) const {
        // LaTeX's article class sets the list under an unnumbered heading,
        // `\\refname`, and makes its margin the width of the label in the
        // list's argument -- `{9}` for a list of fewer than ten, `{99}` for
        // fewer than a hundred -- plus `\\labelsep`.
        context.blocks.watch(
            "thebibliography",
            [this, &context](syntax::Mouth& mouth) {
                std::string sample = "[";
                for (const syntax::Token& token : mouth.argument({}, 0)) sample += token.text;
                sample += "]";

                widest = 0.0f;
                if (const typography::Font* font = context.selection.text()) {
                    const typography::Font* fonts[] = {font};
                    const memory::Slice<layout::Node*> shaped =
                        context.shaper.shape(memory::Slice{fonts, 1uz}, context.arena.copy(sample), {});
                    widest = layout::Line::horizontal(context.arena, shaped, 0.0f)->box().width;
                }
                // Under the class's heading -- `\section*{\refname}` in an
                // article, `\chapter*{\bibname}` in a report or a book -- or
                // the one \printbibliography asked for; then natbib's
                // \\bibfont, which a class sets its references in: nothing in
                // the article class, \\footnotesize in IEEE's.
                const std::string head = heading.value_or("\\@bibheading");
                heading.reset();
                mouth.ingest(context.arena.copy(head + "\\bibfont "));
            },
            [&context](syntax::Mouth& mouth) {
                // The last entry ends here, and what follows is back at the
                // column's own margin.
                mouth.ingest(context.arena.copy(std::string("\\par\\leftskip=0pt\\noindent ")));
            });

        parser.bind("\\bibitem", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            // `\\bibitem[Knu84]{knuth}`: a label of the document's own, which
            // the citations print instead of the entry's number; natbib's
            // `\\bibitem[Knuth(1984)]{knuth}`, an author and a year.
            std::string given;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                given += token.text == "~" ? std::string_view{" "} : token.text;
            }

            const std::string key = syntax::Argument::text(mouth);
            if (key.empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "\\bibitem needs a key");
                return nullptr;
            }

            Mark& mark = marks[key];
            if (mark.defined) {
                tracebacks.emplace_back(syntax::Traceback::Type::Warning, origin,
                                         "Citation `" + key + "' multiply defined");
                return nullptr;
            }

            const bool first = entries == 0;
            ++entries;
            const std::size_t open = given.find('(');
            const std::size_t close = given.find(')', open == std::string::npos ? 0 : open);
            if (open != std::string::npos && close != std::string::npos && open > 0) {
                mark.author = given.substr(0, open);
                while (!mark.author.empty() && mark.author.back() == ' ') mark.author.pop_back();
                mark.year = given.substr(open + 1, close - open - 1);
                mark.text = std::to_string(entries);
            } else {
                mark.text = given.empty() ? std::to_string(entries) : given;
            }
            mark.defined = true;
            mark.anchor = context.anchors++;

            // Every citation that named this key, set again now it has one,
            // and every link to it pointed at where it stands.
            for (const std::size_t index : mark.references) {
                citations[index].node->value = arena.copy(print(citations[index], context));
            }
            mark.references.clear();
            for (layout::Node* link : mark.links) {
                layout::Node::Directive order = link->directive();
                order.index = mark.anchor;
                link->directive(order);
            }
            mark.links.clear();

            // Blanks right after the key are the source's own layout.
            syntax::Token next = mouth.read();
            while (next.category == syntax::Catcodes::Category::Space) next = mouth.read();
            if (!next.empty()) mouth.stream().inject(std::span{&next, 1});

            const typography::Font* font = context.selection.text();
            const float em = font ? font->size() : 10.0f;
            const float gap = em * separation;

            // natbib's author-year list sets no label: each entry hangs, its
            // first line an em left of the rest.
            const std::string* style = context.variables.get("biblatex.style");
            hung = !mark.author.empty() && context.variables.get("cite.natbib") &&
                   !context.variables.get("natbib.numbers") &&
                   (!style || style->starts_with("authoryear") || style->starts_with("apa"));
            layout::Node* label = nullptr;
            if (font && !hung) {
                const typography::Font* fonts[] = {font};
                const memory::Slice<layout::Node*> shaped =
                    context.shaper.shape(memory::Slice{fonts, 1uz}, arena.copy("[" + mark.text + "]"), {});
                label = layout::Line::horizontal(arena, shaped, 0.0f);
            }
            const float width = label ? label->box().width : 0.0f;
            const float margin = hung ? em : std::max(widest, width) + gap;

            // The label hangs to the left of the reference text, right
            // aligned in the margin, exactly as an enumerated item's does.
            auto* back = arena.compose<layout::Node>(layout::Node::Type::Kern);
            back->kern({.width = hung ? -em : -(width + gap)});
            auto* after = arena.compose<layout::Node>(layout::Node::Type::Kern);
            after->kern({.width = hung ? 0.0f : gap});

            const memory::Slice<layout::Node*> contents = arena.allocate<layout::Node*>(label ? 3 : 2);
            std::size_t filled = 0;
            contents[filled++] = back;
            if (label) contents[filled++] = label;
            contents[filled++] = after;

            auto* shift = arena.compose<layout::Node>(layout::Node::Type::Directive);
            shift->directive({.command = layout::Node::Directive::Command::Margin, .width = margin});

            auto* flush = arena.compose<layout::Node>(layout::Node::Type::Directive);
            flush->directive({.command = layout::Node::Directive::Command::Flush});

            // The break that ends the entry before, the space between the
            // two, the margin and missing indentation of this one, and its
            // label. Its text is whatever follows, up to the next \bibitem
            // or the end of the list.
            const memory::Slice<syntax::Node*> item = arena.allocate<syntax::Node*>(6);
            std::size_t placed = 0;
            item[placed++] = arena.compose<syntax::Node>(
                syntax::Node::Type::Paragraph, std::string_view{}, origin);
            auto* anchor = arena.compose<layout::Node>(layout::Node::Type::Directive);
            anchor->directive({.command = layout::Node::Directive::Command::Anchor, .index = mark.anchor});
            item[placed++] = directive(arena, anchor, origin);
            if (!first) {
                const float space = between * em / 10.0f;
                auto* skip = arena.compose<layout::Node>(layout::Node::Type::Glue);
                skip->glue({.width = space, .stretch = space * 0.25f, .shrink = space * 0.125f});
                item[placed++] = directive(arena, skip, origin, true);
            }
            item[placed++] = directive(arena, shift, origin);
            item[placed++] = directive(arena, flush, origin);
            item[placed++] = directive(arena, layout::Line::horizontal(arena, contents, 0.0f), origin);

            return arena.compose<syntax::Node>(
                syntax::Node::Type::Group, std::string_view{}, origin, memory::Slice{item.data, placed});
        });

        // The citations, in every spelling the three packages give them. Each
        // takes a star, which asks natbib for every author's name and here
        // changes nothing, and up to two notes: `\\citep[see][p.~5]{knuth}`
        // is (see Knuth, 1984, p. 5), and with one note it is the second.
        // The keys are read as LaTeX reads them, split at the commas with the
        // spaces around each dropped.
        static constexpr std::array<std::pair<std::string_view, Form>, 20> forms{{
            {"\\cite", Form::Plain},          {"\\citep", Form::Parenthetical}, {"\\citet", Form::Textual},
            {"\\Citep", Form::Parenthetical}, {"\\Citet", Form::Textual},       {"\\citeauthor", Form::Author},
            {"\\Citeauthor", Form::Author},   {"\\citeyear", Form::Year},       {"\\citealt", Form::Loose},
            {"\\citealp", Form::Comma},       {"\\citenum", Form::Number},      {"\\parencite", Form::Parenthetical},
            {"\\Parencite", Form::Parenthetical}, {"\\textcite", Form::Textual}, {"\\Textcite", Form::Textual},
            {"\\autocite", Form::Parenthetical}, {"\\Autocite", Form::Parenthetical}, {"\\smartcite", Form::Parenthetical},
            {"\\supercite", Form::Number},    {"\\citeyearpar", Form::Year},
        }};
        for (const auto& [name, form] : forms) {
            parser.bind(name, [this, &context, form, name](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth;
                memory::Arena& arena = parser.arena;
                const memory::Location origin = mouth.lookahead().location;

                if (mouth.lookahead().is('*')) mouth.read();
                Citation citation{.form = form};
                std::array<std::string, 2> notes{};
                std::size_t given = 0;
                while (given < notes.size() && mouth.lookahead().is('[')) {
                    for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                        const bool blank = token.category == syntax::Catcodes::Category::Space || token.text == "~";
                        notes[given] += blank ? std::string_view{" "} : token.text;
                    }
                    ++given;
                }
                citation.before = given == 2 ? notes[0] : std::string{};
                citation.note = given == 2 ? notes[1] : notes[0];

                const std::string written = syntax::Argument::text(mouth);
                for (const auto piece : std::views::split(written, ',')) {
                    std::string_view key(piece.begin(), piece.end());
                    while (!key.empty() && key.front() == ' ') key.remove_prefix(1);
                    while (!key.empty() && key.back() == ' ') key.remove_suffix(1);
                    if (!key.empty()) citation.keys.emplace_back(key);
                }
                if (citation.keys.empty()) {
                    tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                             std::format("{} needs a key", name));
                }

                std::string text = print(citation, context);
                if (name == "\\citeyearpar") text = "(" + text + ")";
                citation.node = arena.compose<syntax::Node>(syntax::Node::Type::Text, arena.copy(text), origin);

                // A key not yet in the list is filled in once its \bibitem is read.
                const std::size_t index = citations.size();
                for (const std::string& key : citation.keys) {
                    Mark& mark = marks[key];
                    if (!mark.defined) mark.references.push_back(index);
                }
                syntax::Node* node = citation.node;
                // With hyperref, a link to the first entry it names.
                References::Hyperlink link{};
                if (!citation.keys.empty()) {
                    Mark& first = marks[citation.keys.front()];
                    link = References::hyperlink(context, {}, first.anchor, "cite");
                    if (link.open && !first.defined) first.links.push_back(link.open);
                }
                citations.push_back(std::move(citation));
                return References::wrapped(arena, node, link, origin);
            });
        }

        // \nocite: keys listed without being cited, or `*` for every entry of
        // every file; biblatex's \addbibresource, a file its list is read from.
        parser.mouth.bind("\\nocite", [this](syntax::Mouth& mouth) {
            const std::string written = syntax::Argument::text(mouth);
            for (const auto piece : std::views::split(written, ',')) {
                std::string_view key(piece.begin(), piece.end());
                while (!key.empty() && key.front() == ' ') key.remove_prefix(1);
                while (!key.empty() && key.back() == ' ') key.remove_suffix(1);
                if (!key.empty()) extra.emplace_back(key);
            }
        });
        parser.mouth.bind("\\addbibresource", [this](syntax::Mouth& mouth) {
            static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
            resources.push_back(syntax::Argument::text(mouth));
        });

        // BibTeX's work: the files read, the entries cited so far chosen,
        // sorted and set in the style named, and the whole written out as the
        // `thebibliography` block a document could have written itself.
        const auto compile = [this, &context](syntax::Mouth& mouth, const std::vector<std::string>& files) {
            const memory::Location origin = mouth.lookahead().location;

            /// One entry of a `.bib` file: its type, its key, its fields.
            struct Entry {
                std::string type{};
                std::string key{};
                memory::Dictionary<std::string> fields{};
                std::string label{};    // the style's label for it: a number, Knu84, or Knuth(1984)
                std::string order{};    // what it sorts by
                std::size_t cited{0};   // where it was first cited, for the styles that keep that order
            };
            std::vector<Entry> read;
            std::string preamble;   // every @preamble's text, in the order read
            memory::Dictionary<std::string> strings{
                {"jan", "January"}, {"feb", "February"}, {"mar", "March"}, {"apr", "April"}, {"may", "May"},
                {"jun", "June"}, {"jul", "July"}, {"aug", "August"}, {"sep", "September"}, {"oct", "October"},
                {"nov", "November"}, {"dec", "December"},
            };

            for (const std::string& file : files) {
                const std::string name = file.ends_with(".bib") ? file : file + ".bib";
                const std::string* text = nullptr;
                if (context.files) {
                    if (const auto found = context.files->find(name); found != context.files->end()) text = &found->second;
                }
                if (!text && context.disk) text = context.disk(name);
                if (!text) {
                    tracebacks.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                             std::format("No bibliography file named '{}' could be read", name));
                    continue;
                }

                // The file as BibTeX reads it: `@type{key, field = value, ...}`,
                // a value braced, quoted, a number or a @string's name, joined
                // with #; @string defines a name, @preamble's text goes ahead
                // of the list as BibTeX writes it into the .bbl, @comment is
                // read past, and anything outside an entry is a comment.
                const std::string_view source = *text;
                std::size_t at = 0;
                const auto blanks = [&] {
                    while (at < source.size() && std::isspace(static_cast<unsigned char>(source[at]))) ++at;
                };
                const auto word = [&] {
                    blanks();
                    const std::size_t begin = at;
                    while (at < source.size() && (std::isalnum(static_cast<unsigned char>(source[at])) ||
                                                  std::string_view("_-:.+/'").contains(source[at]))) {
                        ++at;
                    }
                    std::string found(source.substr(begin, at - begin));
                    for (char& letter : found) letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
                    return found;
                };
                const auto value = [&] {
                    std::string joined;
                    while (true) {
                        blanks();
                        if (at >= source.size()) break;
                        if (source[at] == '{' || source[at] == '"') {
                            const char closing = source[at] == '{' ? '}' : '"';
                            std::size_t depth = 0;
                            const std::size_t begin = ++at;
                            while (at < source.size() && !(source[at] == closing && depth == 0)) {
                                if (source[at] == '{') ++depth;
                                else if (source[at] == '}' && depth > 0) --depth;
                                ++at;
                            }
                            joined += source.substr(begin, at - begin);
                            if (at < source.size()) ++at;
                        } else {
                            const std::string name = word();
                            if (name.empty()) break;
                            const auto defined = strings.find(name);
                            joined += defined != strings.end() ? defined->second : name;
                        }
                        blanks();
                        if (at < source.size() && source[at] == '#') {
                            ++at;
                            continue;
                        }
                        break;
                    }
                    // Line ends and runs of blanks inside a value are one space.
                    std::string folded;
                    for (const char letter : joined) {
                        const bool blank = std::isspace(static_cast<unsigned char>(letter));
                        if (blank && (folded.empty() || folded.back() == ' ')) continue;
                        folded += blank ? ' ' : letter;
                    }
                    while (!folded.empty() && folded.back() == ' ') folded.pop_back();
                    return folded;
                };

                while ((at = source.find('@', at)) != std::string_view::npos) {
                    ++at;
                    const std::string type = word();
                    blanks();
                    if (at >= source.size() || (source[at] != '{' && source[at] != '(')) continue;
                    const char closing = source[at] == '{' ? '}' : ')';
                    ++at;
                    if (type == "preamble") {
                        preamble += value();
                        blanks();
                        if (at < source.size() && source[at] == closing) ++at;
                        continue;
                    }
                    if (type == "comment") {
                        std::size_t depth = 0;
                        while (at < source.size() && !(source[at] == closing && depth == 0)) {
                            if (source[at] == '{') ++depth;
                            else if (source[at] == '}' && depth > 0) --depth;
                            ++at;
                        }
                        continue;
                    }
                    if (type == "string") {
                        const std::string macro = word();
                        blanks();
                        if (at < source.size() && source[at] == '=') ++at;
                        strings.insert_or_assign(macro, value());
                        continue;
                    }

                    Entry entry{.type = type};
                    blanks();
                    const std::size_t begin = at;
                    while (at < source.size() && source[at] != ',' && source[at] != closing &&
                           !std::isspace(static_cast<unsigned char>(source[at]))) {
                        ++at;
                    }
                    entry.key = source.substr(begin, at - begin);
                    while (true) {
                        blanks();
                        if (at < source.size() && source[at] == ',') ++at;
                        blanks();
                        if (at >= source.size() || source[at] == closing) {
                            ++at;
                            break;
                        }
                        const std::string field = word();
                        blanks();
                        if (field.empty() || at >= source.size() || source[at] != '=') {
                            ++at;
                            continue;
                        }
                        ++at;
                        entry.fields.insert_or_assign(field, value());
                    }
                    if (!entry.key.empty()) read.push_back(std::move(entry));
                }
            }

            // Which entries: those cited, in the order first cited, then those
            // \nocite names; `*` brings every entry the files hold.
            memory::Dictionary<std::size_t> wanted;
            for (const Citation& citation : citations) {
                for (const std::string& key : citation.keys) wanted.try_emplace(key, wanted.size());
            }
            bool every = false;
            for (const std::string& key : extra) {
                if (key == "*") every = true;
                else wanted.try_emplace(key, wanted.size());
            }
            std::vector<Entry> chosen;
            for (Entry& entry : read) {
                const auto cited = wanted.find(entry.key);
                if (cited == wanted.end() && !every) continue;
                entry.cited = cited != wanted.end() ? cited->second : wanted.size() + chosen.size();
                chosen.push_back(std::move(entry));
            }

            // The style: BibTeX's standard ones, natbib's, or biblatex's, each
            // read as the BibTeX style it sets its entries as -- author and
            // year for authoryear, apa, chicago and harvard; IEEE's for ieee;
            // alpha's labels for alphabetic; in the order cited for nature,
            // science, the physics styles and `sorting=none`; plain's else.
            std::string style = "plain";
            if (const std::string* named = context.variables.get("bibliography.style")) style = *named;
            if (const std::string* named = context.variables.get("biblatex.style")) {
                const std::string_view picked = *named;
                const std::string* sorting = context.variables.get("biblatex.sorting");
                style = picked.starts_with("authoryear") || picked.starts_with("apa") || picked.starts_with("chicago") ||
                                picked.starts_with("harvard") || picked.starts_with("authortitle")
                            ? "plainnat"
                        : picked.starts_with("ieee")       ? "IEEEtran"
                        : picked.starts_with("alphabetic") ? "alpha"
                        : picked == "nature" || picked == "science" || picked.starts_with("phys") ||
                                (sorting && *sorting == "none")
                            ? "unsrt"
                            : "plain";
            }
            // The house styles journals and publishers ship, each writing an
            // entry its own way: IEEE's, the ACM's, Springer's LNCS, SIAM's,
            // Elsevier's and the AMS's. The rest are BibTeX's own, natbib's,
            // and anything else as plain.
            enum class House : std::uint8_t { Standard, Ieee, Acm, Springer, Siam, Elsevier, Ams };
            const House house = style.starts_with("IEEEtran") || style == "ieeetr"       ? House::Ieee
                                : style.starts_with("ACM") || style.starts_with("acm")  ? House::Acm
                                : style.starts_with("splncs") || style.starts_with("sp") ? House::Springer
                                : style.starts_with("siam")                             ? House::Siam
                                : style.starts_with("elsarticle") || style.starts_with("model") ? House::Elsevier
                                : style.starts_with("ams")                              ? House::Ams
                                                                                        : House::Standard;
            const bool natural = style.ends_with("nat") || style == "apalike" || style == "agsm" || style == "chicago" ||
                                 style.ends_with("harv");
            // An entry knows its author and year whenever a citation may name
            // them: in natbib's styles, and in biblatex's numeric one too,
            // whose \\textcite writes `Knuth [1]`.
            const bool named = natural || context.variables.get("biblatex.style") != nullptr;
            const bool ordered = style.starts_with("unsrt") || style == "ieeetr" || style == "IEEEtran" ||
                                 style.starts_with("apsrev") || style.starts_with("aip") || style == "naturemag" ||
                                 (house == House::Elsevier && !natural);
            const bool initials = style.starts_with("abbrv") || style == "ieeetr" || style.starts_with("apsrev") ||
                                  style.starts_with("aip");
            const bool alpha = style == "alpha" || style == "amsalpha";

            /// One name, BibTeX's four parts: `von Last, Jr, First` or `First von Last`.
            struct Name {
                std::string first{};
                std::string von{};
                std::string last{};
                std::string junior{};
            };
            // A list of names split at `and`, each split into its parts, the
            // way BibTeX splits them: outside braces only.
            const auto names = [](const std::string_view list) {
                std::vector<std::string_view> pieces;
                std::size_t depth = 0;
                std::size_t begin = 0;
                for (std::size_t at = 0; at < list.size(); ++at) {
                    if (list[at] == '{') ++depth;
                    else if (list[at] == '}' && depth > 0) --depth;
                    else if (depth == 0 && list.substr(at).starts_with(" and ")) {
                        pieces.push_back(list.substr(begin, at - begin));
                        begin = at + 5;
                        at += 4;
                    }
                }
                pieces.push_back(list.substr(begin));

                std::vector<Name> parsed;
                for (std::string_view piece : pieces) {
                    while (!piece.empty() && piece.front() == ' ') piece.remove_prefix(1);
                    while (!piece.empty() && piece.back() == ' ') piece.remove_suffix(1);
                    if (piece.empty()) continue;
                    // The words and the commas between them, outside braces.
                    std::vector<std::string> words;
                    std::vector<std::size_t> commas;
                    std::string current;
                    std::size_t level = 0;
                    for (const char letter : piece) {
                        if (letter == '{') ++level;
                        if (letter == '}' && level > 0) --level;
                        if (level == 0 && (letter == ' ' || letter == ',')) {
                            if (!current.empty()) words.push_back(std::move(current));
                            current.clear();
                            if (letter == ',') commas.push_back(words.size());
                            continue;
                        }
                        current += letter;
                    }
                    if (!current.empty()) words.push_back(std::move(current));
                    const auto lower = [](const std::string& text) {
                        return !text.empty() && std::islower(static_cast<unsigned char>(text.front()));
                    };
                    const auto range = [&words](const std::size_t from, const std::size_t to) {
                        std::string joined;
                        for (std::size_t index = from; index < to && index < words.size(); ++index) {
                            if (!joined.empty()) joined += ' ';
                            joined += words[index];
                        }
                        return joined;
                    };

                    Name name;
                    if (commas.empty()) {
                        // First von Last: the von is the lower-case words
                        // before the last, the first is what comes before it.
                        std::size_t von = 0;
                        while (von + 1 < words.size() && !lower(words[von])) ++von;
                        std::size_t last = von;
                        while (last + 1 < words.size() && lower(words[last])) ++last;
                        if (von + 1 >= words.size()) von = last = words.empty() ? 0 : words.size() - 1;
                        name.first = range(0, von);
                        name.von = range(von, last);
                        name.last = range(last, words.size());
                    } else {
                        // von Last, First -- or von Last, Jr, First.
                        std::size_t last = 0;
                        while (last + 1 < commas.front() && lower(words[last])) ++last;
                        name.von = range(0, last);
                        name.last = range(last, commas.front());
                        if (commas.size() > 1) {
                            name.junior = range(commas[0], commas[1]);
                            name.first = range(commas[1], words.size());
                        } else {
                            name.first = range(commas.front(), words.size());
                        }
                    }
                    parsed.push_back(std::move(name));
                }
                return parsed;
            };
            // A name as the style writes it: whole, or with its first names
            // as initials -- `Donald E. Knuth`, `D. E. Knuth`.
            const auto spell = [initials](const Name& name) {
                std::string first;
                if (initials) {
                    std::size_t at = 0;
                    while (at < name.first.size()) {
                        while (at < name.first.size() && (name.first[at] == ' ' || name.first[at] == '{' ||
                                                          name.first[at] == '}' || name.first[at] == '~')) {
                            ++at;
                        }
                        if (at >= name.first.size()) break;
                        if (!first.empty()) first += '~';
                        first += name.first[at];
                        first += '.';
                        while (at < name.first.size() && name.first[at] != ' ' && name.first[at] != '-') ++at;
                        if (at < name.first.size() && name.first[at] == '-') {
                            first += '-';
                            ++at;
                        }
                    }
                } else {
                    first = name.first;
                }
                std::string written = first;
                for (const std::string& part : {name.von, name.last}) {
                    if (part.empty()) continue;
                    if (!written.empty()) written += ' ';
                    written += part;
                }
                if (!name.junior.empty()) written += ", " + name.junior;
                return written;
            };
            // A list of them: `A`, `A and B`, `A, B, and C`; `others` is
            // BibTeX's way of writing the rest.
            const auto list = [&spell](const std::vector<Name>& people) {
                std::string written;
                for (std::size_t index = 0; index < people.size(); ++index) {
                    const bool others = people[index].last == "others";
                    if (index > 0) written += people.size() == 2 ? " and " : index + 1 == people.size() ? ", and " : ", ";
                    written += others ? "et~al." : spell(people[index]);
                }
                return written;
            };
            // What sorts: letters and digits only, lower case.
            const auto folded = [](const std::string_view text) {
                std::string plain;
                for (std::size_t at = 0; at < text.size(); ++at) {
                    if (text[at] == '\\') {
                        while (at + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[at + 1]))) ++at;
                        continue;
                    }
                    if (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == ' ') {
                        plain += static_cast<char>(std::tolower(static_cast<unsigned char>(text[at])));
                    }
                }
                return plain;
            };
            const auto field = [](const Entry& entry, const std::string_view name) {
                const auto found = entry.fields.find(name);
                return found != entry.fields.end() ? found->second : std::string{};
            };

            // Each entry's label and its sort key, as its style has them.
            for (Entry& entry : chosen) {
                std::vector<Name> people = names(field(entry, "author"));
                if (people.empty()) people = names(field(entry, "editor"));
                const std::string year = field(entry, "year");
                std::string sorted;
                for (const Name& person : people) sorted += folded(person.von + person.last + " " + person.first) + "  ";
                if (people.empty()) sorted = folded(entry.key);
                entry.order = sorted + "   " + year + "   " + folded(field(entry, "title"));

                if (named) {
                    std::string author;
                    const auto surname = [](const Name& person) {
                        return person.von.empty() ? person.last : person.von + " " + person.last;
                    };
                    if (people.empty()) author = entry.key;
                    else if (people.size() == 1) author = surname(people[0]);
                    else if (people.size() == 2 && people[1].last != "others") author = surname(people[0]) + " and " + surname(people[1]);
                    else author = surname(people[0]) + " et~al.";
                    entry.label = author + "(" + (year.empty() ? "?" : year) + ")";
                } else if (alpha) {
                    std::string made;
                    const auto letters = [](const std::string& text, const std::size_t count) {
                        std::string kept;
                        for (std::size_t at = 0; at < text.size() && kept.size() < count; ++at) {
                            if (text[at] == '\\') {
                                while (at + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[at + 1]))) ++at;
                                continue;
                            }
                            if (std::isalpha(static_cast<unsigned char>(text[at]))) kept += text[at];
                        }
                        return kept;
                    };
                    if (people.empty()) made = letters(entry.key, 3);
                    else if (people.size() == 1) made = letters(people[0].von + people[0].last, 3);
                    else {
                        for (std::size_t index = 0; index < std::min<std::size_t>(people.size(), people.size() > 4 ? 3 : 4); ++index) {
                            made += letters(people[index].last, 1);
                        }
                        if (people.size() > 4) made += "+";
                    }
                    entry.label = made + (year.size() >= 2 ? year.substr(year.size() - 2) : year);
                    entry.order = entry.label + "   " + entry.order;
                }
            }

            if (ordered) {
                std::ranges::stable_sort(chosen, {}, &Entry::cited);
            } else {
                std::ranges::stable_sort(chosen, {}, &Entry::order);
            }
            // alpha's labels made unique the way BibTeX makes them: a letter
            // after each of the ones that came out the same.
            if (alpha) {
                for (std::size_t index = 0; index < chosen.size();) {
                    std::size_t end = index + 1;
                    while (end < chosen.size() && chosen[end].label == chosen[index].label) ++end;
                    if (end - index > 1) {
                        for (std::size_t same = index; same < end; ++same) chosen[same].label += static_cast<char>('a' + (same - index));
                    }
                    index = end;
                }
            }

            // A title as BibTeX's styles set an article's: lower case after its
            // first letter, save what braces or a command protect.
            const auto titled = [](const std::string_view title) {
                std::string written;
                std::size_t depth = 0;
                bool first = true;
                for (std::size_t at = 0; at < title.size(); ++at) {
                    const char letter = title[at];
                    if (letter == '{') ++depth;
                    else if (letter == '}' && depth > 0) --depth;
                    if (letter == '\\') {
                        written += letter;
                        while (at + 1 < title.size() && std::isalpha(static_cast<unsigned char>(title[at + 1]))) {
                            written += title[++at];
                        }
                        continue;
                    }
                    if (std::isalpha(static_cast<unsigned char>(letter))) {
                        written += depth == 0 && !first ? static_cast<char>(std::tolower(static_cast<unsigned char>(letter)))
                                                        : letter;
                        first = false;
                        continue;
                    }
                    // After a colon a new sentence starts, capital kept.
                    if (letter == ':') first = true;
                    written += letter;
                }
                return written;
            };
            const auto pages = [](const std::string& range) {
                std::string written;
                for (std::size_t at = 0; at < range.size(); ++at) {
                    if (range[at] == '-') {
                        while (at + 1 < range.size() && range[at + 1] == '-') ++at;
                        written += "--";
                        continue;
                    }
                    written += range[at];
                }
                return written;
            };

            // Each entry as its type is set: blocks, a \newblock between each.
            // natbib's styles print a URL, and say what \url is where no
            // package has: typewriter text, as theirs does, a line ending
            // after a slash where it must.
            std::string written = preamble.empty() ? std::string{} : preamble + "\n";
            if (natural || house != House::Standard) written += "\\providecommand{\\url}[1]{\\texttt{\\@breakable{#1}}}\n";
            if (house == House::Springer) written += "\\providecommand{\\doi}[1]{\\url{https://doi.org/#1}}\n";
            written += std::format("\\begin{{thebibliography}}{{{}}}\n", alpha ? std::string("MMM99")
                                                                               : std::to_string(chosen.size()));
            // A given name as initials, `D.~E.`, or packed as Springer packs
            // them, `D.E.`.
            const auto initialed = [](const std::string& given, const std::string_view between) {
                std::string made;
                std::size_t at = 0;
                while (at < given.size()) {
                    while (at < given.size() && (given[at] == ' ' || given[at] == '{' || given[at] == '}' ||
                                                 given[at] == '~')) {
                        ++at;
                    }
                    if (at >= given.size()) break;
                    if (!made.empty() && !made.ends_with('-')) made += between;
                    made += given[at];
                    made += '.';
                    while (at < given.size() && given[at] != ' ' && given[at] != '-' && given[at] != '~') ++at;
                    if (at < given.size() && given[at] == '-') {
                        made += '-';
                        ++at;
                    }
                }
                return made;
            };
            const auto surname = [](const Name& person) {
                return person.von.empty() ? person.last : person.von + " " + person.last;
            };
            // A house's list of names: each written its way, joined its way.
            const auto roll = [&](const std::vector<Name>& people, const auto& write, const std::string_view pair,
                                  const std::string_view final) {
                std::string written;
                for (std::size_t index = 0; index < people.size(); ++index) {
                    if (index > 0) written += people.size() == 2 ? pair : index + 1 == people.size() ? final : ", ";
                    written += people[index].last == "others" ? std::string("et~al.") : write(people[index]);
                }
                return written;
            };
            const auto forward = [&](const Name& person) {
                std::string given = initialed(person.first, "~");
                return (given.empty() ? std::string{} : given + "~") + surname(person) +
                       (person.junior.empty() ? std::string{} : ", " + person.junior);
            };
            const auto full = [&](const Name& person) {
                return (person.first.empty() ? std::string{} : person.first + " ") + surname(person);
            };
            const auto backward = [&](const Name& person) {
                std::string given = initialed(person.first, "");
                return surname(person) + (given.empty() ? std::string{} : ", " + given);
            };

            for (const Entry& entry : chosen) {
                std::vector<std::string> blocks;
                // A block whose first part was missing starts at its second's
                // comma -- a misc with no howpublished, `, 1843` -- and is cut
                // to what is there, as BibTeX's output.check leaves it.
                const auto add = [&blocks](std::string block) {
                    while (!block.empty() && (block.back() == ' ' || block.back() == ',')) block.pop_back();
                    while (!block.empty() && (block.front() == ' ' || block.front() == ',')) block.erase(block.begin());
                    if (!block.empty()) blocks.push_back(std::move(block));
                };
                const auto with = [](const std::string& value, const std::string& prefix, const std::string& suffix = "") {
                    return value.empty() ? std::string{} : prefix + value + suffix;
                };
                const std::string& type = entry.type;
                const std::vector<Name> authors = names(field(entry, "author"));
                const std::vector<Name> editors = names(field(entry, "editor"));
                const std::string title = field(entry, "title");
                const std::string year = field(entry, "year");
                const std::string month = field(entry, "month");
                const std::string when = month.empty() ? year : year.empty() ? month : month + " " + year;
                const std::string volume = field(entry, "volume");
                const std::string number = field(entry, "number");
                const std::string span = pages(field(entry, "pages"));
                const std::string publisher = field(entry, "publisher");
                const std::string address = field(entry, "address");
                const std::string edited = editors.empty() ? std::string{}
                                                           : list(editors) + (editors.size() > 1 ? ", editors" : ", editor");

                // A house style's entry, written whole.
                if (house != House::Standard && !natural) {
                    const std::string journal = field(entry, "journal");
                    const std::string booktitle = field(entry, "booktitle");
                    const std::string url = field(entry, "url");
                    const std::string doi = field(entry, "doi");
                    const std::string place = type == "phdthesis"   ? field(entry, "school")
                                              : type == "techreport" ? field(entry, "institution")
                                                                     : publisher;
                    const bool paper = type == "article" || type == "inproceedings" || type == "conference" ||
                                       type == "incollection";
                    std::string text;
                    switch (house) {
                        case House::Ieee: {
                            // A. Author and B. Author, ``Title,'' \emph{Journal}, vol.~1, no.~2, pp.~3--4, Jan. 2020.
                            text = roll(authors.empty() ? editors : authors, forward, " and ", ", and ");
                            const bool quoted = type != "book" && type != "booklet" && type != "proceedings" &&
                                                type != "manual";
                            std::vector<std::string> parts;
                            if (type == "article") {
                                parts = {with(journal, "\\emph{", "}"), with(volume, "vol.~"), with(number, "no.~"),
                                         with(span, "pp.~"), when};
                            } else if (type == "book" || type == "booklet" || type == "proceedings") {
                                parts = {with(field(entry, "edition"), "", "~ed."),
                                         address + (address.empty() || publisher.empty() ? "" : ": ") + publisher, when};
                            } else if (type == "inproceedings" || type == "conference" || type == "incollection") {
                                parts = {with(booktitle, "in \\emph{", "}"), address, when, with(span, "pp.~")};
                            } else if (type == "phdthesis" || type == "mastersthesis") {
                                parts = {type == "phdthesis" ? "Ph.D. dissertation" : "Master's thesis", place, address, when};
                            } else if (type == "techreport") {
                                parts = {place, address, "Tech. Rep." + with(number, "~"), when};
                            } else {
                                parts = {field(entry, "howpublished"), with(field(entry, "eprint"), "arXiv:"), when};
                            }
                            std::string rest;
                            for (const std::string& part : parts) {
                                if (part.empty()) continue;
                                if (!rest.empty()) rest += ", ";
                                rest += part;
                            }
                            if (!title.empty()) {
                                if (!text.empty()) text += ", ";
                                if (quoted) text += "``" + title + (rest.empty() ? ".''" : ",'' ");
                                else text += "\\emph{" + title + "}" + (rest.empty() ? "." : ", ");
                            } else if (!rest.empty()) {
                                text += ", ";
                            }
                            text += rest;
                            if (!rest.empty()) text += '.';
                            if (!url.empty()) text += " [Online]. Available: \\url{" + url + "}";
                            break;
                        }
                        case House::Acm: {
                            // Author and Author. 2020. Title. \emph{Journal} 1, 2 (Jan. 2020), 3--4.
                            text = roll(authors.empty() ? editors : authors, full, " and ", ", and ") + ". " +
                                   (year.empty() ? std::string{} : year + ". ");
                            if (type == "article") {
                                text += title + ". " + with(journal, "\\emph{", "}") + with(volume, " ") +
                                        with(number, ", ") + with(when, " (", ")") + with(span, ", ") + ".";
                            } else if (paper) {
                                text += title + ". In \\emph{" + booktitle + "}" + with(address, " (", ")") + ". " +
                                        with(publisher, "", ", ") + with(span, "", "") + ".";
                            } else if (type == "book" || type == "booklet" || type == "proceedings") {
                                text += "\\emph{" + title + "}. " + publisher + with(address, ", ") + ".";
                            } else {
                                text += title + ". " + with(field(entry, "howpublished"), "", ".");
                            }
                            text += with(doi, " \\url{https://doi.org/", "}");
                            break;
                        }
                        case House::Springer: {
                            // Author, F., Author, S.: Title. Journal \textbf{2}(5), 99--110 (2016)
                            text = roll(authors.empty() ? editors : authors, backward, ", ", ", ") + ": " + title + ". ";
                            if (type == "article") {
                                text += journal + with(volume, " \\textbf{", "}") + with(number, "(", ")") +
                                        with(span, ", ") + with(year, " (", ")");
                            } else if (paper) {
                                text += "In: " +
                                        with(roll(editors, backward, ", ", ", "), "", editors.size() > 1 ? " (eds.) " : " (ed.) ") +
                                        booktitle + ". " + with(field(entry, "series"), "", ", ") + with(volume, "vol.~", ", ") +
                                        with(span, "pp.~", ". ") + place + with(address, place.empty() ? "" : ", ") +
                                        with(year, " (", ")");
                            } else {
                                text += place + with(address, place.empty() ? "" : ", ") + with(year, " (", ")");
                            }
                            text += with(doi, " \\doi{", "}");
                            break;
                        }
                        case House::Siam: {
                            // {\sc D.~E. Knuth}, {\em Title}, Journal, 1 (2020), pp.~3--4.
                            text = "{\\sc " + roll(authors.empty() ? editors : authors, forward, " and ", ", and ") +
                                   "}, {\\em " + title + "}";
                            if (type == "article") {
                                text += with(journal, ", ") + with(volume, ", ") + with(year, " (", ")") + with(span, ", pp.~");
                            } else if (paper) {
                                text += with(booktitle, ", in ") + with(address, ", ") + with(year, ", ") + with(publisher, ", ") +
                                        with(span, ", pp.~");
                            } else {
                                text += with(place, ", ") + with(address, ", ") + with(year, ", ");
                            }
                            text += '.';
                            break;
                        }
                        case House::Elsevier: {
                            // A.~B. Author, C. Author, Title, Journal 1~(2) (2020) 3--4.
                            text = roll(authors.empty() ? editors : authors, forward, ", ", ", ") + ", " + title;
                            if (type == "article") {
                                text += with(journal, ", ") + with(volume, " ") + with(number, "~(", ")") + with(year, " (", ")") +
                                        with(span, " ");
                            } else if (paper) {
                                text += with(booktitle, ", in: ") + with(publisher, ", ") + with(address, ", ") + with(year, ", ") +
                                        with(span, ", pp. ");
                            } else {
                                text += with(place, ", ") + with(address, ", ") + with(year, ", ");
                            }
                            text += '.' + with(doi, " \\url{https://doi.org/", "}");
                            break;
                        }
                        case House::Ams: {
                            // D.~E. Knuth, \emph{Title}, Journal \textbf{1} (2020), no.~2, 3--4.
                            text = roll(authors.empty() ? editors : authors, forward, " and ", ", and ") + ", \\emph{" + title + "}";
                            if (type == "article") {
                                text += with(journal, ", ") + with(volume, " \\textbf{", "}") + with(year, " (", ")") +
                                        with(number, ", no.~") + with(span, ", ");
                            } else if (paper) {
                                text += with(booktitle, ", ") + with(address, " (", ")") + with(publisher, ", ") +
                                        with(year, ", ") + with(span, ", pp.~");
                            } else {
                                text += with(place, ", ") + with(address, ", ") + with(year, ", ");
                            }
                            text += '.';
                            break;
                        }
                        case House::Standard: break;
                    }
                    written += "\\bibitem" + (named || alpha ? "[" + entry.label + "]" : std::string{}) + "{" + entry.key +
                               "}\n" + text + "\n\n";
                    continue;
                }

                if (!authors.empty()) add(list(authors));
                else if (!edited.empty()) add(edited);

                const bool whole = type == "book" || type == "phdthesis" || type == "manual" || type == "proceedings" ||
                                   type == "booklet";
                if (!title.empty()) add(whole ? "\\emph{" + title + "}" : titled(title));

                if (type == "article") {
                    std::string place = with(field(entry, "journal"), "\\emph{", "}");
                    std::string reference = volume + with(number, "(", ")") + with(span, volume.empty() && number.empty() ? "pages " : ":");
                    add(place + with(reference, place.empty() ? "" : ", ") + with(when, ", "));
                } else if (type == "book" || type == "booklet" || type == "proceedings") {
                    // The edition in lower case after the publisher, as plain
                    // changes its case mid-sentence: `Addison-Wesley, second
                    // edition`.
                    std::string edition = field(entry, "edition");
                    if (!edition.empty() && !(publisher.empty() && address.empty())) {
                        edition.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(edition.front())));
                    }
                    add(publisher + with(address, publisher.empty() ? "" : ", ") + with(edition, ", ", " edition") +
                        with(when, ", "));
                } else if (type == "inproceedings" || type == "conference" || type == "incollection") {
                    std::string place = "In " + with(edited, "", ", ") + with(field(entry, "booktitle"), "\\emph{", "}");
                    place += with(volume, ", volume ") + with(span, ", pages ") + with(address, ", ") + with(when, ", ");
                    add(place);
                    add(publisher);
                } else if (type == "phdthesis" || type == "mastersthesis") {
                    add(std::string(type == "phdthesis" ? "PhD thesis" : "Master's thesis") +
                        with(field(entry, "school"), ", ") + with(address, ", ") + with(when, ", "));
                } else if (type == "techreport") {
                    add(std::string(field(entry, "type").empty() ? "Technical Report" : field(entry, "type")) +
                        with(number, " ") + with(field(entry, "institution"), ", ") + with(when, ", "));
                } else if (type == "manual") {
                    add(field(entry, "organization") + with(address, ", ") + with(when, ", "));
                } else {
                    // misc, online, unpublished and every type BibTeX does not know.
                    add(field(entry, "howpublished") + with(field(entry, "eprint"), "arXiv:") + with(when, ", "));
                }
                add(field(entry, "note"));
                if (natural) add(with(field(entry, "url"), "\\url{", "}"));

                std::string text;
                for (std::size_t index = 0; index < blocks.size(); ++index) {
                    text += blocks[index];
                    if (!blocks[index].ends_with('.') || blocks[index].ends_with("al.")) text += '.';
                    if (index + 1 < blocks.size()) text += "\n\\newblock ";
                }
                written += "\\bibitem" + (named || alpha ? "[" + entry.label + "]" : std::string{}) + "{" + entry.key +
                           "}\n" + text + "\n\n";
            }
            written += "\\end{thebibliography}\n";

            // A key cited that no file held is reported, as BibTeX does.
            for (const auto& [key, place] : wanted) {
                if (std::ranges::none_of(chosen, [&key](const Entry& entry) { return entry.key == key; }) &&
                    !marks[key].defined) {
                    tracebacks.emplace_back(syntax::Traceback::Type::Warning, origin,
                                             std::format("Citation `{}' undefined", key));
                }
            }
            if (chosen.empty()) return;
            mouth.ingest(context.arena.copy(written));
        };

        parser.bind("\\bibliography", [this, compile, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            std::vector<std::string> files;
            for (const auto piece : std::views::split(syntax::Argument::text(mouth), ',')) {
                std::string file(piece.begin(), piece.end());
                while (!file.empty() && file.front() == ' ') file.erase(file.begin());
                while (!file.empty() && file.back() == ' ') file.pop_back();
                if (!file.empty()) files.push_back(std::move(file));
            }
            // And every file imported -- \input{refs.bib}, \addbibresource --
            // that it does not name already, so `\bibliography{}` sets those.
            for (const std::string& resource : resources) {
                const auto same = [&resource](const std::string& file) {
                    return (file.ends_with(".bib") ? file : file + ".bib") == resource;
                };
                if (std::ranges::none_of(files, same)) files.push_back(resource);
            }

            // A file handed in, or one beside the document.
            const auto readable = [&context](const std::string& name) -> const std::string* {
                if (context.files) {
                    if (const auto found = context.files->find(name); found != context.files->end()) return &found->second;
                }
                return context.disk ? context.disk(name) : nullptr;
            };

            // A document that carries its bibliography already made -- the
            // .bbl BibTeX wrote beside it, which is what a paper sent to a
            // journal or the arXiv carries in place of its .bib -- is set
            // from that when none of the files it names can be read, as
            // LaTeX sets it: the \jobname's .bbl, read where \bibliography
            // stands.
            const bool named = std::ranges::any_of(files, [&readable](const std::string& file) {
                return readable(file.ends_with(".bib") ? file : file + ".bib") != nullptr;
            });
            if (!named) {
                std::string job;
                if (const syntax::Mouth::Macro* macro = mouth.macro(mouth.lexicon.intern("\\jobname"))) {
                    for (const syntax::Token& token : macro->body) job += token.text;
                }
                if (const std::string* made = job.empty() ? nullptr : readable(job + ".bbl")) {
                    mouth.ingest(*made);
                    return nullptr;
                }
            }
            compile(mouth, files);
            return nullptr;
        });
        // biblatex's list, its heading as its options say: `heading=none`
        // for none, `bibintoc` listed in the contents, `title={...}` its own.
        parser.bind("\\printbibliography", [this, compile](syntax::Parser& parser) -> syntax::Node* {
            std::string options;
            for (const syntax::Token& token : parser.mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                options += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') options += ' ';
            }
            std::string title = "\\@bibtitle";
            std::string kind;
            for (const auto piece : std::views::split(std::string_view(options), ',')) {
                std::string_view option(piece.begin(), piece.end());
                while (!option.empty() && option.front() == ' ') option.remove_prefix(1);
                const std::size_t equals = option.find('=');
                const std::string_view key = option.substr(0, std::min(equals, option.size()));
                std::string_view value = equals == std::string_view::npos ? std::string_view{} : option.substr(equals + 1);
                while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
                if (value.size() >= 2 && value.front() == '{' && value.back() == '}') value = value.substr(1, value.size() - 2);
                if (key.starts_with("heading")) kind = value;
                if (key.starts_with("title")) title = value;
            }
            if (kind == "none") heading = std::string{};
            else if (kind.contains("intoc") || title != "\\@bibtitle") {
                heading = "\\csname\\@bibkind\\endcsname*{" + title + "}" +
                          (kind.contains("intoc") ? "\\addcontentsline{toc}{\\@bibkind}{" + title + "}" : "");
            }
            compile(parser.mouth, resources);
            return nullptr;
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound citation primitives");
    }

}
