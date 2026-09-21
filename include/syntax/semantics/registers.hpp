#pragma once

#include "syntax/tokens.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace syntax::semantics {

    /// @brief TeX's numbered register banks, with group-scoped assignment.
    ///
    /// Four banks of 256 slots each: integers (`\\count`), dimensions
    /// (`\\dimen`), glue (`\\skip`) and token lists (`\\toks`). Control
    /// sequences are attached to slots with bind(), so `\\tolerance` and
    /// `\\count20` can name the same storage.
    ///
    /// @par Scoping
    /// Every slot carries a *level stamp*: the group depth at which its
    /// current value was established, where 0 means the outermost level.
    ///
    /// - A local assignment pushes an undo record only when the stamp differs
    ///   from the current depth. A thousand assignments to one slot inside one
    ///   group therefore cost one record, not a thousand.
    /// - A `\\global` assignment writes the value and sets the stamp to 0. It
    ///   does **not** walk the undo log.
    /// - pop() discards any record whose slot now has stamp 0, because that
    ///   slot was globalized after the record was taken.
    ///
    /// Every operation is O(1), and pop() is linear only in the number of
    /// distinct slots touched. This is TeX's `eq_level` scheme, and it is what
    /// makes `{ \\count0=1 \\global\\count0=2 }` leave 2 behind rather than
    /// restoring the pre-group value over the global one.
    class Registers {
    public:
        static constexpr std::size_t slots = 256;   ///< Slots per bank.
        static constexpr std::size_t banks = 4;     ///< One per Type.

        /// @brief Which bank a slot lives in.
        enum class Type : std::uint8_t {
            Count,     ///< Integer value register slots
            Dimension, ///< Fixed-point dimension distance slots
            Glue,      ///< Flexible spacing distance slots
            Tokens     ///< Raw token-list register slots (\\toks)
        };

        /// @brief A bank and slot, as bound to a control sequence.
        struct Target {
            Type type;          ///< Which bank.
            std::size_t slot;   ///< Slot number within it.
        };

        /// @brief One scalar entry on the undo log.
        struct Entry {
            Type type;              ///< Bank to restore into.
            std::size_t slot;       ///< Slot to restore.
            std::int32_t value;     ///< Value to put back.
            std::uint32_t level;    ///< Level stamp to put back with it.
        };

        // \toks stores a whole token sequence, not a scalar, so its undo
        // log needs its own entry shape instead of reusing Entry.
        /// @brief One token-list entry on the undo log.
        struct Record {
            std::size_t slot;           ///< Slot to restore.
            std::vector<Token> value;   ///< Token list to put back.
            std::uint32_t level;        ///< Level stamp to put back with it.
        };

        // A scope checkpoint has to remember how far into *both* undo logs
        // it was taken, since scalar and token-list assignments each keep
        // a separate log.
        /// @brief Heights of both undo logs when a group was opened.
        struct Mark {
            std::size_t entries;   ///< Scalar undo-log height.
            std::size_t tokens;    ///< Token-list undo-log height.
        };

        /// @brief Opens a group.
        /// @complexity O(1) amortized.
        void push();

        /// @brief Closes a group, undoing its local assignments.
        /// @complexity O(k) in the number of distinct slots assigned inside it.
        void pop();

        /// @brief Assigns a scalar slot.
        /// @param type   Bank to write.
        /// @param index  Slot number, 0 to 255.
        /// @param value  New value.
        /// @param global True for `\\global`.
        /// @complexity O(1), global included.
        void assign(Type type, std::size_t index, std::int32_t value, bool global);

        /// @brief Reads a scalar slot.
        /// @param type  Bank to read.
        /// @param index Slot number, 0 to 255.
        /// @return The value, or 0 for an out-of-range index or the Tokens bank.
        /// @complexity O(1).
        [[nodiscard]] std::int32_t fetch(Type type, std::size_t index) const noexcept;

        /// @brief Assigns a token-list slot.
        /// @param index  Slot number, 0 to 255.
        /// @param value  New token list; moved from.
        /// @param global True for `\\global`.
        /// @complexity O(1) plus the move.
        void assign(std::size_t index, std::vector<Token> value, bool global);

        /// @brief Reads a token-list slot.
        ///
        /// Named list() rather than get() because get(std::size_t) and
        /// get(Symbol) overloaded against each other: Symbol is std::uint32_t
        /// and the index is std::size_t, so an ordinary literal matched
        /// neither exactly and the call failed to compile as ambiguous. The
        /// two also return entirely different things.
        ///
        /// @param index Slot number, 0 to 255.
        /// @return The token list, or an empty one for an out-of-range index.
        /// @complexity O(1).
        [[nodiscard]] const std::vector<Token>& list(std::size_t index) const noexcept;

        /// @brief Attaches a control sequence to a slot.
        /// @param symbol Interned control sequence name.
        /// @param type   Bank it should address.
        /// @param index  Slot within that bank.
        /// @complexity O(1) average.
        void bind(Symbol symbol, Type type, std::size_t index);

        /// @brief Assigns through a bound control sequence.
        /// @param symbol Interned name, previously passed to bind().
        /// @param value  New value.
        /// @param global True for `\\global`.
        /// @complexity O(1) average.
        void set(Symbol symbol, std::int32_t value, bool global);

        /// @brief Reads through a bound control sequence.
        /// @param symbol Interned name.
        /// @return The value, or 0 when the symbol is not bound. Use bound()
        ///         to tell "the register holds 0" from "not a register".
        /// @complexity O(1) average.
        [[nodiscard]] std::int32_t get(Symbol symbol) const noexcept;

        /// @brief Is this control sequence attached to a slot?
        /// @param symbol Interned name.
        /// @return True when bind() has been called for it.
        /// @complexity O(1) average.
        [[nodiscard]] bool bound(Symbol symbol) const noexcept;

        /// @brief Which slot a control sequence addresses.
        /// @param symbol Interned name.
        /// @return The bank and slot, or std::nullopt when unbound.
        /// @complexity O(1) average.
        [[nodiscard]] std::optional<Target> target(Symbol symbol) const noexcept;

        /// @brief How many groups are open.
        /// @complexity O(1).
        [[nodiscard]] std::size_t depth() const noexcept { return marks.size(); }

        /// @brief How many undo records are outstanding across both logs.
        /// @return A diagnostic, used by the test suite to show that repeated
        ///         assignment to one slot does not grow the log.
        /// @complexity O(1).
        [[nodiscard]] std::size_t saved() const noexcept { return entries.size() + records.size(); }

    private:
        std::array<std::int32_t, slots> counts{};
        std::array<std::int32_t, slots> dimensions{};
        std::array<std::int32_t, slots> glues{};
        std::array<std::vector<Token>, slots> tokens{};
        std::array<std::array<std::uint32_t, slots>, banks> levels{};   ///< Level stamp per bank per slot.
        std::vector<Entry> entries{};
        std::vector<Record> records{};
        std::vector<Mark> marks{};
        std::unordered_map<Symbol, Target> aliases{};
    };

}