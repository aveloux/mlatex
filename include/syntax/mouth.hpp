#pragma once

#include "syntax/cursor.hpp"
#include "syntax/glossary.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/semantics/scope.hpp"
#include "syntax/tokens.hpp"
#include "syntax/traceback.hpp"
#include "memory/arena.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace syntax {

    namespace semantics {
        class Union;
        class Registers;
    }

    /// @brief The macro expander: TeX's "mouth".
    ///
    /// Sits between Cursor (the token stack) and Parser. expand() returns the
    /// next token that no longer expands, having run macros and primitive
    /// handlers until something unexpandable surfaces.
    ///
    /// The expander knows no primitive by name. Everything a document can say
    /// -- `\\define`, `\\global`, `\\begin` -- is a Handler some module binds;
    /// what is here is only the machinery those handlers are written against:
    /// reading, expanding, scoping, and one table of macros and one of handlers.
    ///
    /// @par Scoping
    /// Macro definitions are group-scoped by the same level-stamp scheme
    /// Registers and Catcodes use: each macro slot records the group depth at
    /// which its current meaning was established, a global definition sets
    /// that stamp to 0, and pop() discards records for slots that have since
    /// been made global. Defining is O(1) whether local or global.
    ///
    /// @par Runaway expansion
    /// expand() counts the expansions it performs while trying to produce one
    /// token and gives up past #budget, so `\\define\\a{\\a}` terminates instead
    /// of spinning. It also caps how many tokens may be pending at once, which
    /// catches `\\define\\a{\\a\\a}`. Either sets #error; callers must check it,
    /// because an abandoned expand() returns an empty token and so is otherwise
    /// indistinguishable from end of input.
    ///
    /// @par Use
    /// @code
    /// syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    /// mouth.bind("\\hello", [](syntax::Mouth& self) {
    ///     self.ingest("Hello");              // a primitive that expands to text
    /// });
    /// mouth.ingest("\\hello, world");
    ///
    /// for (syntax::Token token = mouth.expand(); !token.empty(); token = mouth.expand()) {
    ///     // H, e, l, l, o, the comma, a space, w, o, r, l, d
    /// }
    /// @endcode
    class Mouth {
    public:
        /// Primitive implementation, invoked when its control sequence surfaces.
        using Handler = std::function<void(Mouth&)>;

        /// @brief One parameter in a macro's parameter text.
        struct Parameter {
            bool optional = false;              ///< LaTeX-style `[...]` parameter.
            std::vector<Token> delimiters{};    ///< Tokens that terminate the argument.
            std::vector<Token> fallbacks{};     ///< Default, used when an optional argument is absent.

            /// xparse's `s` and `t`: the one token looked for, then what the
            /// argument is when that token is there -- #fallbacks being what
            /// it is when not. Empty for every other kind of parameter.
            std::vector<Token> present{};
        };

        /// @brief What the reader past this expander -- a Parser -- makes of names.
        ///
        /// A Parser builds nodes for commands Mouth never sees expand, yet
        /// `\\let` has to copy those as well as Mouth's own, and
        /// `\\ifdefined` has to count them. A Parser reading from this
        /// expander fills both in, and empties them again when it goes.
        struct Reader {
            std::function<bool(Symbol)> means{};          ///< Whether it gives a name a meaning.
            std::function<bool(Symbol, Symbol)> lend{};   ///< Gives the first name the second's meaning; false when the second had none.
        };

        /// @brief A user-defined macro.
        struct Macro {
            std::vector<Parameter> parameters{};   ///< Parameter text, left to right.
            std::vector<Token> prefix{};           ///< What must stand before the first argument, as plain
                                                   ///< TeX's `\\def\\x[#1]` asks for its `[`.
            std::vector<Token> body{};             ///< Replacement text.
            bool literal = false;                  ///< Set by define(): the body needs no substitution.
            bool spanning = false;                 ///< `\\long`: arguments may contain `\\par`.
            bool isolated = false;                 ///< `\\outer`: may not appear inside an argument.
            bool implicit = false;                 ///< Made by `\\let` from a character, which \\ifx takes it for.
            bool robust = false;                   ///< e-TeX's `\\protected`: kept as written by `\\edef`.
        };

        /// @brief Binds an expander to a token stream and an engine state.
        /// @param stream  Initial token stack; may be empty and fed later by ingest().
        /// @param state   Registers, catcodes and scope stack, shared with the caller.
        /// @param lexicon   Interning table, shared with the caller.
        /// @param arena Allocator for anything the expander composes.
        Mouth(Cursor stream, semantics::Union& state, Lexicon& lexicon, memory::Arena& arena);

        /// @brief Opens a scope of a given kind.
        ///
        /// The kind is recorded so that pop() can report a mismatch. Passing
        /// the right kind is what lets `\\begin{a} ... }` be diagnosed.
        ///
        /// @param type What is being opened; a plain group by default.
        /// @complexity O(1) amortized.
        void push(semantics::Scope::Type type = semantics::Scope::Type::Group);

        /// @brief Closes a scope and checks its kind.
        /// @param expected The kind the caller believes it is closing.
        /// @return True when the open scope matched; false on a mismatch or an
        ///         unbalanced pop, both of which are recorded in traceback().
        /// @complexity O(k) in the number of distinct macros defined inside.
        bool pop(semantics::Scope::Type expected);

        /// @brief How many groups are currently open.
        /// @complexity O(1).
        [[nodiscard]] std::size_t nesting() const noexcept { return marks.size(); }

        /// @brief Whether a formula is being read: what TeX's `\\ifmmode` asks.
        ///
        /// The innermost formula or box decides, so the words of a `\\text`
        /// inside a formula, a box of their own, are text again.
        ///
        /// @return True inside `$...$` and every other formula.
        /// @complexity O(d) in the scopes open.
        [[nodiscard]] bool formula() const noexcept {
            for (auto type = types.rbegin(); type != types.rend(); ++type) {
                if (*type == semantics::Scope::Type::Equations) return true;
                if (*type == semantics::Scope::Type::Box) return false;
            }
            return false;
        }

        /// @brief Takes the next token without expanding it.
        /// @return The token, or an empty one at end of input.
        /// @complexity O(1).
        Token read() noexcept;

        /// @brief Takes the next token that does not expand.
        ///
        /// Runs handlers and macros until something unexpandable surfaces.
        ///
        /// @return The token, or an empty one at end of input **or** on an
        ///         abandoned expansion. Check #error to tell those apart.
        /// @complexity O(1) amortized per token in the common case; bounded by
        ///             #budget expansions before it gives up.
        Token expand();

        /// @brief Expands the next token once: a macro into its body, a
        ///        primitive by running it. What `\\expandafter` asks of the
        ///        token after next; expand() is this, repeated.
        /// @return False, having taken nothing, when the next token does not expand.
        /// @complexity O(n) in the macro's body and arguments.
        bool step();

        /// @brief Collects one macro argument from the stream.
        /// @param parameter How the argument is delimited.
        /// @param allow     Non-zero when `\\par` is permitted inside it (`\\long`).
        /// @return The argument's tokens, with one layer of outer braces stripped
        ///         when the whole argument was a single group.
        std::vector<Token> argument(const Parameter& parameter, int allow);

        /// @brief Lexes a source buffer and pushes it onto the stream.
        ///
        /// The cursor is a stack, so the buffer ingested **last** is read
        /// **first**. The tokens view the buffer, which must outlive them.
        ///
        /// @param source Text to lex. Illegal bytes are reported into traceback().
        /// @param origin Where every token is to say it came from, in place of
        ///               where each stands in @p source: a Location of line 0
        ///               for text that belongs to no line of the document. Left
        ///               out, each token keeps its own.
        void ingest(std::string_view source, std::optional<memory::Location> origin = std::nullopt);

        /// @brief Reads what the document has not yet been read of under a
        ///        character's new category, as TeX reads characters it has
        ///        not reached: `\\catcode`\\!=13` makes every `!` still to
        ///        come active, `\\catcode`\\|=0` makes `|textbf` a control
        ///        word, a comment character drops the rest of its line, and a
        ///        character made a letter carries on the control word it
        ///        follows. Tokens a macro has already made keep theirs, and a
        ///        change made in a group reaches only as far as the group's
        ///        end -- its `}`, `\\endgroup` or `\\end` -- where it is undone.
        /// @param character The character.
        /// @param category  What it now is.
        /// @param global    Whether the change outlives the group it is made in.
        /// @complexity O(n) in the document's tokens still unread.
        void recategorize(char character, Catcodes::Category category, bool global);

        /// @brief Attaches a primitive implementation to a name.
        /// @param name    Control sequence, interned on the caller's behalf.
        /// @param handler Implementation.
        void bind(std::string_view name, Handler handler);
        /// @brief Attaches a primitive implementation to an interned symbol.
        /// @param symbol  Interned control sequence.
        /// @param handler Implementation.
        void bind(Symbol symbol, Handler handler);

        /// @brief Gives a name the meaning a primitive already has, as
        ///        `\\let` does: the same implementation, and the same
        ///        primitive to `\\ifx` -- `\\csname undefined\\endcsname` is
        ///        `\\relax` itself.
        /// @param symbol Interned control sequence to bind.
        /// @param source The primitive it is to mean; nothing happens when it
        ///               has no implementation.
        void lend(Symbol symbol, Symbol source);

        /// @brief Whether a robust macro -- `\\protected` -- is handed back as
        ///        written by expand() rather than expanded, as `\\edef` keeps
        ///        one.
        /// @param keep True while an `\\edef` reads.
        /// @return What it was before.
        bool robust(const bool keep) noexcept { return std::exchange(keeping, keep); }

        /// @brief The primitive a name was bound as, followed through lend():
        ///        itself for one bound directly.
        /// @complexity O(1).
        [[nodiscard]] Symbol primitive(Symbol symbol) const noexcept;

        /// @brief Defines a macro.
        /// @param symbol Interned control sequence.
        /// @param macro  Definition; moved from.
        /// @param global True to outlive every open group, as `\\global` asks.
        /// @complexity O(1) amortized, global or not.
        void define(Symbol symbol, Macro macro, bool global);

        /// @brief Removes a macro definition.
        /// @param symbol Interned control sequence.
        /// @param global True to remove it past every open group.
        /// @complexity O(1), global or not.
        void forget(Symbol symbol, bool global);

        /// @brief Puts a token by to be read as the innermost open group
        ///        closes: TeX's `\\aftergroup`. Outside every group it is
        ///        never read, as TeX's is not.
        /// @param token The token.
        /// @complexity O(1) amortized.
        void defer(const Token token) { deferred.emplace_back(marks.size(), token); }

        /// @brief Peeks into the token stream without consuming or expanding.
        /// @param offset How far ahead to look, 0 for the next token.
        /// @return The token, or an empty one past the end.
        /// @complexity O(1).
        [[nodiscard]] Token lookahead(std::size_t offset = 0) const noexcept;

        /// @brief The macro a control sequence means, if it means one.
        /// @param symbol Interned control sequence.
        /// @return The definition, or nullptr when undefined. The pointee
        ///         lives in this object; it is invalidated by the next
        ///         define() or forget().
        /// @complexity O(1).
        [[nodiscard]] const Macro* macro(Symbol symbol) const noexcept;

        /// @brief The primitive a control sequence means, if it means one.
        /// @param symbol Interned control sequence.
        /// @return The handler, or nullptr when none is bound.
        /// @complexity O(1).
        [[nodiscard]] const Handler* handler(Symbol symbol) const noexcept;

        /// @brief Whether a control sequence means anything at all: a macro,
        ///        a primitive, or a command the #reader builds.
        /// @param symbol Interned control sequence.
        /// @complexity O(1).
        [[nodiscard]] bool known(Symbol symbol) const {
            return macro(symbol) != nullptr || handler(symbol) != nullptr || (reader.means && reader.means(symbol));
        }

        Reader reader{};   ///< The Parser reading from this expander, as far as names go.

        /// @brief Errors recorded so far.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

        /// @brief The raw token stack behind read() and expand().
        ///
        /// Mouth itself scans no number and knows no grammar past `\\par`;
        /// Number reads one off whichever stream it is given, this one
        /// included, which is what this exists for. A handler that needs to
        /// push tokens back reaches the same stack's inject() through here
        /// rather than through a second method on Mouth. Everything else
        /// here is bind(), define() and forget(), and the plumbing under
        /// them.
        ///
        /// @return The stream, mutable: reading from it is reading from Mouth.
        /// @complexity O(1).
        [[nodiscard]] Cursor& stream() noexcept { return cursor; }

    private:
        /// @brief One entry on the macro undo log.
        ///
        /// Holds a shared pointer rather than a Macro by value: taking a record
        /// is then an atomic increment instead of deep-copying two vectors.
        struct Record {
            Symbol symbol = none;            ///< Which macro slot.
            std::shared_ptr<const Macro> macro{};      ///< Meaning to put back; null means undefined.
            std::uint32_t level = 0;                   ///< Level stamp to put back with it.
        };

    public:
        semantics::Union& state;   ///< Registers, catcodes and scopes, shared with the caller.
        Lexicon& lexicon;          ///< Interning table, shared with the caller.
        memory::Arena& arena;      ///< Allocator for what handlers compose.

        /// Set when an expansion was abandoned: after a runaway expansion or a
        /// pending-token overflow. An empty token from expand() means end of
        /// input only when this is false.
        bool error = false;

        /// Whether the text stands between paragraphs -- TeX's vertical mode,
        /// what `\\ifvmode` asks -- rather than inside one. The parser says
        /// which as it reads: a character starts a paragraph, and a
        /// paragraph's end or a block standing on its own ends one. A
        /// document starts between paragraphs.
        bool vertical = true;

    private:
        Cursor cursor{};           ///< The token stack being read.

        Symbol paragraph = none;   ///< `\\par`, the one name the expander knows by itself.
        Glossary glossary{};     ///< Meanings for the names nothing else here gives one.

        std::vector<std::size_t> marks{};                ///< Undo-log height at each open scope.
        std::vector<semantics::Scope::Type> types{}; ///< What each open scope was opened as.
        std::vector<Traceback> tracebacks{}; ///< Errors recorded so far.
        std::vector<Record> records{}; ///< The macro undo log.
        std::vector<std::pair<std::size_t, Token>> deferred{};   ///< \\aftergroup's tokens, each with its group's depth.

        std::vector<std::shared_ptr<const Macro> > macros{}; ///< Null slot means undefined.
        std::vector<std::uint32_t> levels{}; ///< Group depth of each definition; 0 is outermost.
        /// Primitive per symbol, each in an allocation of its own so a primitive
        /// that binds another while it runs is never moved out from under
        /// itself when the table grows; null means none.
        std::vector<std::unique_ptr<const Handler>> handlers{};
        std::vector<Symbol> lent{};   ///< By name, the primitive lend() bound it as; none for one bound directly.
        bool keeping{false};          ///< Robust macros are kept as written: an `\\edef` is reading.

        std::size_t depth = 0; ///< Re-entrant expand() calls in progress.
        std::size_t limit = 256; ///< Re-entrant expand() calls allowed.
        std::size_t budget = 10000; ///< Expansions per token produced.
        std::size_t capacity = 4u << 20; ///< Most tokens pending at once.
    };
}
