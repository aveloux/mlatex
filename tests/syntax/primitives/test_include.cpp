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

// The Include primitive: packages loaded once however often asked, their
// options kept as variables, packages provided without a file, embedded
// files read by name, and the writes and messages a package makes, which
// have nowhere to go but are read and let go.

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
    sets("\\usepackage{etoolbox}\\usepackage{etoolbox}\\ifpackageloaded{etoolbox}{loaded}{missing}", "loaded",
         "a package is loaded once however often asked");
    sets("\\ifpackageloaded{physics}{loaded}{missing}", "missing", "an unloaded package is missing");
    sets("\\usepackage[margin=2cm,draft]{etoolbox}\\variable{etoolbox.margin} \\variable{etoolbox.draft}",
         "2cm true", "a package's options become its variables");
    sets("\\usepackage[russian,english]{etoolbox}\\variable{etoolbox.options}", "russian,english",
         "and all of them together, in the order written");
    sets("\\providepackage{inputenc, graphicx}\\usepackage[utf8]{inputenc}\\ifpackageloaded{graphicx}{yes}{no}"
         "\\variable{inputenc.utf8}",
         "yestrue", "a provided package loads nothing, and keeps its options");
    sets("\\usepackage{xfp,csquotes}\\ifpackageloaded{csquotes}{both}{one}", "both", "several packages at once");
    sets("\\requirepackage{etoolbox}\\ifpackageloaded{etoolbox}{yes}{no}", "yes", "\\requirepackage");
    reports("\\usepackage{nosuch}", "warning: File `nosuch.sty' not found",
            "an unknown package is a warning that names it");
    {
        const auto [text, errors] = expand("\\usepackage{nosuch}\\ifpackageloaded{nosuch}{yes}{no}");
        assert((text == "yes" && errors.contains("warning: ")) && "and is marked loaded, as LaTeX has it");
    }
    reports("\\include{nosuch}", "File `nosuch.tex' not found", "an unknown file is named");
    sets("\\immediate\\write16{to the log}\\message{said}kept", "kept", "writes and messages set nothing");
    sets("\\iffile{nosuch.tex}{found}{absent}", "absent", "\\iffile of a file that is not there");
    return 0;
}
