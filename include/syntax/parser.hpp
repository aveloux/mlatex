#pragma once

#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/traceback.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <span>
#include <memory>
#include <vector>

namespace syntax {

    /// @brief Turns the expander's tokens into a flat list of nodes.
    ///
    /// The parser is the other half of the pair the Mouth starts: the Mouth
    /// expands until something no longer expands, and the parser decides
    /// what that something is. A run of characters becomes a Text node, `\\par`
    /// a Paragraph node, and a control sequence someone has bound becomes
    /// whatever its Handler returns -- a formula, a heading, a box. Like the
    /// Mouth, it knows no command by name except `\\par`; everything else is
    /// bound from outside.
    ///
    /// @par Errors
    /// As TeX does it: report and carry on. An undefined control sequence
    /// costs that one token, not the rest of the document, and parsing gives
    /// up only after #tolerance errors.
    ///
    /// @par Use
    /// @code
    /// syntax::Parser parser(mouth, arena);
    /// parser.bind("\\today", [](syntax::Parser& self) -> syntax::Node* {
    ///     return self.arena.compose<syntax::Node>(
    ///         syntax::Node::Type::Text, std::string_view{"today"}, memory::Location{});
    /// });
    ///
    /// const memory::Slice<syntax::Node*> nodes = parser.parse(0);   // to the end
    /// @endcode
    class Parser {
    public:
        /// Implementation of a bound control sequence: whatever node it stands for.
        using Handler = std::function<Node*(Parser&)>;

        /// @brief Binds a parser to an expander and an allocator.
        /// @param mouth Where tokens come from; must outlive the parser.
        /// @param arena Allocator for the nodes.
        ///
        /// Fills in the expander's Mouth::reader, so `\\let` and
        /// `\\ifdefined` see the commands bound here; the destructor empties
        /// it again. The reader holds this parser's address, which is why a
        /// parser can be neither copied nor moved.
        Parser(Mouth& mouth, memory::Arena& arena);
        ~Parser();

        Parser(const Parser&) = delete;
        Parser& operator=(const Parser&) = delete;

        /// @brief Parses until a closing character or the end of input.
        ///
        /// A handler that reads an argument calls this itself with `}`, which
        /// is how a nested structure comes back as its own list of nodes.
        ///
        /// @param closing Group character that ends this list, or 0 to read
        ///                to the end of input.
        /// @return The nodes, in order, allocated in the arena.
        /// @complexity O(n) in the tokens read.
        [[nodiscard]] memory::Slice<Node*> parse(char closing);

        /// @brief Parses until one of several control sequences, a closing
        ///        character, or the end of input.
        ///
        /// How a table reads a cell: the cell ends at a `&` or a `\\\\`, and
        /// the table at the marker its `\\end` leaves, none of which is a
        /// group character. A stop inside a brace group in the cell does not
        /// end it, because that group is read by a parse of its own.
        ///
        /// @param closing Group character that ends this list, or 0 for none.
        /// @param stops   Control sequences that end it as well.
        /// @param matched Set to whichever stop ended it, or none when
        ///                something else did.
        /// @return The nodes read before the stop, in order.
        /// @complexity O(n) in the tokens read, times the number of stops.
        [[nodiscard]] memory::Slice<Node*> parse(char closing, std::span<const Symbol> stops, Symbol& matched);

        /// @brief Attaches a handler to a control sequence.
        /// @param name    Control sequence, interned on the caller's behalf.
        /// @param handler Implementation.
        void bind(std::string_view name, Handler handler);
        /// @brief Attaches a handler to an interned symbol.
        /// @param symbol  Interned control sequence.
        /// @param handler Implementation.
        void bind(Symbol symbol, Handler handler);

        /// @brief Errors recorded so far; the expander keeps its own.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

        static constexpr std::size_t tolerance = 100;   ///< Errors before giving up, as in TeX.

        /// @brief Marks each run of text as it starts with what it is to be
        ///        set in -- the face and the color in use at that moment,
        ///        which the render layer fills in -- so a style changed part
        ///        way through a paragraph changes only what follows it.
        std::function<void(Node&)> stamp{};

        /// @brief A count the render layer raises whenever the face or the
        ///        color changes, or none: a run of text ends where it moves,
        ///        since a primitive run inside the expander -- a block's end
        ///        putting its face back -- may change either between two
        ///        letters the parser reads one after the other.
        const std::uint64_t* changes{nullptr};

        Mouth& mouth;                           ///< The expander tokens come from.
        memory::Arena& arena;                   ///< The allocator nodes are composed in.

    private:
        /// Handler per symbol, each in an allocation of its own so the table can
        /// grow while one runs without moving it; null means none.
        std::vector<std::unique_ptr<const Handler>> handlers{};
        std::vector<Traceback> tracebacks{};    ///< Errors recorded so far.
        Symbol paragraph = none;      ///< `\\par`, the one name the parser knows by itself.
        Symbol assign = none;         ///< `\\@set`, which a register's name standing alone is read through.
        std::size_t nesting = 0;                ///< How many parse() calls are in progress.
    };

}
