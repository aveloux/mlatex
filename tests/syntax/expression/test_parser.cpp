#include "memory/arena.hpp"
#include "render/primitives/expressions.hpp"
#include "syntax/cursor.hpp"
#include "syntax/expression/parser.hpp"
#include "syntax/expression/unicodes.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/semantics/union.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// The formula parser: a formula read into a tree -- operators by their
// weight, scripts on their base, fractions, radicals, accents, fences,
// grids and words -- with the grammar the render layer gives it. Each tree
// is printed compactly, a node as its kind and what it holds, so an
// expectation reads as the structure it describes.

namespace expression = syntax::expression;

/// @brief One node and everything under it, as `kind(value)[children]`.
static void print(const expression::Node* node, std::string& text) {
    if (!node) {
        text += '_';
        return;
    }
    using Type = expression::Node::Type;
    static constexpr const char* kinds[] = {"var", "bin", "un", "group", "script", "frac", "root", "accent", "seq",
                                            "grid", "text"};
    text += kinds[static_cast<std::size_t>(node->type)];
    if (!node->value.empty()) {
        text += '(';
        text += node->value;
        text += ')';
    }
    std::vector<const expression::Node*> children;
    switch (node->type) {
        case Type::Script:
            children = {node->left, node->subscript, node->superscript};
            break;
        case Type::Sequence:
        case Type::Matrix:
            for (const expression::Node* child : node->arguments) children.push_back(child);
            break;
        case Type::Variable:
        case Type::Text:
            break;
        default:
            if (node->left || node->right) children = {node->left, node->right};
            break;
    }
    if (children.empty()) return;
    text += '[';
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (index > 0) text += ',';
        print(children[index], text);
    }
    text += ']';
}

/// @brief What one formula parsed to, and whether it reported anything.
struct Tree {
    std::string text{};    ///< The tree, printed.
    bool quiet{true};      ///< Nothing reported.
};

/// @brief Parses a formula as `$...$` holds one.
static Tree parse(const std::string_view formula, const bool displayed = false) {
    memory::Arena arena(1u << 20);
    syntax::Lexicon lexicon(arena);
    syntax::semantics::Union state{};
    const expression::Unicodes unicodes;
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    mouth.ingest(arena.copy(std::string(formula) + "$"));

    const render::primitives::Expressions expressions(lexicon);
    expression::Parser parser(mouth, unicodes, expressions.grammar, arena,
                              displayed ? expression::Node::Style::Display : expression::Node::Style::Inline);
    Tree tree;
    print(parser.parse('$'), tree.text);
    tree.quiet = parser.traceback().empty() && mouth.traceback().empty();
    return tree;
}

/// @brief A formula must parse to exactly this tree, reporting nothing.
static void reads(const std::string_view formula, const std::string_view expected, const char* what) {
    const Tree tree = parse(formula);
    const bool held = tree.text == expected && tree.quiet;
    if (!held) {
        std::fprintf(stderr, "  formula:  %.*s\n  expected: %.*s\n  got:      %s%s\n", static_cast<int>(formula.size()),
                     formula.data(), static_cast<int>(expected.size()), expected.data(), tree.text.c_str(),
                     tree.quiet ? "" : " (and reported something)");
    }
    assert((held) && what);
}

int main() {
    reads("x", "var(x)", "a letter");
    reads("a+b", "bin(+)[var(a),var(b)]", "a binary operator");
    reads("x^2", "script[var(x),_,var(2)]", "a superscript");
    reads("x_i^2", "script[var(x),var(i),var(2)]", "both scripts on one base");
    reads("\\frac{a}{b}", "frac(\\frac)[var(a),var(b)]", "a fraction");
    reads("a+b \\over c", "frac(\\over)[bin(+)[var(a),var(b)],var(c)]",
          "TeX's own fraction: everything before \\over over everything after it");
    reads("n \\choose k", "frac(\\choose)[var(n),var(k)]", "a binomial written between its parts");
    {
        const Tree grouped = parse("1+{1 \\atop 2}");
        assert((grouped.quiet && grouped.text.find("frac(\\atop)[var(1),var(2)]") != std::string::npos) &&
               "\\atop takes its group's parts, and no more");
    }
    reads("\\sqrt{x}", "root(\\sqrt)[_,var(x)]", "a square root");
    reads("\\sqrt[3]{x}", "root(\\sqrt)[var(3),var(x)]", "a root with its degree");
    reads("\\hat{a}", "accent(\\hat)[var(a),_]", "an accent over its base");
    reads("\\text{two words}", "seq(\\text)[text(two words)]", "words set as written");
    reads("a\\!{}_{2}", "seq[var(a),seq(\\!),script[group,group[var(2),_],_]]",
          "a space takes nothing, so the braces after it are an empty base of their own");
    {
        const Tree fenced = parse("\\left(x\\right)");
        assert((fenced.quiet && fenced.text.starts_with("group")) && "\\left and \\right fence a group");
    }
    {
        const Tree grid = parse("\\pmatrix{a & b \\\\ c & d}");
        assert((grid.quiet && grid.text.starts_with("grid") &&
               grid.text.find("var(a),var(b),var(c),var(d)") != std::string::npos) && "a matrix holds its four cells");
    }
    {
        const Tree broken = parse("\\frac{a}");
        assert((!broken.quiet) && "a fraction short of its denominator is reported");
    }
    return 0;
}
