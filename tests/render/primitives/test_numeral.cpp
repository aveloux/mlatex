#include "render/primitives/numeral.hpp"

#include <cassert>

// Numerals: a counter's value written in digits, in Roman numerals and in
// letters, as LaTeX's \arabic, \roman and \alph write it, and in words, as
// fmtcount writes it.

int main() {
    using Numeral = render::primitives::Numeral;

    assert((Numeral::arabic(0) == "0" && Numeral::arabic(2024) == "2024" && Numeral::arabic(-3) == "-3") && "digits");
    assert((Numeral::roman(4, false) == "iv" && Numeral::roman(9, false) == "ix") && "four and nine subtract");
    assert((Numeral::roman(1994, true) == "MCMXCIV") && "and so does every place of a large one");
    assert((Numeral::roman(3999, true) == "MMMCMXCIX") && "up to the largest Roman numerals write");
    assert((Numeral::roman(0, false).empty()) && "nothing has no Roman numeral");
    assert((Numeral::alphabetic(1, false) == "a" && Numeral::alphabetic(26, true) == "Z") && "letters");
    assert((Numeral::alphabetic(0, false).empty()) && "nothing has no letter");
    assert((Numeral::alphabetic(27, false) == "z") && "past the alphabet, its last letter rather than a stop");

    assert((Numeral::words(0, false) == "zero" && Numeral::words(13, false) == "thirteen") && "the first twenty");
    assert((Numeral::words(42, false) == "forty-two" && Numeral::words(90, false) == "ninety") && "tens and units");
    assert((Numeral::words(105, false) == "one hundred and five") && "hundreds, and what is left");
    assert((Numeral::words(2026, false) == "two thousand and twenty-six") && "thousands");
    assert((Numeral::words(1'200'300, false) == "one million two hundred thousand three hundred") && "millions");
    assert((Numeral::words(-7, false) == "minus seven") && "below nothing");
    assert((Numeral::words(1, true) == "first" && Numeral::words(12, true) == "twelfth") && "irregular ordinals");
    assert((Numeral::words(20, true) == "twentieth" && Numeral::words(23, true) == "twenty-third") && "and the rest");
    assert((Numeral::words(100, true) == "one hundredth") && "a round number's ordinal");

    return 0;
}
