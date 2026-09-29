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

// The Compute primitive: \evaluate, integer arithmetic with the usual
// precedence, brackets, powers and remainders, over numerals written in
// any of TeX's bases.

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
    sets("\\evaluate{2 + 3 * 4}", "14", "multiplication binds tighter");
    sets("\\evaluate{(2 + 3) * 4}", "20", "brackets");
    sets("\\evaluate{-7 / 2}", "-3", "division truncates toward zero");
    sets("\\evaluate{17 \\mod 5}", "2", "\\mod");
    sets("\\evaluate{2 ^ 10}", "1024", "powers");
    sets("\\evaluate{\"FF + '10}", "263", "hexadecimal and octal operands");
    sets("\\set\\integer3 = 6 \\evaluate{\\integer3 * 7}", "42", "a register as an operand");
    reports("\\evaluate{1 / 0}", "", "division by zero is reported");
    return 0;
}
