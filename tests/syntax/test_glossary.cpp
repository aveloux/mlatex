#include "memory/arena.hpp"
#include "syntax/cursor.hpp"
#include "syntax/glossary.hpp"
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

// The Glossary: CTAN's commands the engine has no package for, each read
// from its line the first time a document uses it -- defined for the rest
// of the run, taking a paragraph in an argument, never read before it is
// asked for, and never in place of a meaning the document or the engine
// already gave the name.

/// What a document sets, runs of blanks folded to one, and everything it
/// reported: read by the expander and the parser with the syntax primitives
/// bound and the kernel read first, as every run reads it, and no page in
/// sight.
static std::pair<std::string, std::string> expand(const std::string_view document) {
    memory::Arena arena(1u << 22);
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    syntax::primitives::Wrapper core(lexicon);
    syntax::primitives::Context context{state.registers, core.relay, core.variables};
    core(mouth, context);
    mouth.ingest(arena.copy("\\include{core/aliases}\\include{core/kernel}" + std::string(document)));
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

int main() {
    // --- The table the compiler builds ----------------------------------------------------
    const auto line = syntax::Glossary::get("\\contour");
    assert((line && *line == "\\contour[3][]{#3}") && "a command is found by its name, its line whole");
    assert((!syntax::Glossary::get("\\cont")) && "a name is found whole, not by a prefix of one");
    assert((!syntax::Glossary::get("\\nosuchcommand")) && "a name it does not hold is not found");
    assert((!syntax::Glossary::get("% microtype: the line breaker")) && "a comment is not a command");
    const auto helper = syntax::Glossary::get("\\@gobbleoptiontwo");
    assert((helper && helper->ends_with("{}") && !helper->ends_with("\r")) && "a line keeps no line ending");

    // --- Keeping what a command was for, or letting it go ---------------------------------
    sets("\\contour{white}{mot}", "mot", "a command keeps the text it was given");
    sets("\\microtypecontext{spacing=nonfrench}Wort", "Wort", "a setting is read and let go");
    sets("\\microtypesetup{protrusion=true}\\textls[200]{spaced}", "spaced", "one after another");
    sets("\\newfontfamily\\greek{GFS Didot}[Scale=0.9]\\greek x", "x",
         "a font's name between two options is read whole, and the command it names means nothing");
    sets("\\DeclareCaptionStyle{ruled}[justification=centering]{labelfont=bf}x", "x",
         "a name, an option, and a list of keys");
    sets("\\declaretheoremstyle[spaceabove=6pt]{plain}x", "x", "an option on the command itself");

    // --- Defined for good, once -----------------------------------------------------------
    sets("\\begin{x}\\textls{a}\\end{x}\\textls{b}", "ab", "defined outside the block it was first met in");
    {
        const auto [text, errors] = expand("\\contour{white}{a\\par b}");
        assert((errors.empty() && text.starts_with('a') && text.ends_with('b')) &&
               "an argument may hold a paragraph, as \\newcommand's may");
    }
    sets("\\newcommand\\textls[1]{[#1]}\\textls{a}", "[a]", "a document's own definition comes first");
    sets("\\def\\x{a}\\appto\\x{b}\\preto\\x{c}\\x", "cab", "etoolbox's additions to a macro");
    sets("\\csdef{name}{d}\\csuse{name}\\ifcsdef{name}{e}{f}", "de", "etoolbox's names built from text");

    // --- A name no one gives a meaning -------------------------------------------------------
    {
        const auto [text, errors] = expand("\\nosuchcommand x");
        assert((text == "x" && errors.contains("\\nosuchcommand")) && "is still an error");
    }
    return 0;
}
