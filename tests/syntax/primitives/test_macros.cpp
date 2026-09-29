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

// The Macros primitive: \define and what TeX builds on it -- parameters,
// delimited and optional; \alias and \forget; definitions scoped to their
// group or \shared past it -- and the low-level expansion controls a
// package leans on: \expandafter, \noexpand, \csname, \string, \meaning,
// \futurelet, \afterassignment, \aftergroup, \uppercase and \scantokens.

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
    const auto gather = [&text](this const auto& self, const memory::Slice<syntax::Node*> nodes) -> void {
        for (const syntax::Node* node : nodes) {
            if (node && node->type == syntax::Node::Type::Text) text += node->value;
            if (node && node->type == syntax::Node::Type::Group) self(node->nodes);
        }
    };
    gather(parser.parse(0));

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

int main() {
    // --- Definitions --------------------------------------------------------
    sets("\\define\\greeting{hello}\\greeting", "hello", "a macro with no parameters");
    sets("\\define\\twice[1]{#1#1}\\twice{ab}", "abab", "a parameter used twice");
    sets("\\define\\pair[2]{#1 and #2}\\pair{a}{b}", "a and b", "two parameters");
    sets("\\define{\\greet}[2][Hello]{#1, #2!}\\greet{world}", "Hello, world!",
         "an optional first parameter, left out");
    sets("\\define{\\greet}[2][Hello]{#1, #2!}\\greet[Goodbye]{world}", "Goodbye, world!",
         "an optional first parameter, given");
    sets("\\define\\pair#1,#2;{#1 then #2}\\pair x,y;", "x then y", "delimited parameters");
    sets("\\define\\outer[1]{\\define\\inner[1]{#1-##1}}\\outer{a}\\inner{b}", "a-b",
         "## stands for # in a definition made by a definition");
    reports("\\define 5{x}", "\\define", "\\define needs a control sequence");
    reports("\\define\\loop{\\loop}\\loop", "", "a macro that calls itself forever is stopped");

    // --- Aliases, forgetting and scope ----------------------------------------
    sets("\\define\\a{one}\\alias\\b\\a\\define\\a{two}\\b\\a", "onetwo", "an alias keeps the meaning it was given");
    reports("\\define\\a{x}\\forget\\a\\a", "\\a", "a forgotten macro is undefined again");
    sets("\\group\\define\\local{in}\\ungroup\\ifdefined\\local yes\\else no\\fi", "no",
         "a definition ends with its group");
    sets("\\group\\shared\\define\\kept{in}\\ungroup\\kept", "in", "\\shared outlives the group");
    sets("\\include{core/aliases}\\def\\a{x}\\newcommand{\\b}{y}\\a\\b", "xy", "LaTeX's and TeX's names for \\define");

    // --- Expansion controls ---------------------------------------------------
    sets("\\include{core/aliases}\\def\\a{A}\\edef\\b{\\a\\noexpand\\a}\\def\\a{Z}\\b", "AZ",
         "\\noexpand keeps a name from expanding inside \\edef");
    sets("\\include{core/aliases}\\expandafter\\def\\csname built\\endcsname{made}\\built", "made",
         "\\csname builds a name, \\expandafter reaches past \\def to it");
    sets("\\string\\relax", "\\relax", "\\string prints a name");
    sets("\\detokenize{a \\b}", "a \\b", "\\detokenize prints what it holds");
    sets("\\define\\a{xy}\\meaning\\a", "macro:->xy", "\\meaning of a macro");
    sets("\\include{core/aliases}\\def\\y{\\ifx\\next[bracket\\else plain\\fi}\\def\\x{\\futurelet\\next\\y}\\x[ \\x.",
         "bracket[ plain.", "\\futurelet looks at the token ahead and leaves it there");
    sets("\\uppercase{up} \\lowercase{DOWN}", "UP down", "\\uppercase and \\lowercase");
    sets("\\define\\after{!}\\afterassignment\\after\\set\\integer0 = 1 done", "!done",
         "\\afterassignment runs straight after the next assignment");
    sets("\\define\\after{!}\\group\\aftergroup\\after in\\ungroup out", "in!out",
         "\\aftergroup runs straight after the group closes");
    sets("\\scantokens{\\define\\a{scanned}}\\a", "scanned", "\\scantokens reads text as a document would");
    sets("\\include{core/aliases}\\protected\\def\\a{x}\\edef\\b{\\a}\\b", "x",
         "\\protected keeps a macro whole in \\edef");

    sets("\\@define\\a{\\@ifnextchar\\bgroup{group}{other}}\\a{x} \\a x", "group{x} otherx",
         "\\@ifnextchar\\bgroup asks after a brace group");

    sets("\\protected\\define\\p{P}\\expanded\\define\\e{\\p}\\meaning\\e", "macro:->\\p",
         "\\protected keeps a macro as written through \\edef");

    return 0;
}
