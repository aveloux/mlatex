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
    namespace {
        template <typename T>
        concept Current = requires(T rule) { rule.arity; };
    }
    static_assert(Current<Parser::Rule>,
                  "stale syntax/expression/parser.hpp: update the header to the one matching this source");


    Parser::Parser(Mouth& engine, const Unicodes& glyphs, memory::Arena& storage, const Node::Style mode)
        : mouth(engine), unicodes(glyphs), arena(storage), style(mode) {
    }

    Parser::~Parser() {
        if (primed && !current.empty()) {
            mouth.inject(std::span{&current, 1});
            primed = false;
        }
    }

    Token Parser::lookahead() const noexcept {
        if (!primed) {
            current = mouth.expand();
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
        const Token token = lookahead();
        current = {};
        primed = false;
        return token;
    }

    Node* Parser::compose(const Node::Type type) const {
        auto* node = arena.compose<Node>(type);
        node->style = style;
        return node;
    }

    Parser::Rule Parser::rule(const Symbol symbol) const noexcept {
        if (const auto index = static_cast<std::size_t>(symbol); index < rules.size()) {
            return rules[index];
        }
        return Rule{};
    }

    void Parser::fault(const Traceback::Type type, const memory::Location& location,
                       const std::string_view message) {
        Logger::fmt(Logger::Type::Parser, Logger::Level::Error,
                    "Math error at {}:{}: {}", location.line, location.column, message);
        faults.emplace_back(type, location, message);
    }

    void Parser::bind(const Symbol symbol, const Node::Type type, const int weight,
                      const bool right, const bool structural, const int arity) {
        const auto index = static_cast<std::size_t>(symbol);
        if (index >= rules.size()) {
            rules.resize(index + 1, Rule{});
        }
        rules[index] = Rule{type, weight, right, structural, arity};
        Logger::fmt(Logger::Type::Parser, Logger::Level::Debug,
                    "Bound symbol {} to rule type={}, weight={}, right={}, structural={}, arity={}",
                    index, static_cast<int>(type), weight, right, structural, arity);
    }

    void Parser::declare(const Symbol symbol, const int arity) {
        const auto index = static_cast<std::size_t>(symbol);
        if (index >= rules.size()) {
            rules.resize(index + 1, Rule{});
        }
        rules[index].arity = arity;
    }

    Node* Parser::sequence(const char closing, const Symbol stop) {
        if (depth >= limit) {
            fault(Traceback::Type::Recursion, lookahead().location, "Expression nesting limit reached");
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

        const bool bounded = closing != 0 || stop != kInvalidSymbol;

        while (true) {
            const Token next = lookahead();

            if (next.empty()) {
                if (bounded) {
                    fault(Traceback::Type::End, next.location,
                          closing != 0
                              ? std::format("Unexpected end of input; expected '{}'", closing)
                              : std::string("Unexpected end of input inside a formula"));
                }
                break;
            }

            if (stop != kInvalidSymbol && next.symbol == stop) {
                advance();
                break;
            }

            if (closing != 0 && next.is(closing)) {
                advance();
                break;
            }

            if (next.is('}') || next.is(')') || next.is(']')) {
                if (closing != 0) {
                    fault(Traceback::Type::Delimiter, next.location,
                          std::format("Mismatched delimiter '{}'; expected '{}'", next.values, closing));
                    break;
                }

                advance();
                fault(Traceback::Type::Group, next.location,
                      std::format("Unmatched '{}' in formula", next.values));
                continue;
            }

            auto* item = step(0);
            if (!item) break;
            push(item);
        }

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
    }

    Node* Parser::group(const char closing) {
        auto* node = compose(Node::Type::Group);

        if (closing == ')') { node->open = '('; node->close = ')'; }
        else if (closing == ']') { node->open = '['; node->close = ']'; }

        node->left = sequence(closing);
        return node;
    }

    Node* Parser::argument() {
        const Token next = lookahead();
        if (next.empty()) return nullptr;

        if (next.is(CatCodes::Category::Group, '{')) {
            advance();
            return sequence('}');
        }
        return core();
    }

    Node* Parser::command(const Token& token) {
        auto* node = compose(Node::Type::Sequence);
        node->value = token.values;

        const Rule found = rule(token.symbol);

        if (lookahead().is('[')) {
            advance();
            node->subscript = sequence(']');
        }

        int wanted = found.arity;
        if (wanted < 0) {
            wanted = lookahead().is(CatCodes::Category::Group, '{') ? 1 : 0;
        }

        if (wanted <= 0) {
            return node;
        }

        // Gathered locally first, then allocated at exactly the size that was
        // filled. Building a Slice by hand would assume its field order, and
        // a command may also run out of arguments partway through.
        std::array<Node*, 16> gathered{};
        const int capacity = wanted < static_cast<int>(gathered.size())
                                 ? wanted
                                 : static_cast<int>(gathered.size());
        std::size_t filled = 0;

        for (int index = 0; index < capacity; ++index) {
            Node* item = argument();
            if (!item) {
                fault(Traceback::Type::Argument, token.location,
                      std::format("{} expects {} argument(s)", token.values, wanted));
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

        return node;
    }

    Node* Parser::structural(const Node::Type type) {
        auto* node = compose(type);

        if (type == Node::Type::Radical) {
            if (lookahead().is('[')) {
                advance();
                node->left = sequence(']');
            }
            node->right = argument();
        } else if (type == Node::Type::Fraction) {
            node->left = argument();
            node->right = argument();
        } else if (type == Node::Type::Accent) {
            node->left = argument();
        }

        return node;
    }

    Node* Parser::core() {
        const Token token = advance();
        if (token.empty()) {
            return nullptr;
        }

        if (token.is(CatCodes::Category::Group, '{')) return group('}');
        if (token.is('(')) return group(')');

        if (const auto index = static_cast<std::size_t>(token.symbol);
            index < rules.size() && rules[index].structural) {
            return structural(rules[index].type);
        }

        std::string_view name = token.values;
        if (name.starts_with('\\')) {
            name.remove_prefix(1);
        }

        if (const auto symbol = unicodes.query(name)) {
            auto* node = compose(Node::Type::Variable);
            node->value = token.values;
            node->codepoint = symbol->codepoint;
            node->category = symbol->category;
            return node;
        }

        if (token.category == CatCodes::Category::Escape || token.values.starts_with('\\')) {
            return command(token);
        }

        auto* node = compose(Node::Type::Variable);
        node->value = token.values;
        return node;
    }

    Node* Parser::atom() {
        const Token next = lookahead();

        if (const auto index = static_cast<std::size_t>(next.symbol);
            index < rules.size() && rules[index].type == Node::Type::Unary && rules[index].weight == 0) {

            if (depth >= limit) {
                fault(Traceback::Type::Recursion, next.location, "Expression nesting limit reached");
                return nullptr;
            }
            struct Guard {
                std::size_t& counter;
                explicit Guard(std::size_t& value) : counter(value) { ++counter; }
                ~Guard() { --counter; }
            } guard(depth);

            const Token token = advance();
            auto* unary = compose(Node::Type::Unary);
            unary->value = token.values;
            unary->left = atom();
            return script(unary);
        }

        return script(core());
    }

    Node* Parser::script(Node* base) {
        if (!base) return nullptr;

        Node* subscript = nullptr;
        Node* superscript = nullptr;

        while (true) {
            const Token next = lookahead();

            if (next.category == CatCodes::Category::Index || next.is('_')) {
                advance();
                Node* item = core();
                if (subscript) {
                    fault(Traceback::Type::Syntax, next.location, "Double subscript");
                } else {
                    subscript = item;
                }
            } else if (next.category == CatCodes::Category::Mark || next.is('^')) {
                advance();
                Node* item = core();
                if (superscript) {
                    fault(Traceback::Type::Syntax, next.location, "Double superscript");
                } else {
                    superscript = item;
                }
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

        while (true) {
            const Token next = lookahead();
            if (next.empty()) break;
            if (next.is('}') || next.is(')') || next.is(']')) break;

            const Rule found = rule(next.symbol);

            if (found.type == Node::Type::Unary && found.weight > 0) {
                if (found.weight < priority) break;

                const Token token = advance();
                auto* postfix = compose(Node::Type::Unary);
                postfix->value = token.values;
                postfix->left = left;
                left = script(postfix);
                continue;
            }

            if (found.weight > 0 && !found.structural) {
                if (found.weight < priority || (found.weight == priority && !found.right)) {
                    break;
                }

                const Token token = advance();
                auto* binary = compose(found.type);
                binary->value = token.values;
                binary->left = left;
                binary->right = step(found.weight);
                if (!binary->right) {
                    fault(Traceback::Type::Syntax, token.location,
                          std::format("Missing right operand for '{}'", token.values));
                }
                left = script(binary);
                continue;
            }

            break;
        }

        return left;
    }

    Node* Parser::parse() {
        return sequence(0, kInvalidSymbol);
    }

    Node* Parser::parse(const char closing) {
        return sequence(closing, kInvalidSymbol);
    }

    Node* Parser::parse(const Symbol closing) {
        return sequence(0, closing);
    }

}