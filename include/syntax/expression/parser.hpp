#pragma once

#include "syntax/expression/node.hpp"
#include "syntax/expression/unicodes.hpp"
#include "syntax/mouth.hpp"
#include "syntax/traceback.hpp"
#include "memory/arena.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace syntax::expression {

    /// @brief What a formula is read by: how tightly each operator binds,
    ///        which commands take arguments, which open grids, which are
    ///        read as nothing.
    ///
    /// It belongs to the language, not to any one formula, so it is built
    /// once for a lexicon and every formula's parser reads from it. A rule
    /// is found by the command's interned symbol, an index into #rules, so
    /// asking what a token means is one load however many commands there are.
    class Grammar {
    public:
        /// @brief What one command or character means in a formula.
        struct Rule {
            Node::Type type = Node::Type::Variable;   ///< What kind of node it makes.
            int weight = 0;               ///< How tightly it binds as an operator; 0 for none.
            bool right = false;           ///< True for an operator that groups to the right.
            bool structural = false;      ///< True when it takes brace arguments, not operands.
            int arity = -1;               ///< Its arguments; below zero, one brace group if one is present.
            bool silent = false;          ///< Read and set as nothing: `\\displaystyle`, `\\limits`.
            std::uint8_t bracket = 0;     ///< Which grid the command opens, counted from one; 0 for none.
        };

        /// @brief One grid command: its interned name, what it fences the
        ///        finished grid with, and how it lines its columns up.
        struct Bracket {
            Symbol name = none;                     ///< `\\pmatrix`, `\\cases`, `\\aligned` and the rest.
            std::uint32_t open = 0;                 ///< Left delimiter, 0 for none.
            std::uint32_t close = 0;                ///< @copydoc open
            std::string_view preamble = "c";        ///< Column letters; see Node::value.
            Node::Grid grid = Node::Grid::Spaced;   ///< How its columns are spaced and its cells set.
        };

        /// @brief Interns the names every formula compares against and
        ///        registers the grid commands.
        /// @param lexicon Interning table, shared with the expander.
        explicit Grammar(Lexicon& lexicon);

        /// @brief Makes a command or character an operator, or a command
        ///        that takes brace arguments.
        /// @param symbol     The command.
        /// @param type       What kind of node it makes.
        /// @param weight     How tightly it binds; 0 for a structure.
        /// @param right      True when it groups to the right.
        /// @param structural True when it takes brace arguments.
        /// @param arity      How many arguments; below zero, one brace group if present.
        void bind(Symbol symbol, Node::Type type, int weight = 0, bool right = false,
                  bool structural = false, int arity = -1);

        /// @brief Says how many brace groups a command reads.
        /// @param symbol The command.
        /// @param arity  How many.
        void declare(Symbol symbol, int arity);

        /// @brief Marks a command as read and set as nothing.
        ///
        /// A style switch the typesetter decides for itself -- `\\displaystyle`
        /// -- or one that only matters to TeX's own line breaker --
        /// `\\allowbreak` -- is accepted and dropped, rather than set as its
        /// own name the way an unknown command is.
        ///
        /// @param symbol The command.
        void silence(Symbol symbol);

        /// @brief What a symbol means; the default rule for one with none.
        /// @param symbol The symbol.
        /// @return Its rule.
        /// @complexity O(1).
        [[nodiscard]] Rule rule(Symbol symbol) const noexcept {
            const auto index = static_cast<std::size_t>(symbol);
            return index < rules.size() ? rules[index] : Rule{};
        }

        std::vector<Rule> rules{};   ///< Every rule, indexed by symbol.

        /// `\\left` and `\\right`, the two names that fence a sub-formula with
        /// delimiters grown to its height.
        std::array<Symbol, 2> fences{};

        /// Every grid command: the six matrices and `\\smallmatrix`;
        /// `\\cases`, fenced by a brace on the left alone, as a piecewise
        /// definition is, with `\\dcases` beside it and `\\rcases` fenced on
        /// the right; `\\aligned`, whose columns pair up around their
        /// relations; `\\gathered`, one centred column; and `\\substack`,
        /// the rows under a sum.
        std::array<Bracket, 13> brackets{};

        /// `\\over`, `\\atop` and `\\choose`: TeX's own fractions, written
        /// between their parts rather than before them, which take all of
        /// their group before them as the numerator and the rest after.
        std::array<Symbol, 3> infixes{};

        Symbol array = none;          ///< `\\array`, whose preamble is its own first argument.
        Symbol operatorname = none;   ///< `\\operatorname`, which may be starred.
        Symbol column = none;         ///< `&`, between a grid's cells.
        Symbol row = none;            ///< `\\\\`, between its rows.
    };

    /// @brief Reads one formula into a tree, by a Grammar.
    class Parser {
    public:
        /// @brief Where a grid's row stands, as a Hook is told it.
        enum class Edge : std::uint8_t {
            Opened,    ///< A row is about to be read.
            Closed,    ///< The row just read is kept.
            Dropped    ///< The row just read was the empty one after a last `\\\\`, and is not.
        };

        /// @brief Told about every row of a formula's outermost grid, as it
        ///        is read -- which is how a numbered display gives each row
        ///        of an `align` its own number before the row's `\\label`
        ///        is expanded.
        using Hook = std::function<void(Edge)>;

        /// @brief Binds a math parser to an expander.
        /// @param mouth    Where tokens come from.
        /// @param unicodes Symbol table for math names.
        /// @param grammar  The rules the formula is read by.
        /// @param arena    Allocator for the node tree.
        /// @param style    Inline or display.
        Parser(Mouth& mouth, const Unicodes& unicodes, const Grammar& grammar, memory::Arena& arena,
               Node::Style style = Node::Style::Inline);

        ~Parser();

        Parser(const Parser&) = delete("a formula's parser gives its lookahead back to the expander once, when it is destroyed");
        Parser& operator=(const Parser&) = delete("a formula's parser gives its lookahead back to the expander once, when it is destroyed");

        Node* parse(char closing);
        Node* parse(Symbol closing);

        /// @brief Asks to be told about the rows of this formula's outermost grid.
        /// @param value Called as each row opens and as it closes; replaces any earlier one.
        void watch(Hook value) { hook = std::move(value); }

        /// @brief Builds an empty node of a kind, carrying this parser's style.
        /// @param type What the node is.
        /// @return The node.
        [[nodiscard]] Node* compose(Node::Type type) const;

        /// @brief Builds a node for an operator token, resolving its symbol.
        ///
        /// The name is looked up in the maths tables exactly as a bare symbol
        /// would be, so `\\times` becomes a cross rather than reaching the page
        /// as its own seven letters.
        ///
        /// @param type  What the node is.
        /// @param token The operator, as written.
        /// @return The node, with its code point and class filled in where the
        ///         name was recognised.
        [[nodiscard]] Node* compose(Node::Type type, const Token& token) const;
        [[nodiscard]] Token pending() const noexcept;

        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        [[nodiscard]] Token lookahead() const noexcept;
        Token advance();

        /// @brief Reads atoms until one of up to two stop symbols, or a
        ///        closing character, ends the run.
        ///
        /// @param closing  Character that ends the run, or 0 for none.
        /// @param stop     A control sequence that ends it, or #none.
        /// @param alternate A second control sequence that also ends it, or
        ///                  #none -- how a matrix cell stops at either `&` or
        ///                  `\\` without two passes over the same tokens.
        /// @param matched  When given, set to whichever of @p stop and
        ///                 @p alternate actually ended the run, or #none when
        ///                 it ended some other way.
        /// @return One node holding everything read, or nullptr when nothing was.
        Node* sequence(char closing = 0, Symbol stop = none, Symbol alternate = none,
                       Symbol* matched = nullptr);
        Node* step(int priority = 0);

        /// @brief Reads one atom: a symbol, a group, a command over its
        ///        arguments, a grid, a fenced group -- and, unless it is a
        ///        script or an argument, a sign before it and scripts after.
        /// @param scripted False where TeX reads a single token: a script,
        ///                 an argument written without braces.
        /// @return The atom, or nullptr at the end of the formula.
        Node* atom(bool scripted = true);
        Node* script(Node* base);
        Node* argument();

        /// @brief Which character a delimiter token stands for.
        ///
        /// A single character is itself; `\\{`, `\\}` and `\\|` are the brace
        /// and the double bar; a named one comes from the maths tables, which
        /// carry the angle brackets, floors and ceilings; `.` is none at all.
        ///
        /// @param token The token after `\\left`, `\\right` or `\\big`.
        /// @return Its code point, or 0 for none.
        [[nodiscard]] std::uint32_t delimiter(const Token& token) const;

        /// @brief Reads a grid: a brace group of cells, `&` between columns
        ///        and `\\\\` between rows.
        ///
        /// A row shorter than the longest is filled out with empty cells, as
        /// LaTeX's own grids are; a `[length]` straight after a `\\\\` is the
        /// extra space that row asked for, and is read and set aside.
        ///
        /// @param bracket What fences it and how its columns are lined up.
        /// @return The Matrix node, or nullptr when the group was empty.
        Node* matrix(const Grammar::Bracket& bracket);

        Mouth& mouth;
        const Unicodes& unicodes;
        const Grammar& grammar;
        memory::Arena& arena;
        Node::Style style = Node::Style::Inline;

        mutable Token current{};    ///< filled on demand, returned to the mouth on destruction
        mutable bool primed = false;

        std::vector<Traceback> tracebacks{};   ///< Errors found in this formula.

        Hook hook{};              ///< Told about the outermost grid's rows; may be empty.
        std::size_t grids = 0;    ///< How many grids are open around the token being read.

        /// @brief What ends the innermost run being read: the character it
        ///        was opened to wait for, and the control sequences that stop
        ///        it -- what sequence() was given. A binary sign written last
        ///        in a run looks here, so it takes no operand from past it.
        struct Bound {
            char closing = 0;           ///< Group character that ends it, or 0.
            Symbol stop = none;         ///< Control sequence that ends it, or none.
            Symbol alternate = none;    ///< A second one, or none.
        };
        Bound bound{};   ///< The innermost run's ends.

        std::size_t depth = 0;
        std::size_t limit = 256;
    };

}
