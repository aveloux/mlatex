#include "memory/arena.hpp"
#include "syntax/cursor.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/number.hpp"
#include "syntax/node.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/wrapper.hpp"
#include "syntax/semantics/union.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

// The Values primitive: TeX's registers -- counts, dimensions, glue and
// token lists -- set, added to, scaled and divided, named, allocated and
// printed with \the; the expressions \numexpr and \dimexpr; and the
// numbers a document writes out, \number and \romannumeral -- and, under
// all of them, numbers and dimensions as TeX reads them: every numeral a
// number may be written in, and every unit a length may be measured in,
// in scaled points, 65536 to the point.

/// What a document sets, runs of blanks folded to one, and everything it
/// reported: read by the expander and the parser with the syntax primitives
/// bound, and no page in sight.
static std::pair<std::string, std::string> expand(const std::string_view document) {
    memory::Arena arena(1u << 22);
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    syntax::primitives::Wrapper core(lexicon);
    syntax::primitives::Context context{state.registers, core.relay, core.variables};
    core(mouth, context);
    mouth.ingest(arena.copy(document));
    syntax::Parser parser(mouth, arena);

    std::string text;
    const auto get = [&text](this const auto& self, const memory::Slice<syntax::Node*> nodes) -> void {
        for (const syntax::Node* node : nodes) {
            if (node && node->type == syntax::Node::Type::Text) text += node->value;
            if (node && node->type == syntax::Node::Type::Group) self(node->nodes);
        }
    };
    get(parser.parse(0));

    std::string folded;
    for (const char letter : text) {
        if (letter != ' ' && letter != '\n' && letter != '\t' && letter != '\r') {
            folded += letter;
        } else if (!folded.empty() && folded.back() != ' ') {
            folded += ' ';
        }
    }
    if (!folded.empty() && folded.back() == ' ') folded.pop_back();

    std::string errors;
    for (const auto& list : {parser.traceback(), mouth.traceback(), core.traceback()}) {
        for (const syntax::Traceback& fault : list) errors += fault.format() + '\n';
    }
    return {folded, errors};
}

/// A document must set exactly this text, and report nothing.
static void sets(const std::string_view document, const std::string_view expected, const char* what) {
    const auto [text, errors] = expand(document);
    if (text != expected || !errors.empty()) {
        std::fprintf(stderr, "%s\n  document: %.*s\n  expected: [%.*s]\n  got:      [%s]\n%s", what,
                     static_cast<int>(document.size()), document.data(), static_cast<int>(expected.size()),
                     expected.data(), text.c_str(), errors.c_str());
    }
    assert(text == expected && errors.empty());
}

/// A document must report an error whose text holds this.
static void reports(const std::string_view document, const std::string_view fragment, const char* what) {
    const auto [text, errors] = expand(document);
    if (errors.find(fragment) == std::string::npos || errors.empty()) {
        std::fprintf(stderr, "%s\n  document: %.*s\n  wanted an error holding: %.*s\n%s", what,
                     static_cast<int>(document.size()), document.data(), static_cast<int>(fragment.size()),
                     fragment.data(), errors.c_str());
    }
    assert(!errors.empty() && errors.find(fragment) != std::string::npos);
}

/// @brief Scans one dimension from a text.
static std::optional<std::int32_t> dimension(const std::string_view text, const std::int32_t quad = 12 * 65536) {
    memory::Arena arena;
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    state.registers.quad = quad;
    mouth.ingest(arena.copy(text));
    return syntax::Number::dimension(mouth, state.registers);
}

/// @brief Scans one integer from a text.
static std::optional<std::int32_t> integer(const std::string_view text) {
    memory::Arena arena;
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    mouth.ingest(arena.copy(text));
    return syntax::Number::integer(mouth, state.registers);
}

/// @brief Within a scaled point or two of what was expected: TeX itself
///        rounds a fractional unit on the way in.
static bool near(const std::optional<std::int32_t> value, const std::int64_t expected) {
    return value && std::llabs(static_cast<std::int64_t>(*value) - expected) <= 2;
}

