/// @file
/// @brief Alignment primitives: `\\halign`, and the `tabular` blocks.
///
/// Two passes over the cells, because the first one cannot know how wide a
/// column is until it has seen the last row of it. Pass one builds every cell
/// and records the widest in each column; pass two pads each cell out to that.
#include "render/primitives/tables.hpp"
#include "render/primitives/colors.hpp"
#include "syntax/argument.hpp"
#include "layout/line.hpp"
#include "layout/paragraph.hpp"
#include "logger.hpp"

#include "syntax/semantics/scope.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace render::primitives {

    /// @brief The contents of the brace group at a position in a preamble, and
    ///        the position after it -- or the one character there, when no
    ///        group is, as TeX reads an undelimited argument.
    /// @param text The preamble.
    /// @param at   Where the group starts; moved past it.
    /// @return What the group holds.
    /// @complexity O(n) in the group's length.
    static std::string_view enclosed(const std::string_view text, std::size_t& at) {
        while (at < text.size() && text[at] == ' ') ++at;
        if (at >= text.size()) return {};
        if (text[at] != '{') return text.substr(at++, 1);

        const std::size_t start = at + 1;
        int depth = 0;
        for (; at < text.size(); ++at) {
            if (text[at] == '{') {
                ++depth;
            } else if (text[at] == '}' && --depth == 0) {
                const std::string_view inside = text.substr(start, at - start);
                ++at;
                return inside;
            }
        }
        return text.substr(start);
    }

    /// @brief Reads a preamble's text into columns.
    /// @param text    The preamble, braces and all.
    /// @param columns Where the columns go; appended to.
    /// @param depth   How many `*{n}{...}` repeats this is inside, so a
    ///                preamble that repeats itself cannot run forever.
    /// @param customs The column types \\newcolumntype made, or none.
    /// @complexity O(n) in the preamble's expanded length.
    static void spell(const std::string_view text, std::vector<Tables::Column>& columns, const int depth,
                      const Tables::Customs* customs) {
        int rules = 0;        // `|` waiting for the next column
        bool padded = true;   // whether the next column keeps its left padding
        std::string head;     // `>{...}` waiting for the next column
        std::size_t at = 0;

        const auto push = [&](Tables::Column column) {
            column.before = rules;
            column.opened = padded;
            column.head = std::move(head);
            rules = 0;
            padded = true;
            head.clear();
            columns.push_back(std::move(column));
        };

        while (at < text.size()) {
            const char letter = text[at++];
            switch (letter) {
                case 'l': case 'c': case 'r':
                    push(Tables::Column{.align = letter});
                    break;
                // siunitx's number and unit columns, centred as their numbers
                // would be; their options, `S[table-format=2.2]`, read and let go.
                case 'S': case 's':
                    if (at < text.size() && text[at] == '[') {
                        const std::size_t end = text.find(']', at);
                        at = end == std::string_view::npos ? text.size() : end + 1;
                    }
                    push(Tables::Column{.align = 'c'});
                    break;
                case 'p': case 'm': case 'b':
                    push(Tables::Column{.align = 'l', .width = std::string(enclosed(text, at))});
                    break;
                // tabularx's X, and tabu's with its options, `X[c]`, `X[2,r]`:
                // the side its lines stand at among them, its share of the
                // width let go.
                case 'X': {
                    char side = 'l';
                    if (at < text.size() && text[at] == '[') {
                        const std::size_t end = std::min(text.find(']', at), text.size());
                        for (const char option : text.substr(at + 1, end - at - 1)) {
                            if (option == 'c' || option == 'r' || option == 'C' || option == 'R') {
                                side = static_cast<char>(option | 0x20);
                            }
                        }
                        at = std::min(end + 1, text.size());
                    }
                    push(Tables::Column{.align = side, .stretch = true});
                    break;
                }
                case '|':
                    ++rules;
                    break;
                // `@{...}` puts its text where the padding between two
                // columns was; `!{...}` keeps the padding. Only the padding
                // matters here.
                case '@':
                    static_cast<void>(enclosed(text, at));
                    if (!columns.empty() && rules == 0) columns.back().closed = false;
                    padded = false;
                    break;
                case '>':
                    head += enclosed(text, at);
                    break;
                case '!': case '<':
                    static_cast<void>(enclosed(text, at));
                    break;
                case '*': {
                    const std::string_view times = enclosed(text, at);
                    const std::string_view body = enclosed(text, at);
                    int count = 0;
                    std::from_chars(times.data(), times.data() + times.size(), count);
                    for (int step = 0; step < count && depth < 8; ++step) spell(body, columns, depth + 1, customs);
                    break;
                }
                // A control sequence in a preamble belongs to a declaration.
                case '\\':
                    while (at < text.size() && ((text[at] >= 'a' && text[at] <= 'z') ||
                                                (text[at] >= 'A' && text[at] <= 'Z'))) {
                        ++at;
                    }
                    break;
                default: {
                    // A column type \newcolumntype made: its preamble, its
                    // arguments put in, read in its place.
                    if (const auto code = static_cast<unsigned char>(letter); customs && code < customs->size() &&
                        !(*customs)[code].body.empty()) {
                        const Tables::Custom& made = (*customs)[code];
                        std::string expanded = made.body;
                        for (int index = 1; index <= made.count; ++index) {
                            const std::string argument(enclosed(text, at));
                            const std::string mark = "#" + std::to_string(index);
                            for (std::size_t found = expanded.find(mark); found != std::string::npos;
                                 found = expanded.find(mark, found + argument.size())) {
                                expanded.replace(found, mark.size(), argument);
                            }
                        }
                        if (depth < 8) spell(expanded, columns, depth + 1, customs);
                        break;
                    }
                    // tabulary's columns, as wide as their text: set as the
                    // plain ones; dcolumn's D{.}{.}{3}, centred; array's
                    // w{c}{2cm} and W, a fixed width aligned as its first
                    // argument says.
                    if (letter == 'L' || letter == 'J') push(Tables::Column{.align = 'l'});
                    else if (letter == 'C') push(Tables::Column{.align = 'c'});
                    else if (letter == 'R') push(Tables::Column{.align = 'r'});
                    else if (letter == 'D') {
                        for (int skipped = 0; skipped < 3; ++skipped) static_cast<void>(enclosed(text, at));
                        push(Tables::Column{.align = 'c'});
                    } else if (letter == 'w' || letter == 'W') {
                        const std::string_view align = enclosed(text, at);
                        push(Tables::Column{.align = align.empty() ? 'l' : align.front(),
                                            .width = std::string(enclosed(text, at))});
                    }
                    break;
                }
            }
        }

        if (!columns.empty()) {
            columns.back().after += rules;
            if (!padded) columns.back().closed = false;
        }
    }

    std::vector<Tables::Column> Tables::preamble(const std::vector<syntax::Token>& tokens, const Customs* customs) {
        // Written back out as text, a control word keeping the space that
        // ended it, so its letters cannot run into a column letter after it.
        std::string text;
        for (const syntax::Token& token : tokens) {
            text += token.text;
            if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
        }

        std::vector<Column> columns;
        spell(text, columns, 0, customs);
        return columns;
    }

    Tables::Tables(syntax::Lexicon& lexicon) noexcept {
        row = lexicon.intern("\\cr");
        column = lexicon.intern("&");
    }

    void Tables::operator()(syntax::Parser& parser, Context& context) const {
        parser.bind("\\halign", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            // A table is written across several lines, so the blanks between
            // its parts are layout in the source and mean nothing here. Each
            // place a separator is expected skips them first.
            const auto blanks = [&mouth] {
                while (mouth.lookahead().category == syntax::Catcodes::Category::Space) {
                    mouth.read();
                }
            };

            blanks();
            syntax::Token open = mouth.read();
            if (!open.is(syntax::Catcodes::Category::Group, '{')) {
                if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                tracebacks.emplace_back(syntax::Traceback::Type::Group, origin,
                                         "\\halign needs a brace group for its rows");
                return directive(arena, nullptr, origin);
            }

            mouth.push(syntax::semantics::Scope::Type::Alignment);

            std::vector<std::vector<layout::Node*>> grid;
            std::vector<layout::Node*> line;
            bool closed = false;

            while (grid.size() < limit) {
                // Each cell is a brace group. Anything else at this point is
                // taken as the start of one anyway, so a missing brace costs
                // one cell rather than the whole table.
                blanks();
                syntax::Token brace = mouth.read();
                if (!brace.is(syntax::Catcodes::Category::Group, '{') && !brace.empty()) {
                    mouth.stream().inject(std::span{&brace, 1});
                }

                const memory::Slice<syntax::Node*> children = parser.parse('}');

                std::vector<layout::Node*> nodes;
                nodes.reserve(children.count);
                for (std::size_t index = 0; index < children.count; ++index) {
                    compose(nodes, children[index], context);
                }

                const memory::Slice<layout::Node*> contents =
                    arena.allocate<layout::Node*>(nodes.size());
                for (std::size_t index = 0; index < nodes.size(); ++index) {
                    contents[index] = nodes[index];
                }
                line.push_back(layout::Line::horizontal(arena, contents, 0.0f));

                blanks();
                const syntax::Token next = mouth.read();
                if (next.symbol == column) continue;

                if (next.symbol == row) {
                    grid.push_back(std::move(line));
                    line.clear();

                    // A `}` after a `\\cr` is the end of the table; anything
                    // else is the first cell of the next row.
                    blanks();
                    const syntax::Token close = mouth.read();
                    if (close.is(syntax::Catcodes::Category::Group, '}') || close.empty()) {
                        closed = true;
                        break;
                    }
                    mouth.stream().inject(std::span{&close, 1});
                    continue;
                }

                // Neither separator: the row ends here and so does the table.
                if (!line.empty()) grid.push_back(std::move(line));
                closed = next.is(syntax::Catcodes::Category::Group, '}') || next.empty();
                break;
            }

            mouth.pop(syntax::semantics::Scope::Type::Alignment);

            if (!closed) {
                tracebacks.emplace_back(syntax::Traceback::Type::Group, origin,
                                         "\\halign ran past the end of its rows");
            }
            if (grid.empty()) return directive(arena, nullptr, origin, true);

            // Pass one's result: how wide each column has to be.
            std::size_t columns = 0;
            for (const auto& cells : grid) columns = std::max(columns, cells.size());

            const memory::Slice<float> widths = arena.allocate<float>(columns);
            for (std::size_t index = 0; index < columns; ++index) widths[index] = 0.0f;

            for (const auto& cells : grid) {
                for (std::size_t index = 0; index < cells.size(); ++index) {
                    if (cells[index]) widths[index] = std::max(widths[index], cells[index]->box().width);
                }
            }

            // Pass two: each cell followed by a kern out to its column's width.
            const memory::Slice<layout::Node*> rows = arena.allocate<layout::Node*>(grid.size());

            for (std::size_t outer = 0; outer < grid.size(); ++outer) {
                const std::vector<layout::Node*>& cells = grid[outer];
                const memory::Slice<layout::Node*> flat =
                    arena.allocate<layout::Node*>(cells.size() * 2);
                std::size_t filled = 0;

                for (std::size_t index = 0; index < cells.size(); ++index) {
                    layout::Node* cell = cells[index];
                    flat[filled++] = cell;

                    // Out to the column's width, plus the gap to the next one.
                    // The last cell in a row gets neither, so a table does not
                    // carry a trailing space it never asked for.
                    const float taken = cell ? cell->box().width : 0.0f;
                    const float slack = widths[index] - taken +
                                        (index + 1 < cells.size() ? gutter : 0.0f);
                    if (slack > 0.0f) {
                        auto* pad = arena.compose<layout::Node>();
                        pad->kern({.width = slack});
                        flat[filled++] = pad;
                    }
                }

                rows[outer] = layout::Line::horizontal(arena, memory::Slice{flat.data, filled}, 0.0f);
            }

            Logger::log(Logger::Type::Layout, Logger::Level::Debug,
                        "Aligned {} rows over {} columns", grid.size(), columns);

            return directive(arena, layout::Line::vertical(arena, rows, 0.0f), origin, true);
        });

        // LaTeX's tables. Each `\begin` reads its preamble -- and tabularx
        // and tabular* their width first -- and hands over to `\tabular`,
        // which reads the rows as far as the marker `\end` leaves.
        struct Kind {
            std::string_view name;   ///< The block.
            bool sized;              ///< True when a width comes before the preamble.
            bool breakable;          ///< True when a page may end between its rows.
        };
        // nicematrix's and tabularray's tables are read as the tabular they
        // write out, their own keys aside.
        static constexpr std::array<Kind, 14> kinds{{
            {"tabular", false, false}, {"tabular*", true, false}, {"tabularx", true, false},
            {"longtable", false, true}, {"supertabular", false, true}, {"tabulary", true, false},
            {"xtabular", false, true}, {"longtable*", false, true}, {"NiceTabular", false, false},
            {"NiceTabular*", true, false}, {"NiceTabularX", true, false}, {"tblr", false, false},
            {"longtblr", false, true}, {"xltabular", true, true},
        }};
        for (const auto& [name, sized, breakable] : kinds) {
            const bool keyed = name.ends_with("tblr");
            context.blocks.watch(
                name,
                [this, &context, sized, keyed, breakable](syntax::Mouth& mouth) {
                    Opening opening;
                    opening.breakable = breakable;
                    if (sized) opening.width = syntax::Argument::text(mouth);
                    // A vertical position, `[t]`: which of its rows the table
                    // stands on, when it is set in a line or in another's cell.
                    for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                        if (!token.text.empty()) opening.position = token.text.front();
                    }
                    std::vector<syntax::Token> columns = mouth.argument({}, 0);
                    // tabularray's keys, `colspec={lcr}, hlines`: the preamble
                    // is what its colspec holds, the rest of its keys read.
                    std::string spelled;
                    std::vector<std::size_t> starts;
                    for (const syntax::Token& token : columns) {
                        starts.push_back(spelled.size());
                        spelled += token.text;
                    }
                    if (const std::size_t found = spelled.find("colspec"); keyed && found != std::string::npos) {
                        std::size_t at = static_cast<std::size_t>(std::ranges::lower_bound(starts, found) - starts.begin());
                        while (at < columns.size() && !columns[at].is(syntax::Catcodes::Category::Group, '{')) ++at;
                        std::vector<syntax::Token> inside;
                        int depth = 0;
                        for (++at; at < columns.size(); ++at) {
                            if (columns[at].is(syntax::Catcodes::Category::Group, '{')) ++depth;
                            if (columns[at].is(syntax::Catcodes::Category::Group, '}') && depth-- == 0) break;
                            inside.push_back(columns[at]);
                        }
                        columns = std::move(inside);
                    }
                    opening.columns = preamble(columns, &customs);

                    // A long table's caption is longtable's: a table's, across
                    // every column in a box of no width, so it widens none of
                    // them, with room under it; `\\caption*` its text alone.
                    std::string caption;
                    if (breakable) {
                        const std::string across = "\\multicolumn{" + std::to_string(opening.columns.size()) + "}{c}";
                        caption = "\\@define\\caption{\\@ifstar\\@longplain\\@longcaption}"
                                  "\\@define\\@longcaption[2][]{" + across +
                                  "{\\makebox[0pt][c]{\\captionof{table}[#1]{#2}}\\rule[-1em]{0pt}{1em}}}"
                                  "\\@define\\@longplain#1{" + across + "{\\makebox[0pt][c]{#1}\\rule[-1em]{0pt}{1em}}}";
                    }
                    openings.push_back(std::move(opening));
                    mouth.ingest(context.arena.copy(caption + "\\tabular "));
                },
                [](syntax::Mouth& mouth) { mouth.ingest("\\endtabular "); });
        }

        // LaTeX's tabbing: lines set at tab stops, `\\=` setting one and `\\>`
        // moving to the next. Read as a table of left-aligned columns with
        // nothing between them -- each as wide as its widest entry, where
        // tabbing takes the first line's -- and inside it, undone when it
        // closes, the two are a word space and the table's `&`: a stop stands
        // past the space typed before it. A line \\kill ends is measured and
        // not drawn, as a table's is.
        context.blocks.watch(
            "tabbing",
            [this](syntax::Mouth& mouth) {
                mouth.ingest("{@{}l@{}l@{}l@{}l@{}l@{}l@{}l@{}l@{}l@{}l@{}}");
                Opening opening;
                opening.columns = preamble(mouth.argument({}, 0), &customs);
                openings.push_back(std::move(opening));
                mouth.ingest("\\@define\\={\\ &}\\@define\\>{\\ &}\\@define\\+{}\\@define\\-{}\\@define\\<{}"
                             "\\par\\noindent\\tabular ");
            },
            [](syntax::Mouth& mouth) { mouth.ingest("\\endtabular\\par "); });

        // array's \newcolumntype{C}[1]{>{\centering}p{#1}}: a letter that
        // stands for a preamble of its own, with up to nine arguments, which
        // any table's preamble may use from here on.
        parser.mouth.bind("\\newcolumntype", [this](syntax::Mouth& mouth) {
            const std::string letter = syntax::Argument::text(mouth);
            std::string count;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                count += token.text;
            }
            std::string body;
            for (const syntax::Token& token : mouth.argument({}, 0)) {
                body += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') body += ' ';
            }
            if (letter.size() != 1 || static_cast<unsigned char>(letter[0]) >= customs.size()) return;
            Custom& made = customs[static_cast<unsigned char>(letter[0])];
            made.count = count.empty() ? 0 : count[0] - '0';
            made.body = std::move(body);
        });

        // multirow's `\\multirow[place]{rows}[struts]{width}[move]{text}`:
        // in a table's cell, text set down the rows it spans, in their middle
        // or, for `[t]` and `[b]`, level with the first or the last, as
        // \\tabular reads it; anywhere else, its text where it stands. The
        // width, the struts and the move are read and let go.
        struct Span {
            std::size_t rows{1};                       ///< How many rows it spans.
            char place{'c'};                           ///< t, c or b.
            memory::Slice<syntax::Node*> nodes{};      ///< Its text.
        };
        const auto spanned = [&context](syntax::Parser& parser) {
            syntax::Mouth& mouth = parser.mouth;
            const syntax::Mouth::Parameter optional{.optional = true};
            Span made;
            for (const syntax::Token& token : mouth.argument(optional, 0)) {
                if (!token.text.empty()) made.place = token.text.front();
            }
            const std::string count = syntax::Argument::text(mouth);
            std::from_chars(count.data(), count.data() + count.size(), made.rows);
            static_cast<void>(mouth.argument(optional, 0));
            static_cast<void>(syntax::Argument::text(mouth));
            static_cast<void>(mouth.argument(optional, 0));

            syntax::Token open = mouth.read();
            while (open.category == syntax::Catcodes::Category::Space) open = mouth.read();
            if (open.is(syntax::Catcodes::Category::Group, '{')) {
                mouth.push(syntax::semantics::Scope::Type::Group);
                made.nodes = parser.parse('}');
                mouth.pop(syntax::semantics::Scope::Type::Group);
                stamp(made.nodes, context);
            } else if (!open.empty()) {
                mouth.stream().inject(std::span{&open, 1});
            }
            return made;
        };
        parser.bind("\\multirow", [spanned](syntax::Parser& parser) -> syntax::Node* {
            const memory::Location origin = parser.mouth.lookahead().location;
            const Span made = spanned(parser);
            return parser.arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, made.nodes);
        });

        parser.bind("\\tabular", [this, &context, spanned](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            if (openings.empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Environment, origin,
                                         "\\tabular outside a tabular block");
                return directive(arena, nullptr, origin, true);
            }
            // Taken off now, before a cell opens a table of its own.
            const Opening opening = std::move(openings.back());
            openings.pop_back();

            // Which way the text read where the table began: a table in
            // Arabic or Hebrew runs right to left, its first column at the
            // right, as one set under polyglossia does.
            const bool reversed = context.reversed;

            syntax::Lexicon& lexicon = mouth.lexicon;
            const syntax::Symbol ampersand = column;
            const syntax::Symbol newline = lexicon.intern("\\\\");
            const syntax::Symbol alternative = lexicon.intern("\\tabularnewline");
            const syntax::Symbol end = lexicon.intern("\\endtabular");
            const syntax::Symbol hline = lexicon.intern("\\hline");
            const syntax::Symbol toprule = lexicon.intern("\\toprule");
            const syntax::Symbol midrule = lexicon.intern("\\midrule");
            const syntax::Symbol bottomrule = lexicon.intern("\\bottomrule");
            const syntax::Symbol cmidrule = lexicon.intern("\\cmidrule");
            const syntax::Symbol cline = lexicon.intern("\\cline");
            const syntax::Symbol addlinespace = lexicon.intern("\\addlinespace");
            const syntax::Symbol multicolumn = lexicon.intern("\\multicolumn");
            const syntax::Symbol rowcolor = lexicon.intern("\\rowcolor");
            const syntax::Symbol cellcolor = lexicon.intern("\\cellcolor");
            const syntax::Symbol columncolor = lexicon.intern("\\columncolor");
            const syntax::Symbol kill = lexicon.intern("\\kill");
            const syntax::Symbol firsthead = lexicon.intern("\\endfirsthead");
            const syntax::Symbol head = lexicon.intern("\\endhead");
            const syntax::Symbol foot = lexicon.intern("\\endfoot");
            const syntax::Symbol lastfoot = lexicon.intern("\\endlastfoot");
            const syntax::Symbol multirow = lexicon.intern("\\multirow");
            const std::array<syntax::Symbol, 21> stops{
                ampersand, newline, alternative, end, hline, toprule, midrule, bottomrule,
                cmidrule, cline, addlinespace, multicolumn, rowcolor, cellcolor, columncolor, kill,
                firsthead, head, foot, lastfoot, multirow,
            };

            const typography::Font* body = context.selection.text();
            const float size = body ? body->size() : 10.0f;
            const float ex = size * 0.43f;              // Latin Modern's x-height
            // \tabcolsep, \arrayrulewidth and \arraystretch, as the document
            // has them where the table is set: LaTeX's own when it has not
            // changed them.
            const auto length = [&context, &lexicon](const std::string_view name, const float fallback) {
                const auto slot = context.registers.target(lexicon.intern(name));
                return slot ? static_cast<float>(context.registers.get(slot->type, slot->slot)) / 65536.0f : fallback;
            };
            float stretch = 1.0f;
            if (const syntax::Mouth::Macro* written = mouth.macro(lexicon.intern("\\arraystretch"))) {
                std::string digits;
                for (const syntax::Token& token : written->body) digits += token.text;
                if (std::from_chars(digits.data(), digits.data() + digits.size(), stretch).ec != std::errc{}) {
                    stretch = 1.0f;
                }
            }
            const float padding = length("\\tabcolsep", 6.0f);
            const float thin = length("\\arrayrulewidth", 0.4f);
            const float strut = size * 1.2f * stretch;   // a baseline

            /// A rule between two rows, across some of the columns.
            struct Stroke {
                float thickness{0.4f};     ///< How heavy.
                float above{0.0f};         ///< Space above it.
                float below{0.0f};         ///< Space below it.
                std::size_t from{0};       ///< First column it spans.
                std::size_t to{0};         ///< One past the last; 0 for every column.
                float trim{0.0f};          ///< How far in from each end it stops.
            };
            /// One cell as read.
            struct Cell {
                std::vector<syntax::Node*> nodes{};    ///< What it holds.
                std::size_t span{1};                   ///< How many columns it covers.
                std::optional<Column> format{};        ///< Its own column, from \\multicolumn.
                std::optional<layout::Node::Color> fill{};   ///< Painted behind it: `\\cellcolor`.
                bool begun{false};                     ///< Its column's `>{...}` read already.
                std::size_t rows{1};                   ///< How many rows it spans, from \\multirow.
                char place{'c'};                       ///< Where in them it stands: t, c or b.
            };
            /// Which part of a long table a row is in: its body, or the rows
            /// longtable sets above it on its first page and on the others,
            /// and below it on every page but the last and on the last.
            enum class Part : std::uint8_t { Body, First, Head, Foot, Last };
            /// One row as read, with the rules above it.
            struct Line {
                std::vector<Cell> cells{};       ///< Its cells, left to right.
                std::vector<Stroke> strokes{};   ///< Rules between it and the row above.
                float gap{0.0f};                 ///< Extra space below it, from `\\[length]`.
                std::optional<layout::Node::Color> fill{};   ///< Painted behind it: `\\rowcolor`.
                bool hidden{false};              ///< Ended by `\\kill`: measured, and not drawn.
                Part part{Part::Body};           ///< The part of the table it is in.
            };

            std::vector<Line> lines;
            Line current;
            Cell cell;
            // The rows up to the last of longtable's marks, which said what
            // part they are, and which parts were given at all.
            std::size_t marked = 0;
            std::array<bool, 5> given{};
            std::vector<Stroke> trailing;
            bool closed = false;

            // colortbl's colors, `[model]{spec}` as xcolor writes one, and
            // the overhangs `[left][right]` \\rowcolor may take, which a row
            // here does not reach past.
            const auto paint = [&mouth, &context]() -> layout::Node::Color {
                std::string model;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    model += token.text;
                }
                const std::string spec = syntax::Argument::text(mouth);
                const graphics::Color color = Colors::resolve(
                    model.empty() ? spec : Colors::convert(model, spec).value_or(spec), context.variables);
                while (mouth.lookahead().is('[')) {
                    static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                }
                return {color.r, color.g, color.b, color.alpha};
            };

            // A cell with nothing in it but the blanks around the source's `&`.
            // An instruction -- a `>{\\centering}` read into the empty row
            // after a last `\\\\` -- sets nothing, and leaves a cell blank.
            const auto blank = [](const std::vector<syntax::Node*>& nodes) {
                return std::ranges::all_of(nodes, [](const syntax::Node* node) {
                    if (!node || node->type == syntax::Node::Type::Paragraph) return true;
                    if (node->type == syntax::Node::Type::Directive) {
                        const auto* inside = static_cast<const layout::Node*>(node->directive);
                        return !inside || inside->type == layout::Node::Type::Directive;
                    }
                    return node->type == syntax::Node::Type::Text &&
                           node->value.find_first_not_of(" \t\n") == std::string_view::npos;
                });
            };

            // A rule's optional thickness, `\toprule[1pt]`.
            const auto heaviness = [&](const float fallback) {
                if (!mouth.lookahead().is('[')) return fallback;
                std::string text;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    text += token.text;
                }
                const float read = measure(text, mouth, context);
                return read > 0.0f ? read : fallback;
            };

            // The columns a partial rule spans, `{2-3}`, counted from one.
            const auto range = [&](Stroke stroke) {
                const std::string text = syntax::Argument::text(mouth);
                const std::size_t dash = text.find('-');
                std::size_t first = 0;
                std::size_t last = 0;
                std::from_chars(text.data(), text.data() + (dash == std::string::npos ? text.size() : dash), first);
                if (dash != std::string::npos) {
                    std::from_chars(text.data() + dash + 1, text.data() + text.size(), last);
                } else {
                    last = first;
                }
                stroke.from = first > 0 ? first - 1 : 0;
                stroke.to = std::max(last, stroke.from + 1);
                return stroke;
            };

            while (lines.size() < limit) {
                // A cell starts with what its column's `>{...}` declared.
                if (!cell.begun) {
                    cell.begun = true;
                    std::size_t at = 0;
                    for (const Cell& item : current.cells) at += item.span;
                    if (at < opening.columns.size() && !opening.columns[at].head.empty()) {
                        mouth.ingest(context.arena.copy(opening.columns[at].head + " "));
                    }
                }

                // Each cell is a group of its own: a style chosen in it ends at
                // its edge, as it would at a brace.
                const typography::Font* text = context.selection.text();
                const typography::Font* formula = context.selection.formula();
                syntax::Symbol matched = syntax::none;
                const memory::Slice<syntax::Node*> read = parser.parse(0, stops, matched);
                stamp(read, context);
                context.selection.text(text);
                context.selection.formula(formula);
                cell.nodes.insert(cell.nodes.end(), read.begin(), read.end());

                // A rule stands where a row is about to start, and belongs above it.
                std::vector<Stroke>& strokes = current.strokes;
                const auto doubled = [&strokes] { return !strokes.empty() && strokes.back().thickness > 0.0f; };
                if (matched == hline) {
                    strokes.push_back(Stroke{.thickness = thin, .above = doubled() ? 2.0f : 0.0f});
                    continue;
                }
                if (matched == toprule) {
                    strokes.push_back(Stroke{.thickness = heaviness(size * 0.08f), .below = ex * 0.65f});
                    continue;
                }
                if (matched == midrule) {
                    strokes.push_back(Stroke{.thickness = heaviness(size * 0.05f), .above = ex * 0.4f, .below = ex * 0.65f});
                    continue;
                }
                if (matched == bottomrule) {
                    strokes.push_back(Stroke{.thickness = heaviness(size * 0.08f), .above = ex * 0.4f});
                    continue;
                }
                if (matched == cmidrule) {
                    const float weight = heaviness(size * 0.03f);
                    float trim = 0.0f;
                    if (mouth.lookahead().is('(')) {
                        for (syntax::Token token = mouth.read(); !token.empty() && !token.is(')'); token = mouth.read()) {
                            if (!token.is('(')) trim = size * 0.25f;
                        }
                    }
                    strokes.push_back(range(Stroke{.thickness = weight, .above = ex * 0.4f, .below = ex * 0.65f,
                                                   .trim = trim}));
                    continue;
                }
                if (matched == cline) {
                    strokes.push_back(range(Stroke{.thickness = thin}));
                    continue;
                }
                if (matched == addlinespace) {
                    strokes.push_back(Stroke{.thickness = 0.0f, .above = heaviness(size * 0.5f)});
                    continue;
                }

                // `\multicolumn{2}{c}{text}`: a cell over several columns, with
                // a column of its own for how it sits in them.
                if (matched == multicolumn) {
                    std::size_t span = 1;
                    const std::string count = syntax::Argument::text(mouth);
                    std::from_chars(count.data(), count.data() + count.size(), span);
                    const std::vector<Column> format = preamble(mouth.argument({}, 0), &customs);

                    syntax::Token open = mouth.read();
                    while (open.category == syntax::Catcodes::Category::Space) open = mouth.read();
                    if (open.is(syntax::Catcodes::Category::Group, '{')) {
                        const Selection restore = context.selection;
                        mouth.push(syntax::semantics::Scope::Type::Group);
                        const memory::Slice<syntax::Node*> inside = parser.parse('}');
                        mouth.pop(syntax::semantics::Scope::Type::Group);
                        stamp(inside, context);
                        context.selection = restore;
                        cell.nodes.insert(cell.nodes.end(), inside.begin(), inside.end());
                    } else if (!open.empty()) {
                        mouth.stream().inject(std::span{&open, 1});
                    }
                    cell.span = std::max<std::size_t>(span, 1);
                    if (!format.empty()) cell.format = format.front();
                    continue;
                }

                if (matched == multirow) {
                    const Span made = spanned(parser);
                    cell.nodes.insert(cell.nodes.end(), made.nodes.begin(), made.nodes.end());
                    cell.rows = std::max<std::size_t>(made.rows, 1);
                    cell.place = made.place;
                    continue;
                }

                if (matched == rowcolor) {
                    current.fill = paint();
                    continue;
                }
                if (matched == cellcolor || matched == columncolor) {
                    cell.fill = paint();
                    continue;
                }

                if (matched == ampersand) {
                    current.cells.push_back(std::move(cell));
                    cell = Cell{};
                    continue;
                }

                if (matched == newline || matched == alternative || matched == kill) {
                    current.cells.push_back(std::move(cell));
                    cell = Cell{};
                    current.hidden = matched == kill;

                    // `\\*` forbids a page break nothing would take here, and
                    // `\\[4pt]` asks for more room below this row.
                    if (mouth.lookahead().is('*')) mouth.read();
                    if (mouth.lookahead().is('[')) {
                        std::string written;
                        for (const syntax::Token& token :
                             mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                            written += token.text;
                        }
                        current.gap = measure(written, mouth, context);
                    }
                    lines.push_back(std::move(current));
                    current = Line{};
                    continue;
                }

                // longtable's marks: the rows since the last mark are its first
                // head, its head, its foot or its last foot. A row the mark
                // ends without a `\\\\` is one of them; rules alone before it,
                // `\\hline\\endfoot`, close the part.
                if (matched == firsthead || matched == head || matched == foot || matched == lastfoot) {
                    if (current.cells.empty() && cell.span == 1 && blank(cell.nodes)) {
                        current.hidden = true;
                    } else {
                        current.cells.push_back(std::move(cell));
                    }
                    cell = Cell{};
                    lines.push_back(std::move(current));
                    current = Line{};
                    const Part part = matched == firsthead ? Part::First
                                      : matched == head    ? Part::Head
                                      : matched == foot    ? Part::Foot
                                                           : Part::Last;
                    for (; marked < lines.size(); ++marked) lines[marked].part = part;
                    given[static_cast<std::size_t>(part)] = true;
                    continue;
                }

                // The end: the marker `\end{tabular}` left, or the end of the
                // input. A last `\\` leaves an empty row behind it, whose rules
                // are the table's last.
                closed = matched == end;
                if (current.cells.empty() && cell.span == 1 && blank(cell.nodes)) {
                    trailing = std::move(current.strokes);
                } else {
                    current.cells.push_back(std::move(cell));
                    lines.push_back(std::move(current));
                }
                break;
            }

            if (!closed) {
                tracebacks.emplace_back(syntax::Traceback::Type::Environment, origin,
                                         "A tabular ran past the end of the document");
            }

            // The columns: the preamble's, and as many plain ones again as the
            // widest row needs beyond it.
            std::vector<Column> columns = opening.columns;
            std::size_t count = columns.size();
            for (const Line& line : lines) {
                std::size_t taken = 0;
                for (const Cell& item : line.cells) taken += item.span;
                count = std::max(count, taken);
            }
            if (count == 0) return directive(arena, nullptr, origin, true);
            columns.resize(count, Column{});

            // Right to left, the table is its mirror: the columns in the
            // other order, each rule and each padding on the other side, a
            // row's cells from the right -- a short row's empty ones at its
            // left -- and a rule over some of the columns over their mirror.
            if (reversed) {
                // The vertical rules on each boundary between columns, from
                // the table's left edge, 0, to its right, `count`; boundary k
                // is boundary count - k in the mirror.
                std::vector<int> rules(count + 1, 0);
                for (std::size_t index = 0; index < count; ++index) {
                    rules[index] += columns[index].before;
                    rules[index + 1] += columns[index].after;
                }
                std::ranges::reverse(columns);
                for (std::size_t index = 0; index < count; ++index) {
                    Column& format = columns[index];
                    std::swap(format.opened, format.closed);
                    format.before = rules[count - index];
                    format.after = 0;
                }
                columns.back().after = rules[0];
                for (Line& line : lines) {
                    std::size_t taken = 0;
                    for (const Cell& item : line.cells) taken += item.span;
                    for (; taken < count; ++taken) line.cells.emplace_back();
                    std::ranges::reverse(line.cells);
                    for (Stroke& rule : line.strokes) {
                        if (rule.to == 0) continue;
                        const std::size_t from = count - std::min(rule.to, count);
                        rule.to = count - rule.from;
                        rule.from = from;
                    }
                }
            }

            const layout::Document::Configuration& page = context.document.configuration;

            // Pass one: every cell's material, and how wide each column must be.
            struct Piece {
                memory::Slice<layout::Node*> material{};   ///< The cell's boxes.
                std::size_t first{0};                      ///< The column it starts in.
                std::size_t span{1};                       ///< How many it covers.
                const Column* format{nullptr};             ///< How it sits in them.
                layout::Node* box{nullptr};                ///< Built in pass two.
                std::optional<layout::Node::Color> fill{}; ///< Painted behind it.
                std::size_t rows{1};                       ///< How many rows it spans.
                char place{'c'};                           ///< Where in them it stands.
            };
            std::vector<std::vector<Piece>> pieces(lines.size());
            std::vector<float> widths(count, 0.0f);
            for (std::size_t index = 0; index < count; ++index) {
                if (!columns[index].width.empty()) widths[index] = measure(columns[index].width, mouth, context);
            }

            for (std::size_t line = 0; line < lines.size(); ++line) {
                std::size_t at = 0;
                for (Cell& item : lines[line].cells) {
                    if (at >= count) break;

                    // The blanks the source leaves around a `&` are layout, and
                    // not part of the cell.
                    std::vector<syntax::Node*>& nodes = item.nodes;
                    std::erase_if(nodes, [](const syntax::Node* node) {
                        return !node || node->type == syntax::Node::Type::Paragraph;
                    });
                    if (!nodes.empty() && nodes.front()->type == syntax::Node::Type::Text) {
                        std::string_view& value = nodes.front()->value;
                        value.remove_prefix(std::min(value.find_first_not_of(" \t\n"), value.size()));
                    }
                    if (!nodes.empty() && nodes.back()->type == syntax::Node::Type::Text) {
                        std::string_view& value = nodes.back()->value;
                        const std::size_t last = value.find_last_not_of(" \t\n");
                        value = last == std::string_view::npos ? std::string_view{} : value.substr(0, last + 1);
                    }

                    std::vector<layout::Node*> gathered;
                    for (const syntax::Node* node : nodes) compose(gathered, node, context);
                    const memory::Slice<layout::Node*> material = arena.allocate<layout::Node*>(gathered.size());
                    std::ranges::copy(gathered, material.begin());

                    const std::size_t span = std::min(item.span, count - at);
                    const Column* format = item.format ? &*item.format : &columns[at];
                    pieces[line].push_back(
                        Piece{.material = material, .first = at, .span = span, .format = format, .fill = item.fill,
                              .rows = item.rows, .place = item.place});

                    if (span == 1 && format->width.empty() && !format->stretch) {
                        float natural = 0.0f;
                        for (const layout::Node* node : material) natural += layout::Line::advance(node);
                        widths[at] = std::max(widths[at], natural);
                    }
                    at += span;
                }
            }

            // tabularx: what the fixed columns leave of the width asked for is
            // shared out among the `X` columns.
            const auto edge = [&](const std::size_t index) {
                const Column& format = columns[index];
                return (format.opened ? padding : 0.0f) + (format.closed ? padding : 0.0f) +
                       thin * static_cast<float>(format.before) +
                       (index + 1 == count ? thin * static_cast<float>(format.after) : 0.0f);
            };
            const std::size_t stretched = static_cast<std::size_t>(
                std::ranges::count_if(columns, [](const Column& format) { return format.stretch; }));
            if (stretched > 0) {
                const float target = opening.width.empty() ? breadth(context)
                                                           : measure(opening.width, mouth, context);
                float fixed = 0.0f;
                for (std::size_t index = 0; index < count; ++index) {
                    fixed += edge(index) + (columns[index].stretch ? 0.0f : widths[index]);
                }
                const float share = std::max((target - fixed) / static_cast<float>(stretched), size);
                for (std::size_t index = 0; index < count; ++index) {
                    if (columns[index].stretch) widths[index] = share;
                }
            }

            // The room a cell over several columns has: their widths, and the
            // padding and rules between them.
            const auto room = [&](const std::size_t first, const std::size_t span) {
                float total = 0.0f;
                for (std::size_t index = first; index < first + span; ++index) {
                    total += widths[index];
                    if (index > first) {
                        total += thin * static_cast<float>(columns[index].before) +
                                 (columns[index - 1].closed ? padding : 0.0f) + (columns[index].opened ? padding : 0.0f);
                    }
                }
                return total;
            };

            // Pass two: each cell's box, a paragraph for a column of fixed width.
            for (std::vector<Piece>& line : pieces) {
                for (Piece& piece : line) {
                    const bool wrapped = !piece.format->width.empty() || piece.format->stretch;
                    if (wrapped) {
                        const float measure = piece.span == 1 ? widths[piece.first] : room(piece.first, piece.span);

                        // Set as a `\\centering` or `\\raggedright` in the cell
                        // says -- the one a `>{...}` put there, most often --
                        // or tabu's `X[c]` and `X[r]`.
                        const char side = piece.format->align;
                        layout::Node::Justification setting = side == 'c'   ? layout::Node::Justification::Center
                                                              : side == 'r' ? layout::Node::Justification::Right
                                                                            : layout::Node::Justification::Full;
                        for (const layout::Node* node : piece.material) {
                            if (node && node->type == layout::Node::Type::Directive &&
                                node->directive().command == layout::Node::Directive::Command::Align) {
                                setting = node->directive().justification;
                            }
                        }
                        auto* paragraph = arena.compose<layout::Paragraph>(arena, piece.material, setting, 0.0f, 0.0f,
                                                                           reversed);
                        paragraph->layout(arena, measure, page.leading);
                        layout::Node* broken = paragraph->node();
                        if (broken && broken->type == layout::Node::Type::Box && !broken->box().list.empty()) {
                            // Hung from its first line's baseline, as `p` sets a
                            // cell: the first line level with the row's others.
                            const layout::Node* first = broken->box().list[0];
                            const float top = first && first->type == layout::Node::Type::Box ? first->box().height
                                                                                                  : size * 0.7f;
                            // A column hangs from its top edge, so it is raised by
                            // that line's height to bring the line's baseline up.
                            layout::Node::Box shape = broken->box();
                            shape.shift = -top;
                            broken->box(shape);
                            const memory::Slice<layout::Node*> only = arena.allocate<layout::Node*>(1);
                            only[0] = broken;
                            piece.box = layout::Line::horizontal(arena, only, 0.0f);
                        }
                    } else {
                        // One line, put in the order it is drawn in: Arabic in
                        // a cell reads right to left in any table.
                        piece.box = layout::Line::horizontal(arena, piece.material, 0.0f);
                        layout::Line::reorder(arena, piece.box, reversed);
                    }

                    // A cell over several columns wider than they are widens the
                    // last of them.
                    if (piece.box && piece.span > 1) {
                        const float need = piece.box->box().width - room(piece.first, piece.span);
                        if (need > 0.0f) widths[piece.first + piece.span - 1] += need;
                    }
                }
            }

            // Where each column's padded area starts and ends, left to right.
            std::vector<float> starts(count, 0.0f);
            std::vector<float> ends(count, 0.0f);
            float across = 0.0f;
            for (std::size_t index = 0; index < count; ++index) {
                across += thin * static_cast<float>(columns[index].before);
                starts[index] = across;
                across += (columns[index].opened ? padding : 0.0f) + widths[index] +
                          (columns[index].closed ? padding : 0.0f);
                ends[index] = across;
            }
            across += thin * static_cast<float>(columns.back().after);
            const float total = across;

            const auto kern = [&arena](const float width) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Kern);
                node->kern({.width = width});
                return node;
            };
            const auto bar = [&arena, thin](const float height, const float depth) {
                auto* node = arena.compose<layout::Node>(layout::Node::Type::Rule);
                node->rule({.width = thin, .height = height, .depth = depth});
                return node;
            };

            // A rule between rows, as the line of the column it is drawn in.
            const auto stroke = [&](const Stroke& rule, std::vector<layout::Node*>& column) {
                if (rule.above > 0.0f) column.push_back(kern(rule.above));
                if (rule.thickness > 0.0f) {
                    const float left = rule.to == 0 ? 0.0f : starts[std::min(rule.from, count - 1)] + rule.trim;
                    const float right = rule.to == 0 ? total : ends[std::min(rule.to, count) - 1] - rule.trim;
                    auto* line = arena.compose<layout::Node>(layout::Node::Type::Rule);
                    line->rule({.width = std::max(right - left, 0.0f), .height = rule.thickness});
                    const memory::Slice<layout::Node*> parts = arena.allocate<layout::Node*>(3);
                    parts[0] = kern(left);
                    parts[1] = line;
                    parts[2] = kern(total - right);
                    column.push_back(layout::Line::horizontal(arena, parts, 0.0f));
                }
                if (rule.below > 0.0f) column.push_back(kern(rule.below));
            };

            // Each row, with the rules above it and the room below it, in the
            // part of the table it was written in.
            std::array<std::vector<layout::Node*>, 5> stacks;

            // How tall and deep each row stands: its tallest cell, and never
            // less than a strut -- a cell over several rows aside, which
            // stands in the rows it spans rather than stretching its first.
            std::vector<float> heights(lines.size(), strut * 0.7f);
            std::vector<float> depths(lines.size(), strut * 0.3f);
            for (std::size_t line = 0; line < lines.size(); ++line) {
                for (const Piece& piece : pieces[line]) {
                    if (!piece.box || piece.rows > 1) continue;
                    heights[line] = std::max(heights[line], piece.box->box().height);
                    depths[line] = std::max(depths[line], piece.box->box().depth);
                }
            }

            // A cell over several rows, moved down to stand in their middle,
            // or with its foot on the last one's for `[b]`: as far below its
            // own row's baseline as the rows it spans, and the rules and the
            // room between them, put it.
            for (std::size_t line = 0; line < lines.size(); ++line) {
                for (Piece& piece : pieces[line]) {
                    if (!piece.box || piece.rows < 2 || piece.place == 't') continue;
                    const std::size_t last = std::min(line + piece.rows, lines.size()) - 1;
                    float span = 0.0f;   // from the top of its row to the foot of the last
                    for (std::size_t below = line; below <= last; ++below) {
                        if (below > line) {
                            for (const Stroke& rule : lines[below].strokes) {
                                span += rule.above + rule.thickness + rule.below;
                            }
                        }
                        if (!lines[below].hidden) span += heights[below] + depths[below];
                        if (below < last) span += lines[below].gap;
                    }
                    layout::Node::Box shape = piece.box->box();
                    shape.shift += piece.place == 'b'
                                       ? span - shape.depth - heights[line]
                                       : (span - shape.height - shape.depth) * 0.5f + shape.height - heights[line];
                    piece.box->box(shape);
                }
            }

            for (std::size_t line = 0; line < lines.size(); ++line) {
                std::vector<layout::Node*>& stack = stacks[static_cast<std::size_t>(lines[line].part)];
                for (const Stroke& rule : lines[line].strokes) stroke(rule, stack);
                if (lines[line].hidden) continue;
                const float height = heights[line];
                const float depth = depths[line];

                std::vector<layout::Node*> parts;

                // The row's color and its cells' painted first, under the
                // rules and the text: the row across the whole table, a cell
                // across its padded area. The pen goes back to the row's
                // start after them.
                float painted = 0.0f;
                const auto fill = [&](const float from, const float to, const layout::Node::Color color) {
                    parts.push_back(kern(from - painted));
                    auto* area = arena.compose<layout::Node>(layout::Node::Type::Rule);
                    area->rule({.width = to - from, .height = height, .depth = depth, .color = color});
                    parts.push_back(area);
                    painted = to;
                };
                if (lines[line].fill) fill(0.0f, total, *lines[line].fill);
                for (const Piece& piece : pieces[line]) {
                    if (piece.fill) fill(starts[piece.first], ends[piece.first + piece.span - 1], *piece.fill);
                }
                if (painted != 0.0f) parts.push_back(kern(-painted));

                std::size_t at = 0;
                float drawn = 0.0f;
                const auto advance = [&](const std::size_t index) {
                    // The rules before one column, and its padded area.
                    for (int rule = 0; rule < columns[index].before; ++rule) parts.push_back(bar(height, depth));
                    drawn += thin * static_cast<float>(columns[index].before);
                };

                for (const Piece& piece : pieces[line]) {
                    for (; at < piece.first; ++at) {
                        advance(at);
                        parts.push_back(kern(ends[at] - starts[at]));
                        drawn = ends[at];
                    }
                    advance(piece.first);
                    const std::size_t last = piece.first + piece.span - 1;
                    const float left = starts[piece.first] + (columns[piece.first].opened ? padding : 0.0f);
                    const float right = ends[last] - (columns[last].closed ? padding : 0.0f);
                    const float width = piece.box ? piece.box->box().width : 0.0f;
                    const float slack = std::max(right - left - width, 0.0f);
                    const char align = piece.format->align;
                    const float before = align == 'r' ? slack : align == 'c' ? slack * 0.5f : 0.0f;

                    parts.push_back(kern(left - drawn + before));
                    if (piece.box) parts.push_back(piece.box);
                    parts.push_back(kern(ends[last] - left - before - width));
                    drawn = ends[last];
                    at = last + 1;
                }
                for (; at < count; ++at) {
                    advance(at);
                    parts.push_back(kern(ends[at] - starts[at]));
                    drawn = ends[at];
                }
                for (int rule = 0; rule < columns.back().after; ++rule) parts.push_back(bar(height, depth));

                const memory::Slice<layout::Node*> material = arena.allocate<layout::Node*>(parts.size());
                std::ranges::copy(parts, material.begin());
                layout::Node* built = layout::Line::horizontal(arena, material, 0.0f);
                layout::Node::Box shape = built->box();
                shape.height = height;
                shape.depth = depth;
                built->box(shape);
                stack.push_back(built);

                if (lines[line].gap > 0.0f) stack.push_back(kern(lines[line].gap));
            }
            std::vector<layout::Node*>& rows = stacks[static_cast<std::size_t>(Part::Body)];
            for (const Stroke& rule : trailing) stroke(rule, rows);

            // The rows above the body on the first page and below it on the
            // last: longtable's \\endfirsthead and \\endlastfoot where they
            // were given, and its \\endhead and \\endfoot where they were not.
            const auto part = [&](const Part chosen, const Part otherwise) -> const std::vector<layout::Node*>& {
                return stacks[static_cast<std::size_t>(given[static_cast<std::size_t>(chosen)] ? chosen : otherwise)];
            };
            const std::vector<layout::Node*>& opener = part(Part::First, Part::Head);
            const std::vector<layout::Node*>& closer = part(Part::Last, Part::Foot);

            Logger::log(Logger::Type::Layout, Logger::Level::Debug,
                        "Tabulated {} rows over {} columns", lines.size(), count);

            // A long table is set in the column a row at a time, so a page
            // may end between any two: across the line, centred or at the
            // side its `[l]` or `[r]` asks for, \\LTpre above it and \\LTpost
            // below. Between its first head and its last foot a Repeat gives
            // the pager the rows each page it breaks onto opens with, and
            // each page it breaks off closes with.
            if (opening.breakable) {
                const float measure = breadth(context);
                const auto set = [&](layout::Node* each) {
                    if (each->type != layout::Node::Type::Box) return each;
                    const memory::Slice<layout::Node*> line = arena.allocate<layout::Node*>(3);
                    std::size_t filled = 0;
                    const auto fill = [&arena] {
                        auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
                        glue->glue({.stretch = 1.0f, .expand = layout::Node::Order::Fil});
                        return glue;
                    };
                    if (opening.position != 'l') line[filled++] = fill();
                    line[filled++] = each;
                    if (opening.position != 'r') line[filled++] = fill();
                    return layout::Line::horizontal(arena, memory::Slice{line.data, filled}, measure);
                };
                const auto repeated = [&](const Part chosen) -> layout::Node* {
                    const std::vector<layout::Node*>& chunk = stacks[static_cast<std::size_t>(chosen)];
                    if (chunk.empty()) return nullptr;
                    const memory::Slice<layout::Node*> list = arena.allocate<layout::Node*>(chunk.size());
                    for (std::size_t at = 0; at < chunk.size(); ++at) list[at] = set(chunk[at]);
                    return layout::Line::vertical(arena, list, 0.0f);
                };

                std::vector<syntax::Node*> out;
                const auto skip = [&] {
                    auto* glue = arena.compose<layout::Node>(layout::Node::Type::Glue);
                    glue->glue({.width = size * 1.2f, .stretch = size * 0.4f, .shrink = size * 0.4f});
                    out.push_back(directive(arena, glue, origin, true));
                };
                const auto mark = [&](layout::Node* above, layout::Node* below) {
                    auto* order = arena.compose<layout::Node>(layout::Node::Type::Directive);
                    order->directive({.command = layout::Node::Directive::Command::Repeat, .head = above, .foot = below});
                    out.push_back(directive(arena, order, origin));
                };
                // Each row stands where the one above leaves it, with no
                // space between them for their baselines: a rule of nothing
                // before each, as TeX's \\nointerlineskip, stops the space.
                const auto put = [&](layout::Node* each) {
                    auto* stop = arena.compose<layout::Node>(layout::Node::Type::Rule);
                    stop->rule({});
                    out.push_back(directive(arena, stop, origin, true));
                    out.push_back(directive(arena, set(each), origin, true));
                };
                skip();
                for (layout::Node* each : opener) put(each);
                mark(repeated(Part::Head), repeated(Part::Foot));
                for (layout::Node* each : rows) put(each);
                mark(nullptr, nullptr);
                for (layout::Node* each : closer) put(each);
                skip();

                const memory::Slice<syntax::Node*> nodes = arena.allocate<syntax::Node*>(out.size());
                std::ranges::copy(out, nodes.begin());
                return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, nodes);
            }

            // Any other is one box: its first head, its body and its last
            // foot, as a long table set whole is too.
            std::vector<layout::Node*> stack(opener);
            stack.insert(stack.end(), rows.begin(), rows.end());
            stack.insert(stack.end(), closer.begin(), closer.end());
            if (stack.empty()) return directive(arena, nullptr, origin, true);

            const memory::Slice<layout::Node*> down = arena.allocate<layout::Node*>(stack.size());
            std::ranges::copy(stack, down.begin());
            layout::Node* table = layout::Line::vertical(arena, down, 0.0f);

            // A column hangs from its top, so it is raised to where its
            // baseline falls: `[t]` its first row's, `[b]` its last row's,
            // and by default the formula axis through its middle, as LaTeX
            // sets a table in a line or in another table's cell.
            layout::Node::Box shape = table->box();
            const auto rim = [](const layout::Node* row, const bool top) {
                if (!row || row->type != layout::Node::Type::Box) return 0.0f;
                return top ? row->box().height : row->box().depth;
            };
            shape.shift = opening.position == 't'   ? -rim(stack.front(), true)
                          : opening.position == 'b' ? rim(stack.back(), false) - shape.depth
                                                    : -(shape.depth * 0.5f + size * 0.25f);
            table->box(shape);
            const memory::Slice<layout::Node*> only = arena.allocate<layout::Node*>(1);
            only[0] = table;

            // A box in the line, as LaTeX's tabular is: beside the words
            // around it, or a centred line of its own inside `center`.
            return directive(arena, layout::Line::horizontal(arena, only, 0.0f), origin, false);
        });

        // revtex's ruledtabular: the tabular inside it, set as it stands.
        context.blocks.watch("ruledtabular", [](syntax::Mouth&) {}, {});

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound alignment primitives");
    }

}
