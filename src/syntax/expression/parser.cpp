/// @file
/// @brief Expression parser implementation: a Pratt parser over math tokens.
///
/// Lookahead is lazy and at most one token deep, and whatever is still
/// buffered at destruction goes back to the expander. That is what keeps the
/// token after a formula -- the `.` in `\\(x\\).` -- from disappearing.
#include "syntax/expression/parser.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string>

namespace syntax::expression {

    // Fails with one clear, named message when this source is compiled
    // against an older syntax/expression/parser.hpp. The concept has to take a
    // template parameter: a requires-expression over a concrete type is
    // checked immediately and would hard-error instead of yielding false.
    template <typename T>
    concept Current = requires(T rule) { rule.arity; };
    static_assert(Current<Grammar::Rule>,
                  "stale syntax/expression/parser.hpp: update the header to the one matching this source");

    Grammar::Grammar(Lexicon& lexicon) {
        fences = {lexicon.intern("\\left"), lexicon.intern("\\right")};
        brackets = {
            Bracket{lexicon.intern("\\pmatrix"), '(', ')'},
            Bracket{lexicon.intern("\\bmatrix"), '[', ']'},
            Bracket{lexicon.intern("\\Bmatrix"), '{', '}'},
            Bracket{lexicon.intern("\\vmatrix"), '|', '|'},
            Bracket{lexicon.intern("\\Vmatrix"), 0x2016, 0x2016},
            Bracket{lexicon.intern("\\matrix"), 0, 0},
            Bracket{lexicon.intern("\\smallmatrix"), 0, 0},
            // A piecewise definition has one side to fence, not two, and
            // both its columns start at the left: the value, then the case.
            Bracket{lexicon.intern("\\cases"), '{', 0, "ll"},
            Bracket{lexicon.intern("\\dcases"), '{', 0, "ll", Node::Grid::Displayed},
            Bracket{lexicon.intern("\\rcases"), 0, '}', "ll"},
            Bracket{lexicon.intern("\\aligned"), 0, 0, "rl", Node::Grid::Paired},
            Bracket{lexicon.intern("\\gathered"), 0, 0, "c", Node::Grid::Displayed},
            Bracket{lexicon.intern("\\substack"), 0, 0, "c"},
        };
        // Found through the command's own slot in the rule table, so telling
        // a grid from any other atom costs one load rather than a scan.
        for (std::size_t index = 0; index < brackets.size(); ++index) {
            const auto slot = static_cast<std::size_t>(brackets[index].name);
            if (slot >= rules.size()) rules.resize(slot + 1, Rule{});
            rules[slot].bracket = static_cast<std::uint8_t>(index + 1);
        }
        infixes = {lexicon.intern("\\over"), lexicon.intern("\\atop"), lexicon.intern("\\choose")};
        array = lexicon.intern("\\array");
        operatorname = lexicon.intern("\\operatorname");
        column = lexicon.intern("&");
        row = lexicon.intern("\\\\");
    }

    Parser::Parser(Mouth& mouth, const Unicodes& unicodes, const Grammar& grammar, memory::Arena& arena,
                   const Node::Style style)
        : mouth(mouth), unicodes(unicodes), grammar(grammar), arena(arena), style(style) {}

    void Grammar::silence(const Symbol symbol) {
        const auto index = static_cast<std::size_t>(symbol);
        if (index >= rules.size()) rules.resize(index + 1, Rule{});
        rules[index].silent = true;
    }

