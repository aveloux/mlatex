#include "render/primitives/numeral.hpp"

#include <cassert>

// Numerals: a counter's value written in digits, in Roman numerals and in
// letters, as LaTeX's \arabic, \roman and \alph write it.

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

    return 0;
}
