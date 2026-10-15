#include "render/primitives/referencing/numerals.hpp"

#include <cassert>

// Numerals: a counter's value written in digits, in Roman numerals and in
// letters, as LaTeX's \arabic, \roman and \alph write it, and in words, as
// fmtcount writes it.

int main() {
    using Numerals = render::primitives::referencing::Numerals;

    assert((Numerals::arabic(0) == "0" && Numerals::arabic(2024) == "2024" && Numerals::arabic(-3) == "-3") &&
           "digits");
    assert((Numerals::roman(4, false) == "iv" && Numerals::roman(9, false) == "ix") && "four and nine subtract");
    assert((Numerals::roman(1994, true) == "MCMXCIV") && "and so does every place of a large one");
    assert((Numerals::roman(3999, true) == "MMMCMXCIX") && "up to the largest Roman numerals write");
    assert((Numerals::roman(0, false).empty()) && "nothing has no Roman numeral");
    assert((Numerals::alphabetic(1, false) == "a" && Numerals::alphabetic(26, true) == "Z") && "letters");
    assert((Numerals::alphabetic(0, false).empty()) && "nothing has no letter");
    assert((Numerals::alphabetic(27, false) == "z") && "past the alphabet, its last letter rather than a stop");

    assert((Numerals::words(0, false) == "zero" && Numerals::words(13, false) == "thirteen") && "the first twenty");
    assert((Numerals::words(42, false) == "forty-two" && Numerals::words(90, false) == "ninety") && "tens and units");
    assert((Numerals::words(105, false) == "one hundred and five") && "hundreds, and what is left");
    assert((Numerals::words(2026, false) == "two thousand and twenty-six") && "thousands");
    assert((Numerals::words(1'200'300, false) == "one million two hundred thousand three hundred") && "millions");
    assert((Numerals::words(-7, false) == "minus seven") && "below nothing");
    assert((Numerals::words(1, true) == "first" && Numerals::words(12, true) == "twelfth") && "irregular ordinals");
    assert((Numerals::words(20, true) == "twentieth" && Numerals::words(23, true) == "twenty-third") && "and the rest");
    assert((Numerals::words(100, true) == "one hundredth") && "a round number's ordinal");

    return 0;
}
