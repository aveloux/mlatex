#pragma once

#include "syntax/lexicon.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace syntax::semantics {

    /// @brief TeX's numbered register banks, with group-scoped assignment.
    ///
    /// Nine banks of 256 slots: integers (`\count`), dimensions (`\dimen`),
    /// glue (`\skip`), the width, height and depth of each box register
    /// (`\wd`, `\ht`, `\dp`), and the stretch, shrink and orders of each glue
    /// register, all held in scaled points where they measure. A box's three
    /// are set when the box is, and a glue register's three when it is, so
    /// each reads as any length does.
    /// Control sequences are attached either to a whole bank, as `\count` is,
    /// or to one slot, as `\tolerance` is -- both through bind(), which does
    /// either depending on whether a slot is given -- and both lookups are an
    /// index on the symbol.
    ///
    /// @par Scoping
    /// Each slot carries a level stamp, exactly as CatCodes does: a local
    /// assignment records the old value once per group, a global one stamps
    /// the slot 0, and pop() skips records for slots stamped 0 since. Every
    /// operation is O(1).
    ///
    /// @par Use
    /// @code
    /// registers.set(Registers::Type::Count, 0, 42, false);
    /// registers.push();
    /// registers.set(Registers::Type::Count, 0, 7, false);
    /// registers.pop();
    /// assert(registers.get(Registers::Type::Count, 0) == 42);
    /// @endcode
    class Registers {
    public:
        static constexpr std::size_t slots = 256;      ///< Slots per bank.
        static constexpr std::size_t reserved = 240;   ///< Slots from here up belong to the engine.

        /// @brief Which bank a slot lives in.
        enum class Type : std::uint8_t {
            Count,       ///< Integers.
            Dimension,   ///< Dimensions, in scaled points.
            Glue,        ///< Glue, by its natural width in scaled points.
            Width,       ///< A box register's width.
            Height,      ///< A box register's height.
            Depth,       ///< A box register's depth.
            Stretch,     ///< A glue register's stretch: scaled points, or scaled `fil`s at the order #Order says.
            Shrink,      ///< A glue register's shrink, the same way.
            Order        ///< A glue register's orders of infinity: its stretch's, and four times its shrink's.
        };

        static constexpr std::size_t banks = 9;   ///< How many kinds of register there are.

        /// @brief A bank and a slot within it.
        struct Target {
            Type type;          ///< Bank.
            std::size_t slot;   ///< Slot number.
        };

        /// @brief Opens a group.
        void push() { marks.push_back(records.size()); }

        /// @brief Closes a group, undoing its local assignments.
        /// @complexity O(k) in the distinct slots assigned inside the group.
        void pop();

        /// @brief Assigns a slot.
        /// @param type   Bank.
        /// @param slot   Slot number, below #slots.
        /// @param value  New value.
        /// @param global True to survive every enclosing group.
        /// @complexity O(1).
        void set(Type type, std::size_t slot, std::int32_t value, bool global);

        /// @brief Reads a slot.
        /// @param type Bank.
        /// @param slot Slot number, below #slots.
        /// @complexity O(1).
        [[nodiscard]] std::int32_t get(const Type type, const std::size_t slot) const noexcept {
            return values[static_cast<std::size_t>(type)][slot];
        }

        /// @brief Names a bank, or one slot within it.
        ///
        /// Without @p slot, @p symbol addresses the bank itself, as `\count`
        /// does: a number written after it, `\count5`, picks the slot.  With
        /// @p slot, @p symbol addresses that one slot directly, as
        /// `\tolerance` addresses a fixed slot of the integer bank.
        ///
        /// @param symbol Interned name.
        /// @param type   Bank it addresses.
        /// @param slot   Slot to bind to, or nothing to bind the whole bank.
        /// @complexity O(1) amortized.
        void bind(Symbol symbol, Type type, std::optional<std::size_t> slot = std::nullopt);

        /// @brief The bank a name addresses, if it names one.
        /// @param symbol Interned name.
        /// @complexity O(1).
        [[nodiscard]] std::optional<Type> bank(Symbol symbol) const noexcept;

        /// @brief The slot a name addresses, if it names one.
        /// @param symbol Interned name.
        /// @complexity O(1).
        [[nodiscard]] std::optional<Target> target(Symbol symbol) const noexcept;

        /// @brief Width of an em in scaled points: what `1em` scans as.
        [[nodiscard]] std::int32_t quad() const noexcept { return quad_; }

        /// @brief Sets the width of an em.
        /// @param value Width in scaled points.
        void quad(const std::int32_t value) noexcept { quad_ = value; }

    private:
        /// @brief One undo record.
        struct Record {
            Type type;              ///< Bank.
            std::uint8_t slot;      ///< Slot.
            std::int32_t value;     ///< Value to restore.
            std::uint32_t level;    ///< Level stamp to restore.
        };

        /// @brief What a symbol names.
        struct Name {
            Type type{Type::Count};   ///< Bank.
            std::uint8_t slot{0};     ///< Slot, for a slot name.
            std::uint8_t role{0};     ///< 0: nothing; 1: a bank; 2: a slot.
        };

        std::array<std::array<std::int32_t, slots>, banks> values{};    ///< Value of each slot.
        std::array<std::array<std::uint32_t, slots>, banks> levels{};   ///< Level stamp of each slot.
        std::vector<Record> records{};                             ///< Undo log.
        std::vector<std::size_t> marks{};                          ///< Undo-log height at each open group.
        std::vector<Name> names{};                                 ///< What each symbol names.
        std::int32_t quad_{10 * 65536};                            ///< Width of an em.
    };

}
