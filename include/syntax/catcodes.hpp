#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace syntax {

    /// @brief Character category table with group-scoped assignment.
    ///
    /// Maps each byte to the role it plays for the lexer. An assignment made
    /// inside a group is undone when the group closes; a global one is not.
    ///
    /// @par Scoping
    /// Each byte carries a level stamp: the group depth at which its current
    /// category was set, 0 being the outermost level. A local assignment
    /// records the old category only when the stamp differs from the current
    /// depth, so repeated assignment inside one group costs one record. A
    /// global assignment sets the stamp to 0, and pop() discards records for
    /// bytes stamped 0 since. This is TeX's `eq_level` scheme; every operation
    /// is O(1).
    class CatCodes {
    public:
        /// @brief What a character means to the lexer.
        enum class Category : std::uint8_t {
            Escape,      ///< Starts a control sequence (`\`).
            Group,       ///< Opens or closes a group (`{` and `}`).
            Shift,       ///< Enters or leaves a formula (`$`).
            Align,       ///< Separates alignment cells (`&`).
            Parameter,   ///< Marks a macro parameter (`#`).
            Mark,        ///< Superscript (`^`).
            Index,       ///< Subscript (`_`).
            Letter,      ///< Forms control words (`a`-`z`, `A`-`Z`).
            Other,       ///< Digits, punctuation and every other printable byte.
            Space,       ///< Blank space and line endings.
            Comment,     ///< Discards the rest of the line (`%`).
            Active,      ///< Acts as a control sequence on its own (`~`).
            Ignore,      ///< Skipped by the lexer (NUL).
            Invalid      ///< Rejected by the lexer (control bytes and DEL).
        };

        /// @brief Builds plain TeX's table.
        constexpr CatCodes() noexcept {
            table.fill(Category::Other);
            for (std::size_t code = 0; code < 0x20; ++code) table[code] = Category::Invalid;
            for (std::size_t code = 'a'; code <= 'z'; ++code) table[code] = Category::Letter;
            for (std::size_t code = 'A'; code <= 'Z'; ++code) table[code] = Category::Letter;
            // LaTeX's internals are named with `@` in them, and a package here
            // is written the same way; a document's own `@` prints as it did.
            table['@'] = Category::Letter;
            table[0x00] = Category::Ignore;
            table[0x7F] = Category::Invalid;
            table['\\'] = Category::Escape;
            table['{'] = Category::Group;
            table['}'] = Category::Group;
            table['$'] = Category::Shift;
            table['&'] = Category::Align;
            table['#'] = Category::Parameter;
            table['^'] = Category::Mark;
            table['_'] = Category::Index;
            table['%'] = Category::Comment;
            table['~'] = Category::Active;
            for (const char blank : {' ', '\t', '\n', '\r', '\f'}) table[static_cast<unsigned char>(blank)] = Category::Space;
        }

        /// @brief Assigns a character's category.
        /// @param symbol   Character to reassign.
        /// @param category New category.
        /// @param global   True to survive every enclosing group.
        /// @complexity O(1).
        void set(const char symbol, const Category category, const bool global = false) {
            const auto code = static_cast<unsigned char>(symbol);
            if (global) {
                levels[code] = 0;
            } else if (const auto depth = static_cast<std::uint32_t>(marks.size()); depth != 0 && levels[code] != depth) {
                history.push_back(Entry{code, table[code], levels[code]});
                levels[code] = depth;
            }
            table[code] = category;
        }

        /// @brief Reads a character's category.
        /// @param symbol Character to look up.
        /// @complexity O(1).
        [[nodiscard]] constexpr Category get(const char symbol) const noexcept {
            return table[static_cast<unsigned char>(symbol)];
        }

        /// @brief Opens a group.
        void push() { marks.push_back(history.size()); }

        /// @brief Closes a group, undoing its local assignments.
        /// @complexity O(k) in the distinct characters assigned inside the group.
        void pop() {
            if (marks.empty()) return;
            for (std::size_t index = history.size(); index > marks.back(); --index) {
                const Entry& entry = history[index - 1];
                if (levels[entry.code] == 0) continue;
                levels[entry.code] = entry.level;
                table[entry.code] = entry.category;
            }
            history.resize(marks.back());
            marks.pop_back();
        }

    private:
        /// @brief One undo record.
        struct Entry {
            unsigned char code;      ///< Character whose category was replaced.
            Category category;       ///< Category to restore.
            std::uint32_t level;     ///< Level stamp to restore.
        };

        std::array<Category, 256> table{};         ///< Category of each byte.
        std::array<std::uint32_t, 256> levels{};   ///< Level stamp of each byte.
        std::vector<Entry> history{};              ///< Undo log, innermost last.
        std::vector<std::size_t> marks{};          ///< Undo-log height at each open group.
    };

}
