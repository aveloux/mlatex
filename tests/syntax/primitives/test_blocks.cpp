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
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

// The Blocks primitive: \begin and \end, each block a scope of its own that
// must be closed by its own name, and \enter and \leave, the same under
// plainer names.

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

/// A document must set exactly this text, and report no error -- a block
/// of a name nothing here defines is only a warning, as it is set anyway.
static void sets(const std::string_view document, const std::string_view expected, const char* what) {
    const auto [text, reported] = expand(document);
    std::string errors;
    for (const auto line : std::views::split(reported, '\n')) {
        const std::string_view said(line.begin(), line.end());
        if (!said.empty() && !said.contains("warning: Environment ")) errors += std::string(said) + '\n';
    }
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
    sets("\\begin{quote}x\\end{quote}", "x", "a block holds its text");
    sets("\\enter{quote}x\\leave{quote}", "x", "\\enter and \\leave are the same");
    sets("\\begin{a}\\define\\inside{in}\\end{a}\\ifdefined\\inside yes\\else no\\fi", "no",
         "a definition ends with its block");
    sets("\\begin{a}\\begin{b}x\\end{b}\\end{a}", "x", "blocks nest");
    reports("\\begin{a}\\begin{b}\\end{a}\\end{b}", "\\begin{b} ended by \\end{a}", "a block closed out of order");
    reports("\\end{a}", "Extra \\end{a}", "\\end with nothing open");
    reports("\\begin{nosuch}x\\end{nosuch}", "warning: Environment nosuch undefined",
            "a block of a name nothing defines is named, and set");
    sets("\\define\\shout{<}\\define\\endshout{>}\\begin{shout}x\\end{shout}", "<x>",
         "a block of no hooks is its \\name and its \\endname, as LaTeX's are");
    reports("\\begin{}", "needs a block name", "\\begin needs a name");

    // A document's own blocks: the begin code with the block's arguments,
    // the end code before the block closes -- so it may close what the
    // begin code opened -- and a definition inside ending with the block.
    sets("\\newenvironment{note}[1][Note]{[#1:}{]}\\begin{note}a\\end{note}\\begin{note}[Tip]b\\end{note}",
         "[Note:a][Tip:b]", "a block a document defines, with its optional argument");
    sets("\\newenvironment{outer}{\\begin{quote}(}{)\\end{quote}}\\begin{outer}x\\end{outer}", "(x)",
         "its end code closes the block its begin code opened");
    sets("\\newenvironment{a}{\\define\\inside{in}}{}\\begin{a}\\end{a}\\ifdefined\\inside yes\\else no\\fi", "no",
         "what its begin code defines ends with it");
    sets("\\newenvironment{b}{<}{>}\\renewenvironment{b}{(}{)}\\begin{b}x\\end{b}", "(x)",
         "\\renewenvironment replaces a document's own block");
    sets("\\newenvironment{c}{<}{>}\\provideenvironment{c}{(}{)}\\begin{c}x\\end{c}", "<x>",
         "\\provideenvironment leaves one that is there");
    sets("\\NewDocumentEnvironment{d}{O{1} m}{#1#2}{!}\\begin{d}{2}x\\end{d}\\begin{d}[3]{4}y\\end{d}", "12x!34y!",
         "xparse's blocks, their arguments described by letters");
    return 0;
}
