#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace syntax {

    /// @brief Character category table, with group-scoped assignment.
    ///
    /// Maps each of the 256 byte values to the role it plays for the lexer.
    /// `\\catcode` changes made inside a group are undone when the group
    /// closes; changes made with `\\global` are not.
    ///
    /// @par Scoping
    /// Every character carries a *level stamp*: the group depth at which its
    /// current category was established, where 0 means the outermost level.
    /// A local assignment saves the old category only when the stamp differs
    /// from the current depth, so repeated assignment inside one group costs
    /// one undo record, not one per assignment. A `\\global` assignment writes
    /// the table and sets the stamp to 0; pop() then discards any outstanding
    /// record for a character whose stamp is 0, because that character has
    /// been globalized since the record was taken. Both operations are O(1) --
    /// nothing walks the undo log looking for the slot. This is TeX's own
    /// `eq_level` scheme.
    class CatCodes {
    public:
        /// @brief What a character means to the lexer.
        enum class Category : std::uint8_t {
            Escape,    ///< Command prefix (default: \\)
            Group,     ///< Scope delimiters (default: { and })
            Shift,     ///< Math mode toggle (default: $)
            Align,     ///< Alignment separator (default: &)
            Parameter, ///< Macro argument marker (default: #)
            Mark,      ///< Superscript marker (default: ^)
            Index,     ///< Subscript marker (default: _)
            Letter,    ///< Alphabetic identifier characters (a-z, A-Z)
            Other,     ///< Numbers, punctuation, and unmapped text
            Space,     ///< Blank space, tab, or newline
            Comment,   ///< Rest-of-line comment (default: %)
            Active,    ///< Character acting directly as a command (default: ~)
            Ignore,    ///< Character completely skipped by the lexer
            Invalid    ///< Unprintable ASCII or illegal byte
        };

        /// @brief One entry on the undo log.
        struct Entry {
            std::size_t symbol;     ///< Byte value whose category was replaced.
            Category category;      ///< Category to put back.
            std::uint32_t level;    ///< Level stamp to put back with it.
        };

        /// @brief Builds the plain TeX table.
        /// @complexity O(1): a fixed 256-entry fill.
        constexpr CatCodes() noexcept {
            this->table.fill(Category::Other);

            for (std::size_t code = 0; code < 0x20uz; ++code) {
                this->table[code] = Category::Invalid;
            }
            this->table[0x7Fuz] = Category::Invalid; // DEL
            this->table[0x00uz] = Category::Ignore;  // NUL is skipped, as in TeX

            for (int code = 'a'; code <= 'z'; ++code) {
                this->table[static_cast<std::size_t>(code)] = Category::Letter;
            }
            for (int code = 'A'; code <= 'Z'; ++code) {
                this->table[static_cast<std::size_t>(code)] = Category::Letter;
            }

            this->table[static_cast<std::size_t>('\\')] = Category::Escape;
            this->table[static_cast<std::size_t>('{')]  = Category::Group;
            this->table[static_cast<std::size_t>('}')]  = Category::Group;
            this->table[static_cast<std::size_t>('$')]  = Category::Shift;
            this->table[static_cast<std::size_t>('&')]  = Category::Align;
            this->table[static_cast<std::size_t>('#')]  = Category::Parameter;
            this->table[static_cast<std::size_t>('^')]  = Category::Mark;
            this->table[static_cast<std::size_t>('_')]  = Category::Index;
            this->table[static_cast<std::size_t>('%')]  = Category::Comment;
            this->table[static_cast<std::size_t>(' ')]  = Category::Space;
            this->table[static_cast<std::size_t>('\t')] = Category::Space;
            this->table[static_cast<std::size_t>('\n')] = Category::Space;
            this->table[static_cast<std::size_t>('\f')] = Category::Space;
            this->table[static_cast<std::size_t>('\r')] = Category::Space;
            this->table[static_cast<std::size_t>('~')]  = Category::Active;
        }

        /// @brief Assigns a character's category.
        ///
        /// @param symbol   Character to reassign.
        /// @param category New category.
        /// @param global   True for `\\global`: survives every enclosing group.
        /// @complexity O(1), including the global case. See the class note.
        void set(const char symbol, const Category category, const bool global = false) {
            const auto code = static_cast<std::size_t>(static_cast<unsigned char>(symbol));
            if (code >= this->table.size()) return;

            if (global) {
                this->levels[code] = 0;
            } else if (const auto depth = static_cast<std::uint32_t>(this->marks.size());
                       depth != 0 && this->levels[code] != depth) {
                this->history.push_back(Entry{code, this->table[code], this->levels[code]});
                this->levels[code] = depth;
            }

            this->table[code] = category;
        }

        /// @brief Reads a character's category.
        /// @param symbol Character to look up.
        /// @return Its current category.
        /// @complexity O(1).
        [[nodiscard]] constexpr Category get(const char symbol) const noexcept {
            return this->table[static_cast<std::size_t>(static_cast<unsigned char>(symbol))];
        }

        /// @brief Opens a group.
        /// @complexity O(1) amortized.
        void push() {
            this->marks.push_back(this->history.size());
        }

        /// @brief Closes a group, undoing its local assignments.
        ///
        /// Records for characters that have since been globalized are
        /// discarded rather than applied.
        ///
        /// @complexity O(k) in the number of *distinct* characters assigned
        ///             inside this group, not the number of assignments.
        void pop() {
            if (this->marks.empty()) return;

            const std::size_t mark = this->marks.back();
            this->marks.pop_back();

            for (std::size_t index = this->history.size(); index > mark; --index) {
                const auto [symbol, category, level] = this->history[index - 1];
                if (this->levels[symbol] == 0) continue;   // globalized since
                this->levels[symbol] = level;
                this->table[symbol] = category;
            }
            this->history.resize(mark);
        }

        /// @brief How many groups are open.
        /// @complexity O(1).
        [[nodiscard]] std::size_t depth() const noexcept { return this->marks.size(); }

        /// @brief How many undo records are outstanding.
        /// @return Size of the undo log; a diagnostic, used by the test suite
        ///         to show that repeated assignment does not grow it.
        /// @complexity O(1).
        [[nodiscard]] std::size_t saved() const noexcept { return this->history.size(); }

    private:
        std::array<Category, 256> table{};
        std::array<std::uint32_t, 256> levels{};   ///< Group depth of each current value; 0 is outermost.
        std::vector<Entry> history{};              ///< Undo log, innermost last.
        std::vector<std::size_t> marks{};          ///< Undo-log height at each open group.
    };

}