#include "memory/arena.hpp"
#include "syntax/mouth.hpp"
#include "syntax/parser.hpp"
#include "syntax/semantics/union.hpp"

#include <cassert>
#include <string>

// The parser turns what the expander hands it into nodes: text, paragraph
// breaks, and whatever a bound command builds. What it cannot make sense of
// it reports and reads past, so one mistake does not end the document.

/// @brief Parses a text with nothing bound but what the test binds.
struct Harness {
    memory::Arena arena{1u << 20};
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon{arena};
    syntax::Mouth mouth{syntax::Cursor{}, state, lexicon, arena};
    syntax::Parser parser{mouth, arena};

    memory::Slice<syntax::Node*> parse(const std::string_view text) {
        mouth.ingest(arena.copy(text));
        return parser.parse(0);
    }
};

/// @brief How many nodes of a kind a list holds.
static std::size_t count(const memory::Slice<syntax::Node*> nodes, const syntax::Node::Type type) {
    std::size_t found = 0;
    for (const syntax::Node* node : nodes) found += node && node->type == type;
    return found;
}

int main() {
    // Text and paragraph breaks.
    {
        Harness harness;
        const auto nodes = harness.parse("Welcome to LaTeX.\n\nA second paragraph.");
        assert((!nodes.empty()) && "text parses to nodes");
        assert((count(nodes, syntax::Node::Type::Text) >= 2) && "text is text");
        assert((count(nodes, syntax::Node::Type::Paragraph) == 1) && "an empty line is one paragraph break");
        std::string text;
        for (const syntax::Node* node : nodes) {
            if (node->type == syntax::Node::Type::Text) text += node->value;
        }
        assert((text.find("Welcome") != std::string::npos && text.find("second") != std::string::npos) &&
               "the text is kept as written");
        assert((harness.parser.tracebacks().empty()) && "and nothing is reported");
    }

    // A command a module binds builds its own node.
    {
        Harness harness;
        harness.parser.bind("\\mark", [](syntax::Parser& parser) -> syntax::Node* {
            auto* node = parser.arena().compose<syntax::Node>();
            node->type = syntax::Node::Type::Directive;
            return node;
        });
        const auto nodes = harness.parse("a\\mark b\\mark");
        assert((count(nodes, syntax::Node::Type::Directive) == 2) && "a bound command builds its node each time");
    }

    // An unknown command is reported, and what follows still parses.
    {
        Harness harness;
        const auto nodes = harness.parse("before \\nosuch after");
        bool named = false;
        for (const syntax::Traceback& fault : harness.parser.tracebacks()) {
            named = named || fault.format().find("\\nosuch") != std::string::npos;
        }
        assert((named) && "an unknown command is reported by name");
        std::string text;
        for (const syntax::Node* node : nodes) {
            if (node->type == syntax::Node::Type::Text) text += node->value;
        }
        assert((text.find("after") != std::string::npos) && "and the text after it still parses");
    }

    // Nothing parses to nothing.
    {
        Harness harness;
        assert((harness.parse("").empty()) && "an empty document is no nodes");
    }

    return 0;
}
