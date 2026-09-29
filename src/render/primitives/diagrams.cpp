/// @file
/// @brief Diagrams implementation: tikz-cd, xy-pic and amscd diagrams,
///        quantikz and Qcircuit circuits, and forest and qtree trees, each
///        written out as the TikZ picture it draws.
#include "render/primitives/diagrams.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace render::primitives {

    using Category = syntax::Catcodes::Category;

    /// @brief Text without the blanks around it.
    [[nodiscard]] static std::string_view trim(std::string_view text) noexcept {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.remove_suffix(1);
        return text;
    }

    /// @brief Where the group opening at @p at closes -- braces, brackets or
    ///        parentheses, those nested inside counted -- or the text's end.
    [[nodiscard]] static std::size_t closing(const std::string_view text, const std::size_t at) noexcept {
        const char open = text[at];
        const char shut = open == '{' ? '}' : open == '[' ? ']' : ')';
        int depth = 0;
        int braces = 0;
        for (std::size_t index = at; index < text.size(); ++index) {
            const char letter = text[index];
            if (open != '{') {
                if (letter == '{') ++braces;
                if (letter == '}') --braces;
                if (braces > 0 || letter == '}') continue;
            }
            if (letter == open) ++depth;
            if (letter == shut && --depth == 0) return index;
        }
        return text.size();
    }

    Diagrams::Diagrams(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\xymatrix");
        lexicon.intern("\\Qcircuit");
        lexicon.intern("\\Tree");
    }

    std::string Diagrams::body(syntax::Mouth& mouth, const std::string_view name) {
        std::string text;
        int depth = 0;
        for (syntax::Token token = mouth.read(); !token.empty(); token = mouth.read()) {
            if (token.is(Category::Group, '{')) ++depth;
            if (token.is(Category::Group, '}')) --depth;
            if (depth == 0 && token.text == "\\end" && mouth.lookahead().is('{')) {
                std::string called;
                for (std::size_t index = 1; index < 24; ++index) {
                    const syntax::Token letter = mouth.lookahead(index);
                    if (letter.empty() || letter.is('}')) break;
                    called += letter.text;
                }
                if (called == name) {
                    mouth.stream().inject(std::span{&token, 1});
                    break;
                }
            }
            text += token.text;
            if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
        }
        return text;
    }

    std::vector<std::vector<std::string>> Diagrams::grid(const std::string_view text, const std::string_view separator) {
        std::vector<std::vector<std::string>> rows(1);
        std::size_t begin = 0;
        int depth = 0;
        for (std::size_t at = 0; at < text.size(); ++at) {
            if (text[at] == '{') ++depth;
            if (text[at] == '}') --depth;
            if (depth != 0) continue;
            if (text.substr(at).starts_with("\\\\")) {
                rows.back().emplace_back(trim(text.substr(begin, at - begin)));
                rows.emplace_back();
                at += 2;
                while (at < text.size() && text[at] == ' ') ++at;
                // The room a `\\[4pt]` asks for is the diagram's own spacing here.
                if (at < text.size() && text[at] == '[') at = closing(text, at) + 1;
                begin = at;
                --at;
            } else if (text.substr(at).starts_with(separator) && (separator != "&" || at == 0 || text[at - 1] != '\\')) {
                rows.back().emplace_back(trim(text.substr(begin, at - begin)));
                at += separator.size() - 1;
                begin = at + 1;
            }
        }
        rows.back().emplace_back(trim(text.substr(std::min(begin, text.size()))));
        // A last `\\` leaves an empty row, which is none.
        if (rows.size() > 1 && rows.back().size() == 1 && rows.back().front().empty()) rows.pop_back();
        return rows;
    }

    std::string Diagrams::picture(const Diagram& diagram, const memory::Location origin) const {
        // Its middle, a little under, on the baseline: the formula's axis,
        // where tikz-cd centres a diagram.
        std::string out = std::format("\\begin{{tikzpicture}}[baseline=([yshift=-0.25em]cd.center)]"
                                      "\\matrix[matrix of math nodes, column sep={}, row sep={}, nodes={{{}}}] (cd) {{",
                                      diagram.columns, diagram.rows, diagram.nodes);
        for (std::size_t row = 0; row < diagram.cells.size(); ++row) {
            for (std::size_t column = 0; column < diagram.cells[row].size(); ++column) {
                if (column > 0) out += " & ";
                out += diagram.cells[row][column];
            }
            out += " \\\\ ";
        }
        out += "};";
        const auto present = [&diagram](const std::size_t row, const std::size_t column) {
            return row >= 1 && row <= diagram.cells.size() && column >= 1 && column <= diagram.cells[row - 1].size() &&
                   !diagram.cells[row - 1][column - 1].empty();
        };
        for (const Arrow& arrow : diagram.arrows) {
            if (!present(arrow.row, arrow.column) || !present(arrow.down, arrow.across)) {
                tracebacks.emplace_back(syntax::Traceback::Type::Warning, origin,
                                         std::format("a diagram's arrow from row {}, column {} goes to no object and is not drawn",
                                                     arrow.row, arrow.column));
                continue;
            }
            out += std::format("\\draw[{}] (cd-{}-{}) to ", arrow.style, arrow.row, arrow.column);
            for (const std::string& label : arrow.labels) out += label + " ";
            out += std::format("(cd-{}-{});", arrow.down, arrow.across);
        }
        return out + "\\end{tikzpicture}";
    }

    Diagrams::Branch Diagrams::branch(const std::string_view text, std::size_t& at) {
        // How wide a label stands, near enough: half an em a letter, a
        // command one letter, and its node's inner sep either side.
        const auto wide = [](const std::string_view label) {
            int letters = 0;
            for (std::size_t index = 0; index < label.size(); ++index) {
                const char letter = label[index];
                if (letter == '\\') {
                    ++letters;
                    while (index + 1 < label.size() && std::isalpha(static_cast<unsigned char>(label[index + 1]))) ++index;
                } else if (letter != '{' && letter != '}' && letter != '$' && letter != '^' && letter != '_' && letter != ' ') {
                    ++letters;
                }
            }
            return static_cast<float>(letters) * 5.2f + 6.6f;
        };
        Branch node;
        ++at;
        const bool dotted = at < text.size() && text[at] == '.';
        if (dotted) ++at;

        // The label: qtree's is one word or group after its dot; forest's
        // runs to its first child or its end, its options after a comma.
        std::size_t begin = at;
        if (dotted) {
            if (at < text.size() && text[at] == '{') {
                at = closing(text, at) + 1;
            } else {
                while (at < text.size() && text[at] != ' ' && text[at] != '[' && text[at] != ']') {
                    at = text[at] == '{' ? closing(text, at) + 1 : at + 1;
                }
            }
        } else {
            while (at < text.size() && text[at] != '[' && text[at] != ']') {
                at = text[at] == '{' ? closing(text, at) + 1 : at + 1;
            }
        }
        std::string_view label = trim(text.substr(begin, std::min(at, text.size()) - begin));
        if (!dotted) {
            int depth = 0;
            for (std::size_t index = 0; index < label.size(); ++index) {
                if (label[index] == '{') ++depth;
                if (label[index] == '}') --depth;
                if (label[index] == ',' && depth == 0) {
                    label = trim(label.substr(0, index));
                    break;
                }
            }
        }
        if (label.size() > 1 && label.front() == '{' && closing(label, 0) == label.size() - 1) {
            label = label.substr(1, label.size() - 2);
        }
        node.label = label;

        // Its children: bracketed nodes, and in qtree's words bare leaves.
        while (at < text.size()) {
            while (at < text.size() && (text[at] == ' ' || text[at] == '\n')) ++at;
            if (at >= text.size()) break;
            if (text[at] == ']') {
                ++at;
                // qtree may name a node again after its end: `]` `.S`.
                if (at < text.size() && text[at] == '.') {
                    while (at < text.size() && text[at] != ' ' && text[at] != ']' && text[at] != '[') ++at;
                }
                break;
            }
            if (text[at] == '[') {
                node.children.push_back(branch(text, at));
                continue;
            }
            begin = at;
            if (text[at] == '{') {
                at = closing(text, at) + 1;
            } else {
                while (at < text.size() && text[at] != ' ' && text[at] != '[' && text[at] != ']') {
                    at = text[at] == '{' ? closing(text, at) + 1 : at + 1;
                }
            }
            std::string_view word = trim(text.substr(begin, std::min(at, text.size()) - begin));
            if (word.size() > 1 && word.front() == '{' && word.back() == '}') word = word.substr(1, word.size() - 2);
            if (!word.empty() && dotted) node.children.push_back(Branch{.label = std::string(word), .width = wide(word)});
        }

        node.width = wide(node.label);
        // Or its children's, side by side and an em apart, when wider.
        float under = 0.0f;
        for (const Branch& child : node.children) under += child.width;
        if (!node.children.empty()) under += 10.0f * static_cast<float>(node.children.size() - 1);
        node.width = std::max(node.width, under);
        return node;
    }

    std::string Diagrams::plant(Branch& tree, const float left, const int depth, int& count, std::string& out) {
        // The children side by side, centred under the room this node has.
        float under = 0.0f;
        for (const Branch& child : tree.children) under += child.width;
        if (!tree.children.empty()) under += 10.0f * static_cast<float>(tree.children.size() - 1);
        float place = left + (tree.width - under) * 0.5f;
        std::vector<std::string> names;
        for (Branch& child : tree.children) {
            names.push_back(plant(child, place, depth + 1, count, out));
            place += child.width + 10.0f;
        }
        tree.x = tree.children.empty() ? left + tree.width * 0.5f
                                        : (tree.children.front().x + tree.children.back().x) * 0.5f;
        const std::string name = std::format("@t{}", ++count);
        out += std::format("\\node ({}) at ({:.2f}pt,{:.2f}pt) {{{}}};", name, tree.x, -22.0f * static_cast<float>(depth),
                           tree.label);
        for (const std::string& child : names) out += std::format("\\draw ({}) -- ({});", name, child);
        return name;
    }

    void Diagrams::operator()(syntax::Parser& parser, Context& context) const {
        // An option list cut at its commas, those inside braces, brackets or
        // quotes left alone: tikz-cd's `r, "f,g"', bend left`.
        const auto split = [](const std::string_view list) {
            std::vector<std::string_view> found;
            int depth = 0;
            bool quoted = false;
            std::size_t begin = 0;
            for (std::size_t at = 0; at <= list.size(); ++at) {
                const char letter = at < list.size() ? list[at] : ',';
                if (letter == '"' && depth == 0) quoted = !quoted;
                if (!quoted && (letter == '{' || letter == '[')) ++depth;
                if (!quoted && (letter == '}' || letter == ']') && depth > 0) --depth;
                if (letter != ',' || depth != 0 || quoted) continue;
                if (const std::string_view piece = trim(list.substr(begin, at - begin)); !piece.empty()) found.push_back(piece);
                begin = at + 1;
            }
            return found;
        };
        // The grid of a diagram's objects alone, for one inside a formula of
        // other things, which is set as a matrix is.
        const auto objects = [](const Diagram& diagram) {
            std::string out = "\\matrix{";
            for (const std::vector<std::string>& row : diagram.cells) {
                for (std::size_t column = 0; column < row.size(); ++column) out += (column > 0 ? " & " : "") + row[column];
                out += " \\\\ ";
            }
            return out + "}";
        };
        // A label's text as a TikZ node on its arrow, in script size: on the
        // arrow's left, or its right when swapped, with `auto`; across it
        // with `description`; or where a side it names puts it.
        const auto label = [](const std::string_view text, const bool swapped, const std::string_view options) {
            const bool placed = options.find("above") != std::string_view::npos || options.find("below") != std::string_view::npos ||
                                options.find("left") != std::string_view::npos || options.find("right") != std::string_view::npos;
            const bool across = options.find("description") != std::string_view::npos;
            std::string written = "font=\\scriptsize,inner sep=0.5ex,";
            if (across) written += "fill=white,";
            else if (!placed) written += swapped ? "auto,swap," : "auto,";
            written += options;
            return std::format("node[{}] {{${}$}}", written, text);
        };

        // tikz-cd: `\begin{tikzcd}[column sep=large] A \arrow[r, "f"] & B \end{tikzcd}`.
        context.blocks.watch(
            "tikzcd",
            [this, &context, split, objects, label](syntax::Mouth& mouth) {
                const memory::Location origin = mouth.lookahead().location;
                std::string options;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    options += token.text;
                    if (token.text.size() > 1 && token.text.front() == '\\') options += ' ';
                }
                const std::string text = body(mouth, "tikzcd");

                // Its spacing by tikz-cd's names or as a length, the
                // separator between cells, and what every arrow and every
                // object takes.
                Diagram diagram;
                std::string separator = "&";
                std::string every;
                static constexpr std::array<std::pair<std::string_view, std::array<std::string_view, 2>>, 6> sizes{{
                    {"tiny", {"0.6em", "0.45em"}}, {"small", {"1.2em", "0.9em"}}, {"scriptsize", {"1.8em", "1.35em"}},
                    {"normal", {"2.4em", "1.8em"}}, {"large", {"3.6em", "2.7em"}}, {"huge", {"4.8em", "3.6em"}},
                }};
                const auto sized = [](const std::string_view value, const std::size_t which) {
                    const auto named = std::ranges::find(sizes, value, &std::pair<std::string_view, std::array<std::string_view, 2>>::first);
                    return std::string(named != sizes.end() ? named->second[which] : value);
                };
                for (const std::string_view option : split(options)) {
                    const std::size_t equals = option.find('=');
                    const std::string_view key = trim(option.substr(0, equals));
                    std::string_view value = equals == std::string_view::npos ? std::string_view{} : trim(option.substr(equals + 1));
                    if (value.starts_with('{') && value.ends_with('}')) value = value.substr(1, value.size() - 2);
                    if (key == "column sep" || key == "sep") diagram.columns = sized(value, 0);
                    if (key == "row sep" || key == "sep") diagram.rows = sized(value, 1);
                    if (key == "ampersand replacement") separator = trim(value);
                    if (key == "arrows" || key == "every arrow/.append style") every += std::string(value) + ",";
                    if (key == "cells" && value.starts_with("nodes=")) {
                        std::string_view nodes = trim(value.substr(6));
                        if (nodes.starts_with('{') && nodes.ends_with('}')) nodes = nodes.substr(1, nodes.size() - 2);
                        diagram.nodes = nodes;
                    }
                }

                // Each cell's object, and the arrows written in it.
                static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> commands{{
                    {"\\arrow ", ""}, {"\\ar ", ""}, {"\\rar ", "r"}, {"\\lar ", "l"}, {"\\dar ", "d"}, {"\\uar ", "u"},
                    {"\\drar ", "dr"}, {"\\urar ", "ur"}, {"\\dlar ", "dl"}, {"\\ular ", "ul"},
                }};
                const std::vector<std::vector<std::string>> rows = grid(text, separator);
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    diagram.cells.emplace_back();
                    for (std::size_t c = 0; c < rows[r].size(); ++c) {
                        const std::string_view cell = rows[r][c];
                        std::string object;
                        std::size_t at = 0;
                        while (at < cell.size()) {
                            const auto command = std::ranges::find_if(commands, [&](const auto& pair) {
                                return cell.substr(at).starts_with(pair.first);
                            });
                            if (command == commands.end()) {
                                if (cell[at] == '{') {
                                    const std::size_t shut = closing(cell, at);
                                    object += cell.substr(at, shut + 1 - at);
                                    at = shut + 1;
                                } else {
                                    object += cell[at++];
                                }
                                continue;
                            }
                            at += command->first.size();
                            while (at < cell.size() && cell[at] == ' ') ++at;
                            std::string direction(command->second);
                            std::string list;
                            std::vector<std::string_view> old;
                            if (at < cell.size() && cell[at] == '[') {
                                const std::size_t shut = closing(cell, at);
                                list = cell.substr(at + 1, shut - at - 1);
                                at = shut + 1;
                            }
                            // The older form: `\arrow{r}{f}`, its way and its label.
                            while (at < cell.size() && cell[at] == '{' && old.size() < 2) {
                                const std::size_t shut = closing(cell, at);
                                old.push_back(cell.substr(at + 1, shut - at - 1));
                                at = shut + 1;
                                while (at < cell.size() && cell[at] == ' ') ++at;
                            }
                            if (!old.empty() && direction.empty()) {
                                direction = old.front();
                                old.erase(old.begin());
                            }

                            Arrow arrow{.row = r + 1, .column = c + 1};
                            std::string foot;
                            std::string head = ">";
                            bool doubled = false;
                            bool swapped = false;
                            bool across = false;
                            std::string style;
                            std::vector<std::pair<std::string, std::pair<bool, std::string>>> labels;
                            for (const std::string_view item : split(every + list)) {
                                if (item.starts_with('"')) {
                                    const std::size_t shut = item.find('"', 1);
                                    const std::string_view written = item.substr(1, std::min(shut, item.size()) - 1);
                                    std::string_view rest = shut == std::string_view::npos ? std::string_view{} : trim(item.substr(shut + 1));
                                    const bool turned = rest.starts_with('\'');
                                    if (turned) rest = trim(rest.substr(1));
                                    if (rest.starts_with('{') && rest.ends_with('}')) rest = rest.substr(1, rest.size() - 2);
                                    labels.push_back({std::string(written), {turned, std::string(rest)}});
                                } else if (item.find_first_not_of("rlud") == std::string_view::npos) {
                                    direction = item;
                                } else if (item == "Rightarrow" || item == "Longrightarrow" || item == "Rrightarrow") {
                                    doubled = true;
                                } else if (item == "Leftarrow") {
                                    doubled = true;
                                    foot = "<";
                                    head.clear();
                                } else if (item == "Leftrightarrow") {
                                    doubled = true;
                                    foot = "<";
                                } else if (item == "equal" || item == "Equal" || item == "equals") {
                                    doubled = true;
                                    head.clear();
                                } else if (item == "dash" || item == "no head" || item == "-") {
                                    head.clear();
                                } else if (item == "hook") {
                                    foot = "hook";
                                } else if (item == "hook'") {
                                    foot = "right hook";
                                } else if (item == "tail") {
                                    foot = ">";
                                } else if (item == "two heads" || item == "twoheadrightarrow") {
                                    head = ">>";
                                } else if (item == "mapsto" || item == "maps to") {
                                    foot = "|";
                                } else if (item == "leftarrow") {
                                    foot = "<";
                                    head.clear();
                                } else if (item == "leftrightarrow") {
                                    foot = "<";
                                } else if (item == "swap") {
                                    swapped = true;
                                } else if (item == "description" || item == "labels=description") {
                                    across = true;
                                } else if (item == "phantom") {
                                    style += "phantom,";
                                    across = true;
                                } else if (item.starts_with("from=") || item.starts_with("to=")) {
                                    // A cell by its place, `2-3`.
                                    const std::string_view place = trim(item.substr(item.find('=') + 1));
                                    const std::size_t dash = place.find('-');
                                    std::size_t down = 0;
                                    std::size_t over = 0;
                                    std::from_chars(place.data(), place.data() + std::min(dash, place.size()), down);
                                    if (dash != std::string_view::npos) std::from_chars(place.data() + dash + 1, place.data() + place.size(), over);
                                    if (item.starts_with("from=")) {
                                        arrow.row = down;
                                        arrow.column = over;
                                    } else {
                                        direction = std::format("@{}-{}", down, over);
                                    }
                                } else if (item == "crossing over" || item == "rightarrow" || item == "squiggly" ||
                                           item == "rightsquigarrow" || item == "harpoon" || item == "harpoon'" ||
                                           item.starts_with("start anchor") || item.starts_with("end anchor") ||
                                           item.starts_with("outer sep") || item == "cramped") {
                                    // Drawn as a plain arrow.
                                } else {
                                    style += std::string(item) + ",";
                                }
                            }
                            if (!old.empty()) labels.push_back({std::string(old.front()), {false, std::string{}}});

                            // Where it goes: its way from here, or a cell named.
                            if (direction.starts_with('@')) {
                                std::size_t down = 0;
                                std::size_t over = 0;
                                const std::size_t dash = direction.find('-');
                                std::from_chars(direction.data() + 1, direction.data() + dash, down);
                                std::from_chars(direction.data() + dash + 1, direction.data() + direction.size(), over);
                                arrow.down = down;
                                arrow.across = over;
                            } else {
                                arrow.down = arrow.row + static_cast<std::size_t>(std::ranges::count(direction, 'd')) -
                                             static_cast<std::size_t>(std::ranges::count(direction, 'u'));
                                arrow.across = arrow.column + static_cast<std::size_t>(std::ranges::count(direction, 'r')) -
                                               static_cast<std::size_t>(std::ranges::count(direction, 'l'));
                            }
                            arrow.style = (doubled ? "double,double distance=1.6pt," : "") + foot + "-" + head + "," + style;
                            for (const auto& [written, how] : labels) {
                                std::string extra = how.second;
                                if (across && extra.find("description") == std::string::npos) extra += ",description";
                                arrow.labels.push_back(label(written, how.first != swapped, extra));
                            }
                            diagram.arrows.push_back(std::move(arrow));
                        }
                        diagram.cells.back().emplace_back(trim(object));
                    }
                }
                mouth.ingest(context.arena.copy(mouth.formula() ? objects(diagram) : picture(diagram, origin)));
            },
            [](syntax::Mouth&) {});

        // xy-pic: `\xymatrix@C=3em{A \ar[r]^f \ar@{-->}[d]_g & B}`.
        parser.mouth.bind("\\xymatrix", [this, &context, objects, label](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            Diagram diagram{.columns = "2pc", .rows = "2pc"};
            // Its spacing before its body: `@C=`, `@R=`, `@=` and the rest.
            while (mouth.lookahead().is('@')) {
                static_cast<void>(mouth.read());
                std::string which;
                while (!mouth.lookahead().empty() && !mouth.lookahead().is('=') && !mouth.lookahead().is('@') &&
                       !mouth.lookahead().is(Category::Group, '{') && which.size() < 3) {
                    which += mouth.read().text;
                }
                std::string length;
                if (mouth.lookahead().is('=')) {
                    static_cast<void>(mouth.read());
                    while (!mouth.lookahead().empty() && !mouth.lookahead().is('@') &&
                           !mouth.lookahead().is(Category::Group, '{')) {
                        length += mouth.read().text;
                    }
                }
                if (length.empty()) continue;
                if (which == "C" || which.empty()) diagram.columns = trim(length);
                if (which == "R" || which.empty()) diagram.rows = trim(length);
            }
            std::string text;
            for (const syntax::Token& token : mouth.argument({}, 0)) {
                // `\ar@{-->}` is one control word where `@` is a letter.
                if (token.text == "\\ar@") {
                    text += "\\ar @";
                    continue;
                }
                text += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
            }

            for (const std::vector<std::string>& row : grid(text, "&")) {
                diagram.cells.emplace_back();
                for (const std::string_view cell : row) {
                    std::string object;
                    std::size_t at = 0;
                    while (at < cell.size()) {
                        if (!cell.substr(at).starts_with("\\ar ")) {
                            if (cell[at] == '{') {
                                const std::size_t shut = closing(cell, at);
                                object += cell.substr(at, shut + 1 - at);
                                at = shut + 1;
                            } else {
                                object += cell[at++];
                            }
                            continue;
                        }
                        at += 4;
                        Arrow arrow{.row = diagram.cells.size(), .column = diagram.cells.back().size() + 1};
                        std::string foot;
                        std::string head = ">";
                        std::string style;
                        std::string direction;
                        bool doubled = false;
                        // What each piece of it says, until something it is not.
                        while (at < cell.size()) {
                            while (at < cell.size() && cell[at] == ' ') ++at;
                            if (at >= cell.size()) break;
                            if (cell[at] == '@' && at + 1 < cell.size()) {
                                const char kind = cell[at + 1];
                                if (kind == '{') {
                                    // Its shape, xy's way: `-->` dashed, `.>`
                                    // dotted, `=>` double, `^{(}->` a hook,
                                    // `|->` a bar, `->>` two heads.
                                    const std::size_t shut = closing(cell, at + 1);
                                    std::string_view shape = cell.substr(at + 2, shut - at - 2);
                                    at = shut + 1;
                                    if (shape.starts_with("^{(}")) {
                                        foot = "hook";
                                        shape.remove_prefix(4);
                                    } else if (shape.starts_with("_{(}")) {
                                        foot = "right hook";
                                        shape.remove_prefix(4);
                                    } else if (shape.starts_with('|')) {
                                        foot = "|";
                                        shape.remove_prefix(1);
                                    } else if (shape.starts_with("<")) {
                                        foot = "<";
                                        shape.remove_prefix(1);
                                    } else if (shape.starts_with(">")) {
                                        foot = ">";
                                        shape.remove_prefix(1);
                                    }
                                    head = shape.ends_with(">>") ? ">>" : shape.ends_with('>') ? ">" : "";
                                    if (shape.find('=') != std::string_view::npos) doubled = true;
                                    if (shape.starts_with("--")) style += "dashed,";
                                    if (shape.starts_with('.')) style += "dotted,";
                                } else if (kind == '<') {
                                    const std::size_t shut = cell.find('>', at);
                                    style += std::format("shift left={},", cell.substr(at + 2, shut - at - 2));
                                    at = std::min(shut + 1, cell.size());
                                } else if (kind == '/') {
                                    const std::size_t shut = cell.find('/', at + 2);
                                    style += cell[at + 2] == '_' ? "bend right," : "bend left,";
                                    at = std::min(shut + 1, cell.size());
                                } else if (kind == '(' || kind == '[' || kind == '*') {
                                    at = closing(cell, kind == '*' ? at + 2 : at + 1) + 1;
                                } else if (kind == '2') {
                                    doubled = true;
                                    at += 2;
                                } else {
                                    at += 2;
                                }
                            } else if (cell[at] == '[') {
                                const std::size_t shut = closing(cell, at);
                                direction = cell.substr(at + 1, shut - at - 1);
                                at = shut + 1;
                            } else if (cell[at] == '^' || cell[at] == '_' || cell[at] == '|') {
                                // A label: `^` on its left, `_` its right, `|` across it.
                                const char side = cell[at++];
                                while (at < cell.size() && (cell[at] == '<' || cell[at] == '>' || cell[at] == ' ')) ++at;
                                if (at < cell.size() && cell[at] == '(') at = closing(cell, at) + 1;
                                std::string_view written;
                                if (at < cell.size() && cell[at] == '{') {
                                    const std::size_t shut = closing(cell, at);
                                    written = cell.substr(at + 1, shut - at - 1);
                                    at = shut + 1;
                                } else if (at < cell.size() && cell[at] == '\\') {
                                    const std::size_t stop = cell.find(' ', at);
                                    written = cell.substr(at, std::min(stop, cell.size()) - at);
                                    at = std::min(stop, cell.size());
                                } else if (at < cell.size()) {
                                    written = cell.substr(at++, 1);
                                }
                                arrow.labels.push_back(label(written, side == '_', side == '|' ? "description" : ""));
                            } else {
                                break;
                            }
                        }
                        arrow.down = arrow.row + static_cast<std::size_t>(std::ranges::count(direction, 'd')) -
                                     static_cast<std::size_t>(std::ranges::count(direction, 'u'));
                        arrow.across = arrow.column + static_cast<std::size_t>(std::ranges::count(direction, 'r')) -
                                       static_cast<std::size_t>(std::ranges::count(direction, 'l'));
                        arrow.style = (doubled ? "double,double distance=1.6pt," : "") + foot + "-" + head + "," + style;
                        diagram.arrows.push_back(std::move(arrow));
                    }
                    diagram.cells.back().emplace_back(trim(object));
                }
            }
            mouth.ingest(context.arena.copy(mouth.formula() ? objects(diagram) : picture(diagram, origin)));
        });

        // amscd: `\begin{CD} A @>f>> B \\ @VgVV @VVhV \\ C @>>k> D \end{CD}`,
        // its rows of objects and horizontal arrows between rows of
        // vertical ones, each vertical arrow under the object it leaves.
        context.blocks.watch(
            "CD",
            [this, &context, objects, label](syntax::Mouth& mouth) {
                const memory::Location origin = mouth.lookahead().location;
                const std::string text = body(mouth, "CD");
                Diagram diagram{.columns = "2.8em", .rows = "2em"};
                const std::vector<std::vector<std::string>> rows = grid(text, "\x01");
                for (std::size_t index = 0; index < rows.size(); ++index) {
                    const std::string_view line = rows[index].front();
                    const bool upright = index % 2 == 1;
                    if (!upright) diagram.cells.emplace_back();
                    const std::size_t row = diagram.cells.size();
                    std::size_t column = 1;
                    std::string object;
                    std::size_t at = 0;
                    // The text up to the next of a character, braces kept whole.
                    const auto until = [&](const char stop) {
                        const std::size_t begin = at;
                        while (at < line.size() && line[at] != stop) at = line[at] == '{' ? closing(line, at) + 1 : at + 1;
                        const std::string_view found = trim(line.substr(begin, std::min(at, line.size()) - begin));
                        ++at;
                        return found;
                    };
                    while (at < line.size()) {
                        if (line[at] != '@' || at + 1 >= line.size()) {
                            object += line[at++];
                            continue;
                        }
                        const char kind = line[at + 1];
                        at += 2;
                        if (!upright) {
                            diagram.cells.back().emplace_back(trim(object));
                            object.clear();
                        }
                        Arrow arrow{.row = row, .column = column};
                        if (upright) {
                            arrow.down = row + 1;
                            arrow.across = column;
                        } else {
                            arrow.down = row;
                            arrow.across = column + 1;
                        }
                        ++column;
                        if (kind == '>' || kind == '<' || kind == 'V' || kind == 'A') {
                            const std::string_view first = until(kind);
                            const std::string_view second = until(kind);
                            // Drawn from the object it leaves; its first
                            // label above a horizontal arrow or left of a
                            // vertical one, which is the arrow's right side
                            // going left or down, and its second the other.
                            if (kind == '<' || kind == 'A') {
                                std::swap(arrow.row, arrow.down);
                                std::swap(arrow.column, arrow.across);
                            }
                            const bool turned = kind == '<' || kind == 'V';
                            arrow.style = "->,";
                            if (!first.empty()) arrow.labels.push_back(label(first, turned, ""));
                            if (!second.empty()) arrow.labels.push_back(label(second, !turned, ""));
                        } else if (kind == '=' || kind == '|') {
                            arrow.style = "double,double distance=1.6pt,-,";
                        } else {
                            continue;
                        }
                        diagram.arrows.push_back(std::move(arrow));
                    }
                    if (!upright) diagram.cells.back().emplace_back(trim(object));
                }
                mouth.ingest(context.arena.copy(mouth.formula() ? objects(diagram) : picture(diagram, origin)));
            },
            [](syntax::Mouth&) {});

        // A circuit, quantikz's or Qcircuit's: a row a wire, a cell a gate
        // on it -- boxed, a control's dot joined to its target's circle, a
        // meter, a label at its start or end -- and the wire run from its
        // first cell to its last, broken by each box it passes through.
        const auto circuit = [](const std::string_view text, const std::string_view columns, const std::string_view rows,
                                    const bool maths) {
            std::string out = std::format("\\begin{{tikzpicture}}[baseline=([yshift=-0.25em]q.center)]"
                                          "\\matrix[column sep={}, row sep={}] (q) {{", columns, rows);
            std::string after;
            const std::vector<std::vector<std::string>> lines = grid(text, "&");
            for (std::size_t r = 0; r < lines.size(); ++r) {
                std::size_t first = lines[r].size();
                std::size_t last = 0;
                for (std::size_t c = 0; c < lines[r].size(); ++c) {
                    if (lines[r][c].empty()) continue;
                    first = std::min(first, c);
                    last = c;
                }
                std::vector<bool> classical(lines[r].size(), false);
                for (std::size_t c = 0; c < lines[r].size(); ++c) {
                    const std::string_view cell = lines[r][c];
                    // Its command, and the argument it takes, if any.
                    std::string_view name;
                    std::string_view argument;
                    if (cell.starts_with('\\')) {
                        std::size_t stop = 1;
                        while (stop < cell.size() && std::isalpha(static_cast<unsigned char>(cell[stop]))) ++stop;
                        name = cell.substr(1, stop - 1);
                        std::size_t at = stop;
                        while (at < cell.size() && cell[at] == ' ') ++at;
                        if (at < cell.size() && cell[at] == '[') at = closing(cell, at) + 1;
                        while (at < cell.size() && cell[at] == ' ') ++at;
                        if (at < cell.size() && cell[at] == '{') argument = cell.substr(at + 1, closing(cell, at) - at - 1);
                    }
                    const std::string called = std::format("q-{}-{}", r + 1, c + 1);
                    const std::string shown = maths ? std::format("${}$", argument) : std::string(argument);
                    std::string node = "\\node[inner sep=0] {};";
                    if (name == "gate" || name == "multigate") {
                        // Qcircuit's multigate names its wires first, its gate last.
                        const std::size_t open = cell.rfind('{');
                        const std::string_view written = name == "multigate" && open != std::string_view::npos
                                                             ? cell.substr(open + 1, closing(cell, open) - open - 1)
                                                             : argument;
                        node = std::format("\\node[draw, fill=white, minimum size=1.6em] {{${}$}};", written);
                    } else if (name == "lstick" || name == "rstick" || name == "push" || name == "midstick") {
                        node = std::format("\\node {{{}}};", name == "lstick" || name == "rstick" ? shown : std::string(argument));
                    } else if (name == "ctrl" || name == "control" || name == "phase") {
                        node = "\\node[fill, circle, inner sep=0, minimum size=0.4em] {};";
                        if (name == "phase") after += std::format("\\node[above] at ({}) {{${}$}};", called, argument);
                    } else if (name == "octrl" || name == "ctrlo" || name == "controlo") {
                        node = "\\node[draw, fill=white, circle, inner sep=0, minimum size=0.45em] {};";
                    } else if (name == "targ") {
                        node = "\\node[draw, circle, inner sep=0, minimum size=0.9em] {};";
                        after += std::format("\\draw ({0}.north) -- ({0}.south);\\draw ({0}.west) -- ({0}.east);", called);
                    } else if (name == "swap" || name == "targX" || name == "qswap") {
                        node = "\\node[inner sep=0, minimum size=0.55em] {};";
                        after += std::format("\\draw ({0}.north west) -- ({0}.south east);\\draw ({0}.north east) -- ({0}.south west);", called);
                    } else if (name == "meter" || name == "measure" || name == "measureD" || name == "meterD") {
                        node = "\\node[draw, fill=white, minimum width=1.7em, minimum height=1.3em] {};";
                        after += std::format("\\draw ({0}.south) ++(-0.2em,0.2em) -- ++(0.55em,0.75em);"
                                             "\\draw ({0}.center) ++(-0.5em,-0.25em) arc (160:20:0.53em);", called);
                    } else if (name == "cw" || name == "cwx") {
                        classical[c] = true;
                    }
                    out += (c > 0 ? " & " : "") + node;
                    // What joins it to another wire: a control's line.
                    if ((name == "ctrl" || name == "octrl" || name == "ctrlo" || name == "swap" || name == "qwx" ||
                         name == "cwx") && !argument.empty()) {
                        int reach = 0;
                        std::from_chars(argument.data(), argument.data() + argument.size(), reach);
                        const int target = static_cast<int>(r) + 1 + reach;
                        if (reach != 0 && target >= 1) after += std::format("\\draw ({}) -- (q-{}-{});", called, target, c + 1);
                    }
                }
                out += " \\\\ ";
                for (std::size_t c = first; c < last; ++c) {
                    after += std::format("\\draw{} (q-{}-{}) -- (q-{}-{});", classical[c + 1] ? "[double, double distance=1pt]" : "",
                                         r + 1, c + 1, r + 1, c + 2);
                }
            }
            return out + "};" + after + "\\end{tikzpicture}";
        };

        context.blocks.watch(
            "quantikz",
            [&context, circuit](syntax::Mouth& mouth) {
                std::string options;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) options += token.text;
                std::string columns = "0.9em";
                std::string rows = "0.9em";
                for (const auto piece : std::views::split(std::string_view{options}, ',')) {
                    const std::string_view option = trim(std::string_view(piece.begin(), piece.end()));
                    if (option.starts_with("column sep=")) columns = trim(option.substr(11));
                    if (option.starts_with("row sep=")) rows = trim(option.substr(8));
                }
                const std::string text = body(mouth, "quantikz");
                mouth.ingest(context.arena.copy(circuit(text, columns, rows, false)));
            },
            [](syntax::Mouth&) {});

        parser.mouth.bind("\\Qcircuit", [&context, circuit](syntax::Mouth& mouth) {
            std::string columns = "1em";
            std::string rows = "1em";
            while (mouth.lookahead().is('@')) {
                static_cast<void>(mouth.read());
                const std::string which(mouth.read().text);
                if (mouth.lookahead().is('=')) static_cast<void>(mouth.read());
                std::string length;
                while (!mouth.lookahead().empty() && !mouth.lookahead().is('@') && !mouth.lookahead().is(Category::Group, '{')) {
                    length += mouth.read().text;
                }
                if (which == "C") columns = trim(length);
                if (which == "R") rows = trim(length);
            }
            std::string text;
            for (const syntax::Token& token : mouth.argument({}, 0)) {
                text += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
            }
            mouth.ingest(context.arena.copy(circuit(text, columns, rows, true)));
        });

        // Trees: forest's block, and qtree's and tikz-qtree's `\Tree` -- in
        // a TikZ picture of its own, or the one it is written in.
        const auto tree = [](const std::string_view text, const bool inside) {
            const std::size_t open = text.find('[');
            if (open == std::string_view::npos) return std::string{};
            std::size_t at = open;
            Branch root = branch(text, at);
            std::string out = inside ? "" : "\\begin{tikzpicture}";
            int count = 0;
            static_cast<void>(plant(root, 0.0f, 0, count, out));
            return inside ? out : out + "\\end{tikzpicture}";
        };
        context.blocks.watch(
            "forest",
            [&context, tree](syntax::Mouth& mouth) {
                mouth.ingest(context.arena.copy(tree(body(mouth, "forest"), false)));
            },
            [](syntax::Mouth&) {});
        parser.mouth.bind("\\Tree", [&context, tree](syntax::Mouth& mouth) {
            std::string text;
            int depth = 0;
            for (syntax::Token token = mouth.read(); !token.empty(); token = mouth.read()) {
                text += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
                if (token.is('[')) ++depth;
                if (token.is(']') && --depth == 0) break;
            }
            mouth.ingest(context.arena.copy(tree(text, context.blocks.innermost() == "tikzpicture")));
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound diagram primitives");
    }

}