    std::uint32_t Parser::delimiter(const Token& token) const {
        if (token.text == "." || token.empty()) return 0;
        if (token.text == "\\{") return '{';
        if (token.text == "\\}") return '}';
        if (token.text == "\\|") return 0x2016;
        if (token.text.size() == 1) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(token.text[0]));
        }
        std::string_view name = token.text;
        if (name.starts_with('\\')) name.remove_prefix(1);
        const auto symbol = unicodes.get(name);
        return symbol ? symbol->codepoint : 0;
    }

    Parser::~Parser() {
        if (primed && !current.empty()) {
            mouth.stream().inject(std::span{&current, 1});
            primed = false;
        }
    }

    Token Parser::lookahead() const noexcept {
        if (!primed) {
            // Blanks are discarded, as they are in TeX's maths mode. What
            // separates `a` from `+` on the page is the class of the atoms on
            // either side, not how many spaces the author happened to type --
            // so `a+b` and `a + b` have to come out identical, and a space
            // that survived this far would be set as a glyph and ruin both.
            do {
                current = mouth.expand();
            } while (current.category == Catcodes::Category::Space);
            primed = true;
        }
        return current;
    }

    Token Parser::advance() {
        const Token token = lookahead();
        current = {};
        primed = false;
        return token;
    }

    Token Parser::pending() const noexcept {
        // Only what was already read ahead. Reading on from here would skip
        // blanks the way maths does -- and the blank after a closing `$` is
        // a word space in the text, which would vanish with it.
        if (!primed) return {};
        const Token token = current;
        current = {};
        primed = false;
        return token;
    }

    Node* Parser::compose(const Node::Type type) const {
        auto* node = arena.compose<Node>(type);
        node->style = style;
        return node;
    }

    Node* Parser::compose(const Node::Type type, const Token& token) const {
        Node* node = compose(type);
        node->value = token.text;

        // An operator is a symbol like any other, so its name is resolved the
        // same way a bare symbol's would be. Without this an operator written
        // as a control sequence reaches the page as its own name, set as its
        // letters rather than drawn as the mark it stands for.
        std::string_view name = token.text;
        if (name.starts_with('\\')) name.remove_prefix(1);

        if (const auto symbol = unicodes.get(name)) {
            node->codepoint = symbol->codepoint;
            node->category = symbol->category;
        } else if (name.size() == 1) {
            // A bare character is its own code point, and the table does not
            // carry ASCII because nothing needs it to -- except for the two
            // characters a typewriter folded into something else: the hyphen
            // stands in for a minus sign, and the asterisk for the centred one
            // mathematics uses.
            const char written = name[0];
            node->codepoint = written == '-'   ? 0x2212u
                              : written == '*' ? 0x2217u
                                               : static_cast<std::uint32_t>(static_cast<unsigned char>(written));
            switch (written) {
                case '=': case '<': case '>': case ':':
                    node->category = Unicodes::Category::Relation;
                    break;
                case ',': case ';':
                    node->category = Unicodes::Category::Punctuation;
                    break;
                // A solidus is an ordinary symbol in TeX, whatever it is
                // written between: `a/b` sets no space either side.
                case '/':
                    node->category = Unicodes::Category::Ordinary;
                    break;
                // A bracket is a character like any other in TeX's maths, not
                // a group: `[0, T)` is a half-open interval, and only `\\left`
                // and `\\right` make a pair that has to match.
                case '(': case '[': case '{':
                    node->category = Unicodes::Category::Opening;
                    break;
                case ')': case ']': case '}':
                    node->category = Unicodes::Category::Closing;
                    break;
                default:
                    node->category = type == Node::Type::Binary ? Unicodes::Category::Binary
                                                                : Unicodes::Category::Ordinary;
                    break;
            }
        }

        return node;
    }

    void Grammar::bind(const Symbol symbol, const Node::Type type, const int weight,
                      const bool right, const bool structural, const int arity) {
        const auto index = static_cast<std::size_t>(symbol);
        if (index >= rules.size()) {
            rules.resize(index + 1, Rule{});
        }
        rules[index] = Rule{type, weight, right, structural, arity, rules[index].silent, rules[index].bracket};
        Logger::log(Logger::Type::Parser, Logger::Level::Debug,
                    "Bound symbol {} to rule type={}, weight={}, right={}, structural={}, arity={}",
                    index, static_cast<int>(type), weight, right, structural, arity);
    }

    void Grammar::declare(const Symbol symbol, const int arity) {
        const auto index = static_cast<std::size_t>(symbol);
        if (index >= rules.size()) {
            rules.resize(index + 1, Rule{});
        }
        rules[index].arity = arity;
    }

    Node* Parser::sequence(const char closing, const Symbol stop, const Symbol alternate,
                          Symbol* const matched) {
        if (matched) *matched = none;

        if (depth >= limit) {
            tracebacks.emplace_back(Traceback::Type::Recursion, lookahead().location, "Expression nesting limit reached");
            return nullptr;
        }

        struct Guard {
            std::size_t& counter;
            explicit Guard(std::size_t& value) : counter(value) { ++counter; }
            ~Guard() { --counter; }
        } guard(depth);

        std::array<Node*, 32> stack{};
        std::vector<Node*> heap{};
        std::size_t count = 0;

        auto push = [&](Node* item) {
            if (!item) return;
            if (count < stack.size()) {
                stack[count] = item;
            } else {
                if (heap.empty()) {
                    heap.reserve(64);
                    heap.assign(stack.begin(), stack.end());
                }
                heap.push_back(item);
            }
            ++count;
        };

        const bool bounded = closing != 0 || stop != none || alternate != none;

        // What has been read, as one node: nothing, the one item, or the run.
        const auto collect = [&]() -> Node* {
            if (count == 0) return nullptr;
            if (count == 1) return stack[0];

            auto* node = compose(Node::Type::Sequence);
            auto slice = arena.allocate<Node*>(count);
            if (heap.empty()) {
                std::copy_n(stack.begin(), count, slice.begin());
            } else {
                std::copy_n(heap.begin(), count, slice.begin());
            }
            node->arguments = slice;
            return node;
        };

        while (true) {
            const Token next = lookahead();

            if (next.empty()) {
                if (bounded) {
                    tracebacks.emplace_back(Traceback::Type::End, next.location,
                          closing != 0
                              ? std::format("Unexpected end of input; expected '{}'", closing)
                              : std::string("Unexpected end of input inside a formula"));
                }
                break;
            }

            if ((stop != none && next.symbol == stop) ||
                (alternate != none && next.symbol == alternate)) {
                if (matched) *matched = next.symbol;
                advance();
                break;
            }

            if (closing != 0 && next.is(closing)) {
                advance();
                break;
            }

            // Only a brace closes anything uninvited; a parenthesis or a
            // bracket is an atom, and ends a run only when it is the
            // character that run was opened to wait for.
            if (next.is(Catcodes::Category::Group, '}')) {
                if (closing != 0) {
                    tracebacks.emplace_back(Traceback::Type::Delimiter, next.location,
                          std::format("Mismatched delimiter '{}'; expected '{}'", next.text, closing));
                    break;
                }

                advance();
                tracebacks.emplace_back(Traceback::Type::Group, next.location,
                      std::format("Unmatched '{}' in formula", next.text));
                continue;
            }

            // TeX's own fraction, written between its parts: everything read
            // so far is the numerator, and the rest of the run, to wherever
            // the run was going to end, the denominator.
            if (next.symbol != none && std::ranges::contains(grammar.infixes, next.symbol)) {
                advance();
                auto* fraction = compose(Node::Type::Fraction);
                fraction->value = next.text;
                fraction->left = collect();
                fraction->right = sequence(closing, stop, alternate, matched);
                return fraction;
            }

            auto* item = step(0);
            if (!item) break;
            push(item);
        }

        return collect();
    }

    Node* Parser::matrix(const Grammar::Bracket& bracket) {
        const Token brace = lookahead();
        if (!brace.is(Catcodes::Category::Group, '{')) {
            tracebacks.emplace_back(Traceback::Type::Group, brace.location,
                                     "A matrix needs a brace group for its cells");
            return nullptr;
        }
        advance();

        // Only the outermost grid's rows are told about: a matrix inside a
        // row of an `align` is part of that row, not a row of its own.
        struct Guard {
            std::size_t& counter;
            explicit Guard(std::size_t& value) : counter(value) { ++counter; }
            ~Guard() { --counter; }
        } guard(grids);
        const bool outermost = grids == 1;
        const auto tell = [this, outermost](const Edge edge) {
            if (outermost && hook) hook(edge);
        };

        std::vector<std::vector<Node*>> table;
        std::vector<Node*> cells;   // the row still being built
        bool closed = false;

        while (!closed) {
            if (cells.empty()) tell(Edge::Opened);

            Symbol ended = none;
            cells.push_back(sequence('}', grammar.row, grammar.column, &ended));

            if (ended == grammar.column) continue;   // another cell in this row

            closed = ended != grammar.row;

            // A trailing `\\` right before the closing brace leaves one
            // empty phantom row of a single empty cell -- dropped, since it
            // is exactly what a document meant by ending each row the same
            // way, not a genuine last row of its own.
            if (closed && cells.size() == 1 && cells.back() == nullptr && !table.empty()) {
                tell(Edge::Dropped);
                break;
            }

            table.push_back(std::move(cells));
            cells.clear();
            tell(Edge::Closed);

            // `\\[4pt]`: the extra space the row asked for below itself --
            // looked for straight after the `\\`, before any blank, as
            // amsmath looks for it -- and `\\*`, which forbids a page break
            // here that nothing would have taken anyway.
            if (!closed) {
                if (const Token next = mouth.lookahead(); next.is('*')) {
                    mouth.read();
                }
                if (const Token next = mouth.lookahead(); next.is('[')) {
                    for (Token skipped = mouth.read(); !skipped.empty() && !skipped.is(']');
                         skipped = mouth.read()) {
                    }
                }
            }
        }

        std::size_t columns = 0;
        for (const std::vector<Node*>& line : table) columns = std::max(columns, line.size());
        if (table.empty() || columns == 0) return nullptr;

        auto* node = compose(Node::Type::Matrix);
        node->open = bracket.open;
        node->close = bracket.close;
        node->value = bracket.preamble;
        node->grid = bracket.grid;
        node->columns = columns;
        node->category = Unicodes::Category::Inner;

        // Row-major, every row filled out to the widest with empty cells.
        auto slice = arena.allocate<Node*>(table.size() * columns);
        std::size_t filled = 0;
        for (const std::vector<Node*>& line : table) {
            for (std::size_t index = 0; index < columns; ++index) {
                slice[filled++] = index < line.size() ? line[index] : nullptr;
            }
        }
        node->arguments = slice;
        return node;
    }

    Node* Parser::argument() {
        const Token next = lookahead();
        if (next.empty()) return nullptr;

        if (next.is(Catcodes::Category::Group, '{')) {
            advance();
            // An empty group is still an argument -- `\\frac{}{2}`,
            // `\\underset{}{=}` -- and sets as nothing, as an empty base does.
            Node* inside = sequence('}');
            return inside ? inside : compose(Node::Type::Sequence);
        }
        return atom(false);
    }

    Node* Parser::atom(const bool scripted) {
        // An operator written before its operand -- a sign, `\\neg` -- takes
        // the atom after it, scripts and all. Not in a script or an argument,
        // which TeX reads as one token: `x^-1` raises the minus alone.
        if (scripted) {
            const Token next = lookahead();
            if (const auto index = static_cast<std::size_t>(next.symbol);
                index < grammar.rules.size() && grammar.rules[index].type == Node::Type::Unary && grammar.rules[index].weight == 0) {

                if (depth >= limit) {
                    tracebacks.emplace_back(Traceback::Type::Recursion, next.location, "Expression nesting limit reached");
                    return nullptr;
                }
                struct Guard {
                    std::size_t& counter;
                    explicit Guard(std::size_t& value) : counter(value) { ++counter; }
                    ~Guard() { --counter; }
                } guard(depth);

                const Token token = advance();
                auto* unary = compose(Node::Type::Unary, token);
                unary->left = atom();
                return script(unary);
            }
        }

        // A script with nothing written before it -- `{}^{14}C`, `^{th}` --
        // stands on an empty base, as it does in TeX, rather than printing
        // its own mark.
        if (const Token next = lookahead(); next.category == Catcodes::Category::Mark || next.is('^') ||
                                            next.category == Catcodes::Category::Index || next.is('_')) {
            Node* empty = compose(Node::Type::Sequence);
            return scripted ? script(empty) : empty;
        }

        const Token token = advance();
        if (token.empty()) return nullptr;

        const Grammar::Rule found = grammar.rule(token.symbol);
        std::string_view name = token.text;
        if (name.starts_with('\\')) name.remove_prefix(1);

        Node* node = nullptr;
        if (token.is(Catcodes::Category::Group, '{')) {
            // A brace group only groups.
            node = compose(Node::Type::Group);
            node->left = sequence('}');
        } else if (found.silent) {
            // A style or a spacing hint the typesetter decides for itself: an
            // empty node, which sets as nothing and takes no space around it.
            node = compose(Node::Type::Sequence);
        } else if (token.symbol == grammar.fences[0]) {
            // `\\left( ... \right)`: a group whose fences grow to fit it. Either
            // side may be `.`, which is a fence that prints nothing -- the way a
            // formula opens with a brace on one side and nothing on the other.
            node = compose(Node::Type::Group);
            node->open = delimiter(advance());
            node->left = sequence(0, grammar.fences[1]);
            node->close = delimiter(advance());
            node->category = Unicodes::Category::Inner;
        } else if (found.bracket != 0) {
            // `\\pmatrix{...}` and every other grid, fenced and lined up the way
            // its name says.
            node = matrix(grammar.brackets[found.bracket - 1]);
        } else if (token.symbol == grammar.array) {
            // `\\array{lcr}{...}`: the preamble comes first, as its own group.
            // Only the column letters matter here; a rule `|` between columns
            // and a paragraph column's width are LaTeX's business in a table,
            // not in a formula, and a `p` column is set flush left.
            std::string preamble;
            if (lookahead().is(Catcodes::Category::Group, '{')) {
                advance();
                int nesting = 0;
                for (Token next = advance(); !next.empty(); next = advance()) {
                    if (next.is(Catcodes::Category::Group, '{')) {
                        ++nesting;
                        continue;
                    }
                    if (next.is(Catcodes::Category::Group, '}')) {
                        if (nesting == 0) break;
                        --nesting;
                        continue;
                    }
                    if (nesting > 0) continue;
                    for (const char letter : next.text) {
                        if (letter == 'l' || letter == 'c' || letter == 'r') preamble += letter;
                        else if (letter == 'p' || letter == 'm' || letter == 'b') preamble += 'l';
                    }
                }
            }
            node = matrix(Grammar::Bracket{.name = grammar.array, .preamble = preamble.empty()
                                                                 ? std::string_view{"c"}
                                                                 : arena.copy(preamble)});
        } else if (found.structural && found.type == Node::Type::Text) {
            // A command that reads words, not a formula: `\\text` and its kin.
            // Kept as the command over one Text node, so the typesetter sets
            // the words in whichever alphabet the command's name asks for.
            // The words are read raw from the expander rather than through
            // lookahead(), which drops blanks: the space in `\\text{if }` is
            // the whole point of it. Macros are still expanded, so a word
            // defined elsewhere reads here.
            node = compose(Node::Type::Sequence);
            node->value = token.text;
            if (lookahead().is(Catcodes::Category::Group, '{')) {
                advance();
                std::string words;
                int nesting = 0;
                for (Token next = mouth.expand(); !next.empty(); next = mouth.expand()) {
                    if (next.is(Catcodes::Category::Group, '{')) {
                        ++nesting;
                        continue;
                    }
                    if (next.is(Catcodes::Category::Group, '}')) {
                        if (nesting == 0) break;
                        --nesting;
                        continue;
                    }
                    if (next.category == Catcodes::Category::Space) {
                        words += ' ';
                        continue;
                    }
                    // A formula inside the words would be set as maths; here
                    // its dollars are dropped and its letters kept, upright.
                    if (next.category == Catcodes::Category::Shift) continue;

                    if (next.text.starts_with('\\') && next.text.size() > 1) {
                        const std::string_view written = next.text.substr(1);
                        // The spaces a document writes out, and the characters
                        // it has to escape to write at all.
                        if (written == " " || written == "," || written == ";" || written == ":" ||
                            written == "quad" || written == "qquad" || written == "~") {
                            words += ' ';
                        } else if (written.size() == 1) {
                            words += written;
                        } else if (const auto symbol = unicodes.get(written)) {
                            // The symbol's character, in the UTF-8 the words are.
                            const std::uint32_t point = symbol->codepoint;
                            if (point < 0x80) {
                                words += static_cast<char>(point);
                            } else if (point < 0x800) {
                                words += static_cast<char>(0xC0 | (point >> 6));
                                words += static_cast<char>(0x80 | (point & 0x3F));
                            } else if (point < 0x10000) {
                                words += static_cast<char>(0xE0 | (point >> 12));
                                words += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
                                words += static_cast<char>(0x80 | (point & 0x3F));
                            } else {
                                words += static_cast<char>(0xF0 | (point >> 18));
                                words += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
                                words += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
                                words += static_cast<char>(0x80 | (point & 0x3F));
                            }
                        }
                        continue;
                    }
                    if (next.is('~')) {
                        words += ' ';
                        continue;
                    }
                    words += next.text;
                }

                auto* text = compose(Node::Type::Text);
                text->value = arena.copy(words);
                auto slice = arena.allocate<Node*>(1);
                slice[0] = text;
                node->arguments = slice;
                node->left = text;
            }
        } else if (found.structural && found.type == Node::Type::Variable) {
            // A delimiter at a fixed size: `\\bigl(`, `\\Bigr]`. The character
            // is the token straight after the command, read as `\\left` reads
            // one, and it opens, closes or stands between as the command's
            // last letter says: `\\bigl`, `\\bigr`, `\\bigm`, and plain `\\big`.
            node = compose(Node::Type::Variable);
            node->codepoint = delimiter(advance());
            node->value = token.text;
            switch (token.text.back()) {
                case 'l': node->category = Unicodes::Category::Opening; break;
                case 'r': node->category = Unicodes::Category::Closing; break;
                case 'm': node->category = Unicodes::Category::Relation; break;
                default: node->category = Unicodes::Category::Ordinary; break;
            }
        } else if (found.structural) {
            // A radical, with its optional degree; a fraction; an accent,
            // which needs to know which mark it is, and nothing else carries
            // that but the name it was written as.
            node = compose(found.type);
            node->value = token.text;
            if (found.type == Node::Type::Radical) {
                if (lookahead().is('[')) {
                    advance();
                    node->left = sequence(']');
                }
                node->right = argument();
            } else if (found.type == Node::Type::Fraction) {
                node->left = argument();
                node->right = argument();
            } else if (found.type == Node::Type::Accent) {
                node->left = argument();
            }
        } else if (token.text == "\\{" || token.text == "\\}" || token.text == "\\|") {
            // The braces a formula prints, escaped so the lexer does not take
            // them for grouping, and the double bar: the same atoms `\\lbrace`,
            // `\\rbrace` and `\\Vert` are, rather than names set as a function's.
            node = compose(Node::Type::Variable);
            node->value = token.text;
            node->codepoint = delimiter(token);
            node->category = token.text == "\\{"   ? Unicodes::Category::Opening
                             : token.text == "\\}" ? Unicodes::Category::Closing
                                                   : Unicodes::Category::Ordinary;
        } else if (token.text.size() == 2 && token.text[0] == '\\' &&
                   std::string_view{"%&#$_"}.contains(token.text[1])) {
            // The characters the lexer reads as instructions, escaped to be
            // printed: in a formula as in the text, each is just itself.
            node = compose(Node::Type::Variable);
            node->value = token.text.substr(1);
            node->codepoint = static_cast<unsigned char>(token.text[1]);
            node->category = Unicodes::Category::Ordinary;
        } else if (const auto symbol = unicodes.get(name)) {
            node = compose(Node::Type::Variable);
            node->value = token.text;
            node->codepoint = symbol->codepoint;
            node->category = symbol->category;
        } else if (token.category == Catcodes::Category::Escape || token.text.starts_with('\\') ||
                   (token.category == Catcodes::Category::Active && token.text == "~")) {
            // A command over its arguments -- or a tie, which is a word space
            // in a formula too, as `\\ ` is: `1~m` is a number and its unit,
            // not a tilde between them.
            node = compose(Node::Type::Sequence);
            node->value = token.text;

            // `\\operatorname*{ess\,sup}`: the starred name sets its limits
            // above and below, as a sum does, and says so by the name it is
            // kept under.
            if (token.symbol == grammar.operatorname && lookahead().is('*')) {
                advance();
                node->value = "\\operatorname*";
            }
            // An optional argument only for a command that reads arguments:
            // after a switch, `{\bf [}`, a bracket is only a bracket.
            if (found.arity > 0 && lookahead().is('[')) {
                advance();
                node->subscript = sequence(']');
            }

            int wanted = found.arity;
            if (wanted < 0) wanted = lookahead().is(Catcodes::Category::Group, '{') ? 1 : 0;

            // Gathered locally first, then allocated at exactly the size that
            // was filled: a command may run out of arguments partway through.
            std::array<Node*, 16> gathered{};
            const int capacity = std::min(wanted, static_cast<int>(gathered.size()));
            std::size_t filled = 0;
            for (int index = 0; index < capacity; ++index) {
                Node* item = argument();
                if (!item) {
                    tracebacks.emplace_back(Traceback::Type::Argument, token.location,
                                             std::format("{} expects {} argument(s)", token.text, wanted));
                    break;
                }
                gathered[filled++] = item;
            }
            if (filled > 0) {
                auto slice = arena.allocate<Node*>(filled);
                std::copy_n(gathered.begin(), filled, slice.begin());
                node->arguments = slice;
                node->left = slice[0];
                if (filled > 1) node->right = slice[1];
            }
        } else {
            // A letter, a digit or a bare mark, resolved the way an operator is.
            node = compose(Node::Type::Variable, token);
        }

        return scripted ? script(node) : node;
    }

    Node* Parser::script(Node* base) {
        if (!base) return nullptr;

        Node* subscript = nullptr;
        Node* superscript = nullptr;

        while (true) {
            const Token next = lookahead();

            if (next.category == Catcodes::Category::Index || next.is('_')) {
                advance();
                Node* item = atom(false);
                if (subscript) {
                    tracebacks.emplace_back(Traceback::Type::Syntax, next.location, "Double subscript");
                } else {
                    subscript = item;
                }
            } else if (next.category == Catcodes::Category::Mark || next.is('^')) {
                advance();
                Node* item = atom(false);
                if (superscript) {
                    tracebacks.emplace_back(Traceback::Type::Syntax, next.location, "Double superscript");
                } else {
                    superscript = item;
                }
            } else if (grammar.rule(next.symbol).silent) {
                // `\\sum\limits_{k}`: a hint between a base and its scripts
                // belongs to the base, and the scripts still attach to it.
                advance();
            } else {
                break;
            }
        }

        if (!subscript && !superscript) {
            return base;
        }

        auto* node = compose(Node::Type::Script);
        node->left = base;
        node->subscript = subscript;
        node->superscript = superscript;
        return node;
    }

    Node* Parser::step(const int priority) {
        Node* left = atom();
        if (!left) return nullptr;

        // After an opening bracket, a relation, an operator or a comma, a
        // plus or a minus has nothing on its left to be between, and is a
        // sign instead -- `(-x)`, `= -1`, `f(x, -y)` -- as TeX reads it.
        if (left->type == Node::Type::Variable &&
            (left->category == Unicodes::Category::Opening || left->category == Unicodes::Category::Relation ||
             left->category == Unicodes::Category::Binary || left->category == Unicodes::Category::Punctuation)) {
            return left;
        }

        while (true) {
            const Token next = lookahead();
            if (next.empty()) break;
            if (next.is(Catcodes::Category::Group, '}')) break;

            const Grammar::Rule found = grammar.rule(next.symbol);

            if (found.type == Node::Type::Unary && found.weight > 0) {
                if (found.weight < priority) break;

                const Token token = advance();
                auto* postfix = compose(Node::Type::Unary, token);
                postfix->left = left;
                left = script(postfix);
                continue;
            }

            if (found.weight > 0 && !found.structural) {
                if (found.weight < priority || (found.weight == priority && !found.right)) {
                    break;
                }

                // An operand is left out where the formula breaks off -- `a =`
                // at the end of an `align` row, carried on in the next --
                // which TeX sets as written, and so is this.
                const Token token = advance();
                auto* binary = compose(found.type, token);
                binary->left = left;
                binary->right = step(found.weight);
                left = script(binary);
                continue;
            }

            break;
        }

        return left;
    }

    Node* Parser::parse(const char closing) {
        return sequence(closing, none);
    }

    Node* Parser::parse(const Symbol closing) {
        return sequence(0, closing);
    }

}