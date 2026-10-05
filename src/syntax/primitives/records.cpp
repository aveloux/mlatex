/// @file
/// @brief Records implementation: csvsimple's CSV files and datatool's
///        databases, one table type for both.
#include "syntax/primitives/records.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace syntax::primitives {

    Records::Records(Lexicon& lexicon) noexcept {
        static constexpr std::array<std::string_view, 20> names{
            "\\csvautotabular", "\\csvautobooktabular", "\\csvreader", "\\DTLnewdb", "\\DTLnewrow",
            "\\DTLnewdbentry", "\\DTLloaddb", "\\DTLforeach", "\\DTLdisplaydb", "\\DTLrowcount",
            "\\DTLcolumncount", "\\DTLgetvalue", "\\DTLfetch", "\\DTLsort", "\\DTLsumcolumn",
            "\\DTLmeanforcolumn", "\\DTLifdbexists", "\\DTLcleardb", "\\DTLsetseparator", "\\DTLsettabseparator",
        };
        for (const std::string_view name : names) lexicon.intern(name);
    }

    std::vector<std::vector<std::string>> Records::parse(const std::string_view text, const char separator) {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        bool any = false;
        for (std::size_t at = 0; at <= text.size(); ++at) {
            const char letter = at < text.size() ? text[at] : '\n';
            if (quoted) {
                if (letter == '"' && at + 1 < text.size() && text[at + 1] == '"') {
                    field += '"';
                    ++at;
                } else if (letter == '"') {
                    quoted = false;
                } else {
                    field += letter;
                }
                continue;
            }
            if (letter == '"' && field.empty()) {
                quoted = true;
                any = true;
            } else if (letter == separator) {
                fields.push_back(std::move(field));
                field.clear();
                any = true;
            } else if (letter == '\n') {
                if (any || !field.empty()) {
                    fields.push_back(std::move(field));
                    rows.push_back(std::move(fields));
                }
                fields.clear();
                field.clear();
                any = false;
            } else if (letter != '\r') {
                field += letter;
            }
        }
        // Each field without the blanks around it, as both packages read one.
        for (std::vector<std::string>& row : rows) {
            for (std::string& cell : row) {
                const std::size_t first = cell.find_first_not_of(" \t");
                const std::size_t last = cell.find_last_not_of(" \t");
                cell = first == std::string::npos ? std::string{} : cell.substr(first, last - first + 1);
            }
        }
        return rows;
    }

    void Records::operator()(Mouth& mouth, Context& context) const {
        // A run of tokens as the text it was written as, a control word
        // spaced from a letter after it.
        const auto written = [](const std::span<const Token> tokens) {
            std::string text;
            bool word = false;
            for (const Token& token : tokens) {
                if (word && token.category == Catcodes::Category::Letter) text += ' ';
                text += token.text;
                word = token.category == Catcodes::Category::Escape && token.text.size() > 1 &&
                       std::isalpha(static_cast<unsigned char>(token.text.back())) != 0;
            }
            return text;
        };
        // A key list cut at its top-level commas, each piece a key and a
        // value, the value's outer braces taken off: `tabular=lr, no head`.
        const auto pieces = [](const std::string_view list) {
            std::vector<std::pair<std::string, std::string>> found;
            int depth = 0;
            std::size_t begin = 0;
            for (std::size_t at = 0; at <= list.size(); ++at) {
                const char letter = at < list.size() ? list[at] : ',';
                if (letter == '{') ++depth;
                if (letter == '}') --depth;
                if (letter != ',' || depth > 0) continue;
                std::string_view piece = list.substr(begin, at - begin);
                begin = at + 1;
                const auto trim = [](std::string_view text) {
                    while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.remove_prefix(1);
                    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.remove_suffix(1);
                    return text;
                };
                piece = trim(piece);
                if (piece.empty()) continue;
                const std::size_t equals = piece.find('=');
                std::string_view value = equals == std::string_view::npos ? std::string_view{} : trim(piece.substr(equals + 1));
                if (value.size() >= 2 && value.front() == '{' && value.back() == '}') value = value.substr(1, value.size() - 2);
                found.emplace_back(std::string(trim(piece.substr(0, equals))), std::string(value));
            }
            return found;
        };
        // A file's text, from those handed in, written by the document or
        // beside it; none, reported, when it is nowhere.
        const auto file = [this, &context](const std::string& name, const memory::Location origin)
            -> std::optional<std::string> {
            if (context.files) {
                if (const auto found = context.files->find(name); found != context.files->end()) return found->second;
            }
            if (context.disk) {
                if (const std::string* found = context.disk(name)) return *found;
            }
            tracebacks.emplace_back(Traceback::Type::Primitive, origin, std::format("No data file named '{}'", name));
            return std::nullopt;
        };
        // Whether a value reads whole as a number.
        const auto numeric = [](const std::string& text) {
            double value = 0.0;
            const auto [end, fault] = std::from_chars(text.data(), text.data() + text.size(), value);
            return !text.empty() && fault == std::errc{} && end == text.data() + text.size();
        };
        // A table as a tabular: its names over its rows, a number column set
        // flush right, ruled as csvsimple's autotabular or booktabs' rules.
        const auto tabular = [numeric](const Table& table, const std::string_view rules) {
            std::string preamble;
            for (std::size_t column = 0; column < table.keys.size(); ++column) {
                const bool numbers = !table.rows.empty() && std::ranges::all_of(table.rows, [&](const auto& row) {
                    return column < row.size() && numeric(row[column]);
                });
                if (rules == "ruled") preamble += '|';
                preamble += numbers ? 'r' : 'l';
            }
            if (rules == "ruled") preamble += '|';
            std::string text = "\\begin{tabular}{" + preamble + "}";
            text += rules == "booktabs" ? "\\toprule " : rules == "ruled" ? "\\hline " : "";
            for (std::size_t column = 0; column < table.keys.size(); ++column) {
                text += (column == 0 ? "" : " & ") + (rules == "plain" ? "\\textbf{" + table.keys[column] + "}" : table.keys[column]);
            }
            text += "\\\\";
            text += rules == "booktabs" ? "\\midrule " : rules == "ruled" ? "\\hline " : "";
            for (const std::vector<std::string>& row : table.rows) {
                for (std::size_t column = 0; column < table.keys.size(); ++column) {
                    text += (column == 0 ? "" : " & ") + (column < row.size() ? row[column] : std::string{});
                }
                text += "\\\\";
            }
            text += rules == "booktabs" ? "\\bottomrule " : rules == "ruled" ? "\\hline " : "";
            return text + "\\end{tabular}";
        };
        // A CSV file as a table: its first line its names, or columns
        // numbered from 1 when it has none.
        const auto read = [this](const std::string& text, const bool headed) {
            std::vector<std::vector<std::string>> lines = parse(text, separator);
            Table table;
            if (headed && !lines.empty()) {
                table.keys = std::move(lines.front());
                lines.erase(lines.begin());
            } else {
                std::size_t across = 0;
                for (const auto& line : lines) across = std::max(across, line.size());
                for (std::size_t column = 1; column <= across; ++column) table.keys.push_back(std::to_string(column));
            }
            for (std::vector<std::string>& line : lines) {
                line.resize(table.keys.size());
                table.rows.push_back(std::move(line));
            }
            return table;
        };
        // What each row sets: `\\cs=Key` as datatool writes it, `Key=\\cs` or
        // `1=\\cs` as csvsimple does -- each macro defined, for every row,
        // as the value in its column.
        const auto definitions = [pieces](const Table& table, const std::string_view list,
                                          const std::vector<std::string>& row) {
            std::string text;
            for (const auto& [left, right] : pieces(list)) {
                const bool named = left.starts_with('\\');
                const std::string& macro = named ? left : right;
                const std::string& key = named ? right : left;
                std::size_t column = table.keys.size();
                for (std::size_t index = 0; index < table.keys.size(); ++index) {
                    if (table.keys[index] == key) column = index;
                }
                if (column == table.keys.size()) {
                    std::size_t number = 0;
                    const auto [end, fault] = std::from_chars(key.data(), key.data() + key.size(), number);
                    if (fault == std::errc{} && end == key.data() + key.size() && number >= 1) column = number - 1;
                }
                if (macro.starts_with('\\') && column < row.size()) text += "\\@shared\\@define" + macro + "{" + row[column] + "}";
            }
            return text;
        };

        // --- csvsimple ----------------------------------------------------------
        for (const bool booktabs : {false, true}) {
            mouth.bind(booktabs ? "\\csvautobooktabular" : "\\csvautotabular",
                       [this, file, read, tabular, pieces, booktabs](Mouth& mouth) {
                const memory::Location origin = mouth.lookahead().location;
                std::string options;
                for (const Token& token : mouth.argument(Mouth::Parameter{.optional = true}, 0)) options += token.text;
                const std::string name = Argument::text(mouth);
                const char kept = separator;
                for (const auto& [key, value] : pieces(options)) {
                    if (key == "separator") separator = value == "semicolon" ? ';' : value == "tab" ? '\t' : value == "pipe" ? '|' : ',';
                }
                const std::optional<std::string> text = file(name, origin);
                const bool headed = !options.contains("no head");
                if (text) mouth.ingest(mouth.arena.copy(tabular(read(*text, headed), booktabs ? "booktabs" : "ruled")), memory::Location{});
                separator = kept;
            });
        }

        mouth.bind("\\csvreader", [this, file, read, pieces, written, definitions](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            std::string options;
            for (const Token& token : mouth.argument(Mouth::Parameter{.optional = true}, 0)) options += token.text;
            const std::string name = Argument::text(mouth);
            const std::string assignments = written(mouth.argument({}, 0));
            const std::string body = written(mouth.argument({}, 1));

            std::string spec;
            std::string head;
            std::string foot;
            std::string after;
            bool headed = true;
            bool columns = false;
            bool centred = false;
            const char kept = separator;
            for (const auto& [key, value] : pieces(options)) {
                if (key == "head to column names") columns = true;
                if (key == "no head") headed = false;
                if (key == "separator") separator = value == "semicolon" ? ';' : value == "tab" ? '\t' : value == "pipe" ? '|' : ',';
                if (key == "tabular" || key == "centered tabular") {
                    spec = value;
                    after = "\\\\";
                    centred = key == "centered tabular";
                }
                if (key == "table head") head = value;
                if (key == "table foot") foot = value;
                if (key == "late after line" || key == "after line") after = value;
            }
            const std::optional<std::string> text = file(name, origin);
            const Table table = text ? read(*text, headed) : Table{};
            separator = kept;
            if (!text) return;

            std::string out = centred ? "\\begin{center}" : "";
            if (!spec.empty()) out += "\\begin{tabular}{" + spec + "}" + head;
            static constexpr std::array<std::string_view, 10> numerals{"i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x"};
            for (std::size_t index = 0; index < table.rows.size(); ++index) {
                const std::vector<std::string>& row = table.rows[index];
                out += "\\@shared\\@define\\thecsvrow{" + std::to_string(index + 1) + "}";
                for (std::size_t column = 0; column < row.size() && column < numerals.size(); ++column) {
                    out += "\\@shared\\@define\\csvcol" + std::string(numerals[column]) + "{" + row[column] + "}";
                }
                if (columns) {
                    for (std::size_t column = 0; column < table.keys.size(); ++column) {
                        const std::string& key = table.keys[column];
                        if (!key.empty() && std::ranges::all_of(key, [](const char letter) { return std::isalpha(static_cast<unsigned char>(letter)) != 0; })) {
                            out += "\\@shared\\@define\\" + key + "{" + row[column] + "}";
                        }
                    }
                }
                out += definitions(table, assignments, row) + body + after;
            }
            if (!spec.empty()) out += foot + "\\end{tabular}";
            if (centred) out += "\\end{center}";
            mouth.ingest(mouth.arena.copy(out), memory::Location{});
        });

        // --- datatool -----------------------------------------------------------
        // A database by name, or none, reported where it was asked for.
        const auto named = [this](Mouth& mouth, const std::string& name) -> Table* {
            const auto found = tables.find(name);
            if (found != tables.end()) return &found->second;
            tracebacks.emplace_back(Traceback::Type::Argument, mouth.lookahead().location,
                                     std::format("No database named '{}'", name));
            return nullptr;
        };

        mouth.bind("\\DTLnewdb", [this](Mouth& mouth) { tables[Argument::text(mouth)] = Table{}; });
        mouth.bind("\\DTLcleardb", [this](Mouth& mouth) { tables[Argument::text(mouth)] = Table{}; });
        mouth.bind("\\DTLnewrow", [named](Mouth& mouth) {
            if (Table* table = named(mouth, Argument::text(mouth))) table->rows.emplace_back(table->keys.size());
        });
        mouth.bind("\\DTLnewdbentry", [named, written](Mouth& mouth) {
            const std::string name = Argument::text(mouth);
            const std::string key = Argument::text(mouth);
            const std::string value = written(mouth.argument({}, 0));
            Table* table = named(mouth, name);
            if (!table) return;
            if (table->rows.empty()) table->rows.emplace_back(table->keys.size());
            auto column = static_cast<std::size_t>(std::ranges::find(table->keys, key) - table->keys.begin());
            if (column == table->keys.size()) {
                table->keys.push_back(key);
                for (std::vector<std::string>& row : table->rows) row.resize(table->keys.size());
            }
            table->rows.back()[column] = value;
        });
        mouth.bind("\\DTLloaddb", [this, file, read](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            std::string options;
            for (const Token& token : mouth.argument(Mouth::Parameter{.optional = true}, 0)) options += token.text;
            const std::string name = Argument::text(mouth);
            const std::string path = Argument::text(mouth);
            if (const std::optional<std::string> text = file(path, origin)) {
                tables[name] = read(*text, !options.contains("noheader"));
            }
        });
        mouth.bind("\\DTLsetseparator", [this](Mouth& mouth) {
            const std::string written = Argument::text(mouth);
            separator = written.empty() ? ',' : written.front();
        });
        mouth.bind("\\DTLsettabseparator", [this](Mouth&) { separator = '\t'; });

        mouth.bind("\\DTLforeach", [named, written, definitions](Mouth& mouth) {
            if (mouth.lookahead().is('*')) mouth.read();
            static_cast<void>(mouth.argument(Mouth::Parameter{.optional = true}, 0));
            const std::string name = Argument::text(mouth);
            const std::string assignments = written(mouth.argument({}, 0));
            const std::string body = written(mouth.argument({}, 1));
            const Table* table = named(mouth, name);
            if (!table) return;
            std::string out;
            for (std::size_t index = 0; index < table->rows.size(); ++index) {
                out += "\\@shared\\@define\\DTLcurrentindex{" + std::to_string(index + 1) + "}";
                out += std::string("\\@shared\\@define\\DTLiffirstrow[2]{") + (index == 0 ? "#1" : "#2") + "}";
                out += std::string("\\@shared\\@define\\DTLiflastrow[2]{") + (index + 1 == table->rows.size() ? "#1" : "#2") + "}";
                out += definitions(*table, assignments, table->rows[index]) + body;
            }
            mouth.ingest(mouth.arena.copy(out), memory::Location{});
        });
        mouth.bind("\\DTLdisplaydb", [named, tabular](Mouth& mouth) {
            static_cast<void>(mouth.argument(Mouth::Parameter{.optional = true}, 0));
            if (const Table* table = named(mouth, Argument::text(mouth))) {
                mouth.ingest(mouth.arena.copy(tabular(*table, "plain")), memory::Location{});
            }
        });
        for (const bool rows : {true, false}) {
            mouth.bind(rows ? "\\DTLrowcount" : "\\DTLcolumncount", [named, rows](Mouth& mouth) {
                if (const Table* table = named(mouth, Argument::text(mouth))) {
                    mouth.ingest(mouth.arena.copy(std::to_string(rows ? table->rows.size() : table->keys.size())),
                                 memory::Location{});
                }
            });
        }
        mouth.bind("\\DTLifdbexists", [this](Mouth& mouth) {
            const bool exists = tables.contains(Argument::text(mouth));
            const std::vector<Token> yes = mouth.argument({}, 1);
            const std::vector<Token> no = mouth.argument({}, 1);
            const std::vector<Token>& chosen = exists ? yes : no;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        });
        // The value in a row and a column, both counted from 1, kept in a macro.
        mouth.bind("\\DTLgetvalue", [named](Mouth& mouth) {
            const std::vector<Token> target = mouth.argument({}, 0);
            const std::string name = Argument::text(mouth);
            const std::string row = Argument::expanded(mouth);
            const std::string column = Argument::expanded(mouth);
            const Table* table = named(mouth, name);
            if (!table || target.empty()) return;
            std::size_t across = 0;
            std::size_t down = 0;
            std::from_chars(row.data(), row.data() + row.size(), down);
            std::from_chars(column.data(), column.data() + column.size(), across);
            const std::string value = down >= 1 && down <= table->rows.size() && across >= 1 && across <= table->keys.size()
                                          ? table->rows[down - 1][across - 1] : std::string{};
            mouth.ingest(mouth.arena.copy("\\@define" + std::string(target.front().text) + "{" + value + "}"),
                         memory::Location{});
        });
        // The value one column holds in the first row where another holds a value.
        mouth.bind("\\DTLfetch", [named](Mouth& mouth) {
            const std::string name = Argument::text(mouth);
            const std::string key = Argument::text(mouth);
            const std::string wanted = Argument::expanded(mouth);
            const std::string other = Argument::text(mouth);
            const Table* table = named(mouth, name);
            if (!table) return;
            const auto at = [table](const std::string& column) {
                return static_cast<std::size_t>(std::ranges::find(table->keys, column) - table->keys.begin());
            };
            const std::size_t matched = at(key);
            const std::size_t shown = at(other);
            for (const std::vector<std::string>& row : table->rows) {
                if (matched < row.size() && shown < row.size() && row[matched] == wanted) {
                    mouth.ingest(mouth.arena.copy(row[shown]), memory::Location{});
                    return;
                }
            }
        });
        // Rows ordered by columns, `Score=descending,Name`: numbers as
        // numbers, the rest as text.
        mouth.bind("\\DTLsort", [named, pieces](Mouth& mouth) {
            static_cast<void>(mouth.argument(Mouth::Parameter{.optional = true}, 0));
            const std::string criteria = Argument::text(mouth);
            Table* table = named(mouth, Argument::text(mouth));
            if (!table) return;
            std::vector<std::pair<std::size_t, bool>> order;
            for (const auto& [key, value] : pieces(criteria)) {
                const auto column = static_cast<std::size_t>(std::ranges::find(table->keys, key) - table->keys.begin());
                if (column < table->keys.size()) order.emplace_back(column, value == "descending" || value == "desc");
            }
            std::ranges::stable_sort(table->rows, [&order](const std::vector<std::string>& left, const std::vector<std::string>& right) {
                for (const auto& [column, descending] : order) {
                    double first = 0.0;
                    double second = 0.0;
                    const auto one = std::from_chars(left[column].data(), left[column].data() + left[column].size(), first);
                    const auto two = std::from_chars(right[column].data(), right[column].data() + right[column].size(), second);
                    const bool numbers = one.ec == std::errc{} && two.ec == std::errc{};
                    const bool less = numbers ? first < second : left[column] < right[column];
                    const bool more = numbers ? first > second : left[column] > right[column];
                    if (less || more) return descending ? more : less;
                }
                return false;
            });
        });
        // A column's total, or its mean, kept in a macro.
        for (const bool mean : {false, true}) {
            mouth.bind(mean ? "\\DTLmeanforcolumn" : "\\DTLsumcolumn", [named, mean](Mouth& mouth) {
                const std::string name = Argument::text(mouth);
                const std::string key = Argument::text(mouth);
                const std::vector<Token> target = mouth.argument({}, 0);
                const Table* table = named(mouth, name);
                if (!table || target.empty()) return;
                const auto column = static_cast<std::size_t>(std::ranges::find(table->keys, key) - table->keys.begin());
                double total = 0.0;
                std::size_t counted = 0;
                for (const std::vector<std::string>& row : table->rows) {
                    double value = 0.0;
                    if (column < row.size() &&
                        std::from_chars(row[column].data(), row[column].data() + row[column].size(), value).ec == std::errc{}) {
                        total += value;
                        ++counted;
                    }
                }
                const double result = mean ? (counted == 0 ? 0.0 : total / static_cast<double>(counted)) : total;
                mouth.ingest(mouth.arena.copy("\\@define" + std::string(target.front().text) + "{" + std::format("{}", result) + "}"),
                             memory::Location{});
            });
        }

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Bound the record primitives");
    }

}
