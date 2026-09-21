#pragma once

#include "syntax/expression/node.hpp"
#include "syntax/expression/unicodes.hpp"
#include "syntax/mouth.hpp"
#include "syntax/traceback.hpp"
#include "memory/arena.hpp"

#include <cstddef>
#include <vector>

namespace syntax::expression {

    class Parser {
    public:
        struct Rule {
            Node::Type type = Node::Type::Variable;
            int weight = 0;
            bool right = false;
            bool structural = false;
            int arity = -1;     // below zero: one brace group if one is present
        };

        /// @brief Binds a math parser to an expander.
        /// @param engine  Where tokens come from.
        /// @param glyphs  Symbol table for math names.
        /// @param storage Allocator for the node tree.
        /// @param mode    Inline or display.
        Parser(Mouth& engine, const Unicodes& glyphs, memory::Arena& storage,
               Node::Style mode = Node::Style::Inline);

        ~Parser();

        Parser(const Parser&) = delete;
        Parser& operator=(const Parser&) = delete;

        Node* parse();
        Node* parse(char closing);
        Node* parse(Symbol closing);

        void bind(Symbol symbol, Node::Type type, int weight = 0, bool right = false,
                  bool structural = false, int arity = -1);

        void declare(Symbol symbol, int arity);

        [[nodiscard]] Node* compose(Node::Type type) const;
        [[nodiscard]] Token pending() const noexcept;

        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return faults; }

    private:
        [[nodiscard]] Token lookahead() const noexcept;
        Token advance();

        Node* sequence(char closing = 0, Symbol stop = kInvalidSymbol);
        Node* step(int priority = 0);
        Node* atom();
        Node* core();
        Node* group(char closing);
        Node* script(Node* base);
        Node* structural(Node::Type type);
        Node* command(const Token& token);
        Node* argument();

        [[nodiscard]] Rule rule(Symbol symbol) const noexcept;
        void fault(Traceback::Type type, const memory::Location& location, std::string_view message);

        Mouth& mouth;
        const Unicodes& unicodes;
        memory::Arena& arena;
        Node::Style style = Node::Style::Inline;

        mutable Token current{};    ///< filled on demand, returned to the mouth on destruction
        mutable bool primed = false;

        std::vector<Rule> rules{};
        std::vector<Traceback> faults{};
        std::size_t depth = 0;
        std::size_t limit = 256;
    };

}