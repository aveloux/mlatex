#include "memory/arena.hpp"
#include "syntax/cursor.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/wrapper.hpp"
#include "syntax/semantics/union.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

// The Strings primitive: xstring's tests and functions over the expanded
// text, a character a UTF-8 sequence, a result set where it stands or kept
// in a macro.

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

/// A document must report an error naming this.
static void reports(const std::string_view document, const std::string_view needle, const char* what) {
    const auto [text, errors] = expand(document);
    if (errors.find(needle) == std::string::npos) {
        std::fprintf(stderr, "%s\n  document: %.*s\n  expected an error naming: %.*s\n  got: %s\n", what,
                     static_cast<int>(document.size()), document.data(), static_cast<int>(needle.size()),
                     needle.data(), errors.c_str());
    }
    assert(errors.find(needle) != std::string::npos);
}

int main() {
    // --- The tests ------------------------------------------------------------------------
    sets("\\IfSubStr{banana}{nan}{yes}{no}", "yes", "a piece inside");
    sets("\\IfSubStr{banana}{xyz}{yes}{no}", "no", "a piece not inside");
    sets("\\IfSubStr[3]{banana}{a}{yes}{no} \\IfSubStr[4]{banana}{a}{yes}{no}", "yes no",
         "a piece standing so many times");
    sets("\\IfSubStr*{banana}{ban}{yes}{no}", "yes", "the starred test");
    sets("\\IfBeginWith{report.pdf}{report}{a}{b} \\IfEndWith{report.pdf}{.pdf}{c}{d}", "a c", "the ends of text");
    sets("\\IfStrEq{a}{a}{same}{different} \\IfStrEq{a}{b}{same}{different}", "same different", "text compared");
    sets("\\IfEq{1.0}{1}{equal}{unequal} \\IfEq{a}{a}{equal}{unequal}", "equal equal",
         "numbers compared as numbers, the rest as text");
    sets("\\IfInteger{-42}{whole}{not} \\IfInteger{4.2}{whole}{not}", "whole not", "whole numbers");
    sets("\\IfDecimal{3,5}{number}{not} \\IfDecimal{3.5.1}{number}{not} \\IfDecimal{x}{number}{not}",
         "number not not", "decimal numbers");
    sets("\\define\\plan{gold}\\IfSubStr{\\plan}{old}{expanded}{as written}", "expanded",
         "arguments expanded first");

    // --- The functions --------------------------------------------------------------------
    sets("\\StrLen{hello} \\StrLen{café} \\StrLen{}", "5 4 0", "lengths in characters");
    sets("\\StrLeft{hello}{2} \\StrRight{hello}{2} \\StrLeft{hi}{9}", "he lo hi", "characters off either end");
    sets("\\StrGobbleLeft{hello}{1} \\StrGobbleRight{hello}{2}", "ello hel", "characters let go");
    sets("\\StrMid{hello}{2}{4} \\StrChar{hello}{1} \\StrChar{héllo}{2}", "ell h é", "characters from the middle");
    sets("\\StrBefore{2026-09-29}{-} \\StrBehind{2026-09-29}{-} \\StrBehind[2]{2026-09-29}{-}",
         "2026 09-29 29", "before and behind a piece");
    sets("\\StrBefore{abc}{x}.", ".", "a piece that is not there leaves nothing");
    sets("\\StrBetween{<a><b>}{<}{>} \\StrBetween[2,1]{<a><b>}{<}{>}", "a b", "between two pieces");
    sets("\\StrSubstitute{a.b.c}{.}{,} \\StrSubstitute[1]{a.b.c}{.}{,}", "a,b,c a,b.c", "pieces replaced");
    sets("\\StrDel{a-b-c}{-} \\StrCount{banana}{a} \\StrCount{banana}{x}", "abc 3 0", "pieces deleted and counted");
    sets("\\StrPosition{banana}{n} \\StrPosition[2]{banana}{n} \\StrPosition{banana}{x}", "3 5 0",
         "where a piece stands");
    sets("\\StrLeft{hello}{2}[\\start]kept: \\start", "kept: he", "a result kept in a macro");
    reports("\\StrLeft{hello}{two}", "needs a whole number", "a count that is no number");
    return 0;
}