int main() {
    // --- Setting and arithmetic -------------------------------------------------
    sets("\\set\\integer0 = 42 \\evaluate{\\integer0}", "42", "\\set an integer, read it back");
    sets("\\set\\integer0 = 10 \\increase\\integer0 by 5 \\evaluate{\\integer0}", "15", "\\increase");
    sets("\\set\\integer0 = 10 \\reduce\\integer0 by 3 \\evaluate{\\integer0}", "3", "\\reduce divides, truncating");
    sets("\\set\\integer0 = 10 \\scale\\integer0 by 3 \\evaluate{\\integer0}", "30", "\\scale");
    sets("\\set\\integer1 = 7 \\name\\tally\\integer1 \\set\\tally = 9 \\evaluate{\\integer1}", "9",
         "\\name gives a register a name");
    sets("\\group\\set\\integer0 = 5 \\ungroup\\evaluate{\\integer0}", "0", "an assignment ends with its group");
    sets("\\group\\shared\\set\\integer0 = 5 \\ungroup\\evaluate{\\integer0}", "5", "a shared one outlives it");
    sets("\\set\\integer0 = 123456 \\evaluate{\\integer0}", "123456", "a many-digit number reads whole");

    // --- TeX's own names --------------------------------------------------------
    sets("\\include{core/aliases}\\newcount\\tally \\tally=3 \\advance\\tally by 2 \\the\\tally", "5",
         "\\newcount, \\advance and \\the");
    sets("\\include{core/aliases}\\newdimen\\gap \\gap=2pt \\multiply\\gap by 3 \\the\\gap", "6.0pt",
         "a dimension printed as TeX prints one");
    sets("\\include{core/aliases}\\newskip\\gap \\gap=4pt plus 1fil \\the\\gap", "4.0pt plus 1.0fil",
         "glue printed with its stretch");
    sets("\\newtoks\\stash \\stash={saved tokens}\\the\\stash", "saved tokens", "a token list");
    sets("\\chardef\\letter=65 \\number\\letter", "65", "\\chardef gives a number a name");
    sets("\\countdef\\first=5 \\first=7 \\the\\first", "7", "\\countdef names a count register");

    // --- Expressions ------------------------------------------------------------
    sets("\\number\\numexpr 3*(4+5)/2\\relax", "14", "\\numexpr rounds a division, as e-TeX's does");
    sets("\\the\\numexpr 7/2\\relax", "4", "half rounds away from zero");
    sets("\\the\\dimexpr 1pt*3+2pt\\relax", "5.0pt", "\\dimexpr");
    sets("\\the\\dimexpr (1pt+1pt)*2\\relax", "4.0pt", "\\dimexpr with brackets");

    // --- Numbers written out ------------------------------------------------------
    sets("\\romannumeral 2024", "mmxxiv", "\\romannumeral");
    sets("\\number 007", "7", "\\number drops leading zeros");
    reports("\\set\\integer0 = x", "\\set needs a value", "a register set to nothing is reported");


    // --- Numbers and dimensions, as the values above are read ----------------
    constexpr std::int64_t point = 65536;

    // --- Integers -------------------------------------------------------------
    assert((integer("42") == 42) && "a decimal number");
    assert((integer("123456") == 123456) && "every digit of a long one");
    assert((integer("-17") == -17) && "a sign");
    assert((integer("--5") == 5) && "two signs cancel");
    assert((integer("\"FF") == 255) && "hexadecimal");
    assert((integer("'17") == 15) && "octal");
    assert((!integer("x").has_value()) && "no numeral is no number");

    // --- Dimensions ---------------------------------------------------------------
    assert((dimension("1pt") == point) && "a point");
    assert((dimension("1.5pt") == point * 3 / 2) && "a fraction of one");
    assert((dimension("-3pt") == -3 * point) && "a negative length");
    assert((dimension("1sp") == 1) && "a scaled point");
    assert((dimension("1pc") == 12 * point) && "a pica is twelve points");
    assert((near(dimension("1in"), point * 7227 / 100)) && "an inch is 72.27 points");
    assert((near(dimension("72bp"), point * 7227 / 100)) && "and 72 big points");
    assert((near(dimension("2.54cm"), point * 7227 / 100)) && "and 2.54 centimetres");
    assert((near(dimension("10mm"), *dimension("1cm"))) && "ten millimetres make a centimetre");
    assert((near(dimension("1157dd"), 1238 * point)) && "didot points");
    assert((near(dimension("1cc"), *dimension("12dd"))) && "a cicero is twelve didots");
    assert((dimension("1em") == 12 * point) && "an em is the face's own");
    assert((dimension("2em", 10 * point) == 20 * point) && "whatever the face's size");
    assert((near(dimension("1ex"), 12 * point * 43 / 100)) && "an ex is 0.43 of it");
    assert((dimension("3 pt") == 3 * point) && "a space before the unit is allowed");
    assert((dimension("1 true pt") == point) && "and so is TeX's `true`");
    assert((!dimension("pt").has_value()) && "a unit with no numeral is no dimension");

    sets("\\catcode`\\!=13 \\define!{BANG}!x\\catcode`\\!=12 !", "BANGx!",
         "a character made active is read so from where \\catcode says, and ordinary again after");
    sets("{\\catcode`\\|=0 |define|a{A}|a}|", "{A}|", "a new escape character, as far as its group reaches");
    sets("\\catcode`\\_=11 \\define\\my_name{N}\\my_name", "N", "a letter now carries the control word on");
    sets("\\catcode95=11 \\catcode58=11 \\define\\tl_set:Nn{T}\\tl_set:Nn", "T",
         "and carries on a word an earlier change already made, as expl3's names need");
    sets("\\count255=7 \\the\\count255", "7", "the last of the 256 registers");

    return 0;
}
