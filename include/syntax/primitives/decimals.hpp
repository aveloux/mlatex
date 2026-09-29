#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace syntax::primitives {

    class Variables;

    /// @brief Money-safe arithmetic and the formatting of amounts:
    ///        `\\calculate`, `\\amount`, `\\separators`.
    ///
    /// `\\evaluate` counts in whole numbers, which is what a register holds and
    /// what a counter wants. An invoice wants 3 × 12.50 to be 37.50 exactly,
    /// and a total of a hundred lines to come to the cent a calculator would
    /// give -- which binary floating point does not promise: it holds 2.675 as
    /// 2.67499999…, and rounds it the wrong way. So these are decimals held
    /// as a whole number of millionths, added and multiplied exactly, and
    /// rounded half away from zero -- commercial rounding -- only where a
    /// division or a multiplication has more digits than a millionth can keep.
    ///
    /// @par Language
    /// @code
    /// \calculate{3 * 12.50}                   % 37.5
    /// \calculate{(100 - 15) / 3}              % 28.333333
    /// \calculate{subtotal * 7.5 / 100}        % a name is a variable's value
    ///
    /// \amount{1234.5}                         % 1,234.50
    /// \amount[0]{1234.5}                      % 1,235
    /// \amount{\variable{total}}               % anything that expands to a number
    ///
    /// \separators{.}{,}                       % 1.234,50 from here on
    /// @endcode
    ///
    /// Both expand their argument all the way first, so a macro or a
    /// `\\variable` inside one has already become digits by the time it is
    /// calculated. A bare name -- letters, digits, `_` and `.`, starting
    /// with a letter -- is the value of the variable it names, read as an
    /// expression of its own. `+`, `-`, `*`, `/` and parentheses do what they
    /// do on paper, `*` and `/` before `+` and `-`.
    ///
    /// @par Range
    /// Values are held to six decimal places, between about minus and plus
    /// nine trillion. A result outside that, a division by zero, a name no
    /// variable answers to and text that is not an expression are all
    /// reported, and the calculation yields 0 rather than a wrong figure
    /// that looks right.
    class Decimals {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Decimals(Lexicon& lexicon) noexcept;

        /// @brief Installs `\\calculate`, `\\amount` and `\\separators`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services; names resolve against its variables.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Evaluates an expression.
        /// @param text      Text to evaluate, already expanded.
        /// @param variables What a bare name resolves against.
        /// @param error     Set to what went wrong, when anything did.
        /// @return The value in millionths, or nullopt on any error.
        /// @complexity O(n) in the expression's length, plus each variable read.
        [[nodiscard]] static std::optional<std::int64_t> evaluate(
            std::string_view text, const Variables& variables, std::string& error);

        /// @brief Writes a value exactly, with no trailing zeros: `37.5`, `-3`.
        /// @param value Value in millionths.
        /// @return Its text.
        /// @complexity O(1).
        [[nodiscard]] static std::string write(std::int64_t value);

        static constexpr std::int64_t scale = 1'000'000;   ///< Millionths in one.
        static constexpr int precision = 6;                ///< Decimal places a value keeps.

        /// @brief Errors this module has recorded.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

    private:
        /// @brief State threaded through one evaluation.
        struct Pass {
            std::string_view text;          ///< What is being read.
            std::size_t cursor = 0;         ///< How far it has been read.
            const Variables& variables;     ///< What a bare name resolves against.
            std::string& error;             ///< Set on the first thing that goes wrong.
            std::size_t level = 0;          ///< Nesting, through parentheses and variables.
        };

        [[nodiscard]] static std::optional<std::int64_t> expression(Pass& pass);   ///< Sums and differences.
        [[nodiscard]] static std::optional<std::int64_t> term(Pass& pass);         ///< Products and quotients.
        [[nodiscard]] static std::optional<std::int64_t> unary(Pass& pass);        ///< A signed operand.
        [[nodiscard]] static std::optional<std::int64_t> primary(Pass& pass);      ///< A number, a name, a group.

        /// @brief A hundred-and-twenty-eight-bit unsigned number, as two words.
        ///
        /// A product of two values in millionths is in millionths squared,
        /// and for the figures an invoice deals in that no longer fits
        /// sixty-four bits. The one step that needs more -- a product before
        /// it is scaled back down, a dividend before it is divided -- is done
        /// in a pair of words, by hand, the same way on every compiler. It
        /// runs a handful of times per document, where portability is worth
        /// more than the cycles an intrinsic would save.
        struct Wide {
            std::uint64_t high{0};   ///< The upper sixty-four bits.
            std::uint64_t low{0};    ///< The lower sixty-four bits.
        };

        /// @brief Multiplies two words into a wide number, exactly.
        /// @param left  One factor.
        /// @param right The other.
        /// @complexity O(1).
        [[nodiscard]] static Wide multiply(std::uint64_t left, std::uint64_t right) noexcept;

        /// @brief Divides a wide number by a word, rounding half away from zero.
        /// @param dividend What is divided.
        /// @param divisor  What it is divided by; not zero.
        /// @param quotient Set to the rounded quotient.
        /// @return False when the quotient does not fit a word.
        /// @complexity O(1): one pass over the dividend's 128 bits.
        [[nodiscard]] static bool divide(Wide dividend, std::uint64_t divisor, std::uint64_t& quotient) noexcept;

        /// @brief The magnitude of a signed value, as a word.
        /// @param value Value to take it of; the most negative one included.
        [[nodiscard]] static constexpr std::uint64_t magnitude(const std::int64_t value) noexcept {
            return value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                             : static_cast<std::uint64_t>(value);
        }

        /// @brief Gives a magnitude its sign back, when it still fits.
        /// @param value    The magnitude.
        /// @param negative Whether the result is below zero.
        /// @param result   Set to the signed value.
        /// @return False when it does not fit.
        [[nodiscard]] static bool sign(std::uint64_t value, bool negative, std::int64_t& result) noexcept;

        /// @brief Is this character part of a name in an expression?
        /// @param letter Character to test.
        /// @param first  True for the first character, which may not be a
        ///               digit or a point.
        [[nodiscard]] static constexpr bool naming(const char letter, const bool first) noexcept {
            const bool alphabetic = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
                                    letter == '_';
            if (first) return alphabetic;
            return alphabetic || (letter >= '0' && letter <= '9') || letter == '.';
        }

        /// Deepest nesting of parentheses and variables read inside variables,
        /// so a variable defined in terms of itself is reported, not followed.
        static constexpr std::size_t depth = 64;

        mutable std::string grouping{","};   ///< Between each three digits of the whole part.
        mutable std::string point{"."};      ///< Between the whole part and the fraction.
        mutable std::vector<Traceback> tracebacks{};   ///< Errors this module found.
    };

}
