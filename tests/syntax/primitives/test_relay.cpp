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

// The Relay primitive: the \if family. Each chooses one branch and skips
// the other, nested conditionals are skipped whole, and a test that takes
// its branches as arguments is not mistaken for one that ends with \fi.

/// What a document sets, runs of blanks folded to one, and everything it
/// reported: read by the expander and the parser with the syntax primitives
/// bound, and no page in sight.
static std::pair<std::string, std::string> expand(const std::string_view document) {
    memory::Arena arena(1u << 22);
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    syntax::primitives::Wrapper core(lexicon);
    syntax::primitives::Context context{state.registers(), core.conditionals(), core.variables()};
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
    for (const auto& list : {parser.tracebacks(), mouth.tracebacks(), core.tracebacks()}) {
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

int main() {
    sets("\\iftrue yes\\else no\\fi", "yes", "\\iftrue");
    sets("\\iffalse yes\\else no\\fi", "no", "\\iffalse");
    sets("\\set\\integer0 = 12 \\ifnum\\integer0 > 10 big\\else small\\fi", "big", "\\ifnum");
    sets("\\set\\length1 = 1pt \\ifdim\\length1 < 2pt narrow\\else wide\\fi", "narrow", "\\ifdim");
    sets("\\set\\integer0 = 2 \\ifcase\\integer0 zero\\or one\\or two\\else many\\fi", "two", "\\ifcase");
    sets("\\set\\integer0 = 7 \\ifcase\\integer0 zero\\or one\\else many\\fi", "many", "\\ifcase falls to \\else");
    sets("\\ifodd 3 odd\\else even\\fi", "odd", "\\ifodd");
    sets("\\define\\a{x}\\define\\b{x}\\ifx\\a\\b same\\else different\\fi", "same", "\\ifx on equal macros");
    sets("\\define\\a{x}\\define\\b{y}\\ifx\\a\\b same\\else different\\fi", "different", "\\ifx on different ones");
    sets("\\ifx aa same\\else different\\fi", "same", "\\ifx on one character twice");
    sets("\\ifx ab same\\else different\\fi", "different", "\\ifx on two characters");
    sets("\\ifx\\undefined\\alsoundefined same\\else different\\fi", "same", "\\ifx on two undefined names");
    sets("\\include{core/aliases}\\let\\bracket=[\\ifx\\bracket[ same\\else different\\fi", "same",
         "\\ifx sees a name \\let to a character as that character");
    sets("\\unless\\ifdefined\\missing absent\\fi", "absent", "\\unless inverts");
    sets("\\ifcsname relax\\endcsname known\\fi", "known", "\\ifcsname");
    sets("\\ifempty{} nothing\\else something\\fi", "nothing", "\\ifempty");
    sets("\\ifcat aa letters\\fi", "letters", "\\ifcat");
    sets("\\iftrue\\iffalse a\\else b\\fi\\else c\\fi", "b", "conditionals nest");
    sets("\\ifmmode maths\\else text\\fi", "text", "\\ifmmode outside a formula");
    sets("\\ifvmode vertical\\else horizontal\\fi", "vertical", "a document starts between paragraphs");
    sets("\\iffalse\\ifstrequal{a}{a}{x}{y}\\else skipped\\fi", "skipped",
         "a test that takes its branches as arguments is not counted as a nested \\if");
    sets("\\define\\iftoggle[3]{#2}\\iffalse\\iftoggle{a}{b}{c}\\else macro\\fi", "macro",
         "nor is a macro that only has an \\if name");

    sets("\\expandafter\\ifx\\csname nothing\\endcsname\\relax yes\\else no\\fi", "yes",
         "a name \\csname makes for nothing is \\relax itself to \\ifx");

    return 0;
}
