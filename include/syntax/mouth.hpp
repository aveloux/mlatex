#pragma once

#include "syntax/cursor.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/semantics/scope.hpp"
#include "syntax/tokens.hpp"
#include "syntax/traceback.hpp"
#include "memory/arena.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
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
    /// @par Scoping
    /// Macro definitions are group-scoped by the same level-stamp scheme
    /// Registers and CatCodes use: each macro slot records the group depth at
    /// which its current meaning was established, `\\global` sets that stamp
    /// to 0, and pop() discards records for slots that have since been
    /// globalized. Defining is O(1) whether local or global.
    ///
    /// @par Runaway expansion
    /// expand() counts the expansions it performs while trying to produce one
    /// token and gives up past #budget, so `\\def\\a{\\a}` terminates instead
    /// of spinning. It also caps how many tokens may be pending at once, which
    /// catches `\\def\\a{\\a\\a}`. Either abort sets failed(); callers must
    /// check it, because an aborted expand() returns an empty token and so is
    /// otherwise indistinguishable from end of input.
    class Mouth {
    public:
        /// Primitive implementation, invoked when its control sequence surfaces.
        using Handler = std::function<void(Mouth&)>;

        /// @brief One parameter in a macro's parameter text.
        struct Parameter {
            bool optional = false;              ///< LaTeX-style `[...]` parameter.
            std::vector<Token> delimiters{};    ///< Tokens that terminate the argument.
            std::vector<Token> fallbacks{};     ///< Default, used when an optional argument is absent.
        };

        /// @brief A user-defined macro.
        struct Macro {
            std::vector<Parameter> parameters{};   ///< Parameter text, left to right.
            std::vector<Token> body{};             ///< Replacement text.
            bool active = false;                   ///< False once undefined.
            bool literal = false;                  ///< Engine-maintained: body needs no substitution.
            bool spanning = false;                 ///< `\\long`: arguments may contain `\\par`.
            bool isolated = false;                 ///< `\\outer`: may not appear inside an argument.
        };

        /// @brief One entry on the macro undo log.
        ///
        /// Holds a shared pointer rather than a Macro by value: taking a record
        /// is then an atomic increment instead of deep-copying two vectors.
        struct Record {
            Symbol symbol = kInvalidSymbol;            ///< Which macro slot.
            std::shared_ptr<const Macro> macro{};      ///< Meaning to put back; null means undefined.
            std::uint32_t level = 0;                   ///< Level stamp to put back with it.
        };

        /// @brief Binds an expander to a token stream and an engine state.
        /// @param stream  Initial token stack; may be empty and fed later by ingest().
        /// @param state   Registers, catcodes and scope stack, shared with the caller.
        /// @param names   Interning table, shared with the caller.
        /// @param storage Allocator for anything the expander composes.
        Mouth(Cursor stream, semantics::Union& state, Lexicon& names, memory::Arena& storage);

        /// @brief The interning table this expander was built with.
        [[nodiscard]] Lexicon& lexicon() noexcept { return lexicon_; }
        [[nodiscard]] const Lexicon& lexicon() const noexcept { return lexicon_; }   ///< @copydoc lexicon()

        /// @brief The engine state this expander was built with.
        [[nodiscard]] semantics::Union& state() noexcept { return domain; }
        [[nodiscard]] const semantics::Union& state() const noexcept { return domain; }   ///< @copydoc state()

        /// @brief The allocator this expander was built with.
        [[nodiscard]] memory::Arena& arena() const noexcept { return arena_; }

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
        ///         unbalanced pop, both of which are recorded in history().
        /// @complexity O(k) in the number of distinct macros defined inside.
        bool pop(semantics::Scope::Type expected);

        /// @brief Closes whatever scope is on top, without checking its kind.
        /// @complexity O(k) in the number of distinct macros defined inside.
        void pop();

        /// @brief Marks the next definition `\\global`.
        void globalize() noexcept;
        /// @brief Consumes and returns the pending `\\global` flag.
        /// @return Non-zero when the next definition should be global.
        int unglobal() noexcept;

        /// @brief Marks the next definition `\\long`.
        void longify() noexcept;
        /// @brief Consumes and returns the pending `\\long` flag.
        int unlong() noexcept;

        /// @brief Marks the next definition `\\outer`.
        void outerize() noexcept;
        /// @brief Consumes and returns the pending `\\outer` flag.
        int unouter() noexcept;

        /// @brief Queues a token to be re-injected when the current group closes.
        /// @param token Token to hold.
        void defer(const Token& token);

        /// @brief Queues a token to be re-injected after the next definition.
        /// @param token Token to hold.
        void schedule(const Token& token);

        /// @brief Takes the next token without expanding it.
        /// @return The token, or an empty one at end of input.
        /// @complexity O(1).
        Token read() noexcept;

        /// @brief Takes the next token that does not expand.
        ///
        /// Runs handlers and macros until something unexpandable surfaces.
        ///
        /// @return The token, or an empty one at end of input **or** on an
        ///         aborted expansion. Check failed() to tell those apart.
        /// @complexity O(1) amortized per token in the common case; bounded by
        ///             #budget expansions before it gives up.
        Token expand();

        /// @brief Performs at most one expansion step.
        /// @return 1 when something was expanded, 0 when the next token is
        ///         unexpandable or the stream is empty.
        int step();

        /// @brief Collects one macro argument from the stream.
        /// @param parameter How the argument is delimited.
        /// @param allow     Non-zero when `\\par` is permitted inside it (`\\long`).
        /// @return The argument's tokens, with one layer of outer braces stripped
        ///         when the whole argument was a single group.
        std::vector<Token> argument(const Parameter& parameter, int allow);

        /// @brief Pushes tokens back onto the front of the stream.
        /// @param batch Tokens to inject, in reading order.
        /// @complexity O(k).
        void inject(std::span<const Token> batch);

        /// @brief Lexes a source buffer and pushes it onto the stream.
        ///
        /// The cursor is a stack, so the buffer ingested **last** is read
        /// **first**.
        ///
        /// @param source Text to lex. Illegal bytes are reported into history().
        void ingest(std::string_view source);

        /// @brief Attaches a primitive implementation to a name.
        /// @param name    Control sequence, interned on the caller's behalf.
        /// @param handler Implementation.
        void bind(std::string_view name, Handler handler);
        /// @brief Attaches a primitive implementation to an interned symbol.
        /// @param symbol  Interned control sequence.
        /// @param handler Implementation.
        void bind(Symbol symbol, Handler handler);

        /// @brief Defines a macro.
        /// @param name  Control sequence, interned on the caller's behalf.
        /// @param macro Definition; moved from.
        void define(std::string_view name, Macro macro);
        /// @brief Defines a macro.
        /// @param symbol Interned control sequence.
        /// @param macro  Definition; moved from.
        /// @complexity O(1) amortized, `\\global` included.
        void define(Symbol symbol, Macro macro);

        /// @brief Removes a macro definition.
        /// @param name Control sequence, interned on the caller's behalf.
        void undefine(std::string_view name);
        /// @brief Removes a macro definition.
        /// @param symbol Interned control sequence.
        /// @complexity O(1), `\\global` included.
        void undefine(Symbol symbol);

        /// @brief Peeks into the token stream without consuming or expanding.
        /// @param offset How far ahead to look, 0 for the next token.
        /// @return The token, or an empty one past the end.
        /// @complexity O(1).
        [[nodiscard]] Token lookahead(std::size_t offset = 0) const noexcept;

        /// @brief Looks up a macro definition.
        /// @param symbol Interned control sequence.
        /// @return Pointer to the definition, or nullptr when undefined. The
        ///         pointee lives in this object; it is invalidated by the next
        ///         define() or undefine().
        /// @complexity O(1).
        [[nodiscard]] const Macro* lookup(Symbol symbol) const noexcept;

        /// @brief Looks up a primitive implementation.
        /// @param symbol Interned control sequence.
        /// @return Pointer to the handler, or nullptr when none is bound.
        /// @complexity O(1).
        [[nodiscard]] const Handler* primitive(Symbol symbol) const noexcept;

        /// @brief Errors recorded so far.
        [[nodiscard]] const std::vector<Traceback>& history() const noexcept { return tracebacks; }

        /// @brief Did an expansion abort?
        /// @return True after a runaway expansion or a pending-token overflow.
        ///         An empty token from expand() means end of input only when
        ///         this is false.
        [[nodiscard]] bool failed() const noexcept { return aborted; }

        /// @brief Clears the aborted flag so expansion can be retried.
        void forgive() noexcept { aborted = false; }

        /// @brief Takes the recorded errors, leaving the list empty.
        /// @return Everything history() would have returned.
        std::vector<Traceback> drain();

        /// @brief Scans an integer from the stream.
        /// @param ledger Registers to resolve `\\count` and bound names against.
        /// @param count  Interned `\\count`, or kInvalidSymbol to disable it.
        /// @return The value, or std::nullopt when the stream does not start with one.
        std::optional<std::int32_t> integer(const semantics::Registers& ledger, Symbol count);

        /// @brief Scans a dimension from the stream.
        /// @param ledger Registers to resolve register references against.
        /// @param count  Interned `\\count`, or kInvalidSymbol to disable it.
        /// @param unit   Interned `\\dimen`, or kInvalidSymbol to disable it.
        /// @return The value in scaled points, or std::nullopt.
        std::optional<std::int32_t> dimension(const semantics::Registers& ledger, Symbol count, Symbol unit);

        /// @brief How many undo records are outstanding.
        /// @return A diagnostic, used by the test suite to show that repeated
        ///         definition does not grow the log.
        [[nodiscard]] std::size_t saved() const noexcept { return records.size(); }

    private:
        void abort(Traceback::Type type, std::string_view message);
        void reserve(std::size_t slot);

        Cursor cursor{};
        semantics::Union& domain;
        Lexicon& lexicon_;
        memory::Arena& arena_;

        Symbol paragraph = kInvalidSymbol;

        std::vector<std::size_t> marks{};                ///< Undo-log height at each open scope.
        std::vector<semantics::Scope::Type> kinds{};     ///< What each open scope was opened as.
        std::vector<std::vector<Token>> deferred{};
        std::vector<Traceback> tracebacks{};
        std::vector<Record> records{};

        std::vector<std::shared_ptr<const Macro>> macros{};   ///< Null slot means undefined.
        std::vector<std::uint32_t> levels{};              ///< Group depth of each definition; 0 is outermost.
        std::vector<Handler> handlers{};

        int global = 0;
        int spanning = 0;
        int isolated = 0;
        int suppressed = 0;
        bool aborted = false;

        std::optional<Token> scheduled{};

        std::size_t depth = 0;
        std::size_t limit = 256;            ///< Re-entrant expand() calls.
        std::size_t budget = 10000;         ///< Expansions per token produced.
        std::size_t capacity = 4u << 20;    ///< Most tokens pending at once.
    };

}