#include "memory/arena.hpp"
#include "syntax/lexer.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>

// The lexer turns text into tokens the way TeX's eyes do: a control word
// swallows the spaces after it, a run of spaces is one, a line ending is a
// space and an empty line is \par, and a comment takes its own line ending
// with it.

using Category = syntax::Catcodes::Category;

/// @brief Every token a text lexes into.
static std::vector<syntax::Token> lex(syntax::Lexicon& lexicon, const std::string_view text,
                                      std::size_t* errors = nullptr) {
    syntax::Catcodes codes;
    syntax::Lexer lexer(text, codes, lexicon);
    std::vector<syntax::Token> tokens;
    while (!lexer.empty()) {
        if (const syntax::Token token = lexer.advance(); !token.empty()) tokens.push_back(token);
    }
    if (errors) *errors = lexer.traceback().size();
    return tokens;
}

/// @brief The tokens' text, one after another, a bar between each.
static std::string spell(const std::vector<syntax::Token>& tokens) {
    std::string text;
    for (const syntax::Token& token : tokens) {
        if (!text.empty()) text += '|';
        text += token.text;
    }
    return text;
}

int main() {
    memory::Arena arena;
    syntax::Lexicon lexicon(arena);

    // A control word, a group and letters, each a token of its own.
    {
        const auto tokens = lex(lexicon, "\\documentclass{article}");
        assert((tokens.size() == 10) && "a control word, two braces and seven letters");
        assert((tokens[0].category == Category::Escape && tokens[0].text == "\\documentclass") &&
               "a control word is one escape token");
        assert((tokens[1].category == Category::Group && tokens[1].text == "{") &&
               "an opening brace is a group token");
        assert((tokens[2].category == Category::Letter && tokens[2].text == "a") && "a letter is a letter token");
        assert((tokens[9].text == "}") && "the closing brace ends it");
    }

    // Spaces: swallowed after a control word, folded to one elsewhere.
    assert((spell(lex(lexicon, "\\foo   bar")) == "\\foo|b|a|r") && "a control word swallows the spaces after it");
    assert((spell(lex(lexicon, "a    b")) == "a| |b") && "a run of spaces is one space");
    assert((spell(lex(lexicon, "\\% x")) == "\\%| |x") && "a control symbol keeps the space after it");

    // Line endings: a space inside a paragraph, \par at an empty line.
    assert((spell(lex(lexicon, "a\nb")) == "a| |b") && "a line ending is a space");
    assert((spell(lex(lexicon, "a\n\nb")) == "a| |\\par|b") && "an empty line is \\par");
    assert((spell(lex(lexicon, "a\r\n\r\nb")) == "a| |\\par|b") && "Windows line endings read the same");
    assert((spell(lex(lexicon, "a\n\n\n\nb")) == "a| |\\par|b") && "several empty lines are one \\par");

    // Comments take their line ending with them.
    assert((spell(lex(lexicon, "a% note\nb")) == "a|b") && "a comment swallows its line ending");
    assert((spell(lex(lexicon, "a%\n\nb")) == "a|\\par|b") && "an empty line after a comment is still \\par");

    // Characters past ASCII are one token each, however many bytes.
    {
        const auto tokens = lex(lexicon, "\xC3\xA9t\xC3\xA9");
        assert((tokens.size() == 3) && "an accented letter is one token");
        assert((tokens[0].text == "\xC3\xA9") && "and keeps both its bytes");
        const auto symbol = lex(lexicon, "\\\xE2\x82\xAC");
        assert((symbol.size() == 1 && symbol[0].text == "\\\xE2\x82\xAC") &&
               "a control symbol may be past ASCII too");
    }

    // Where each token came from.
    {
        const auto tokens = lex(lexicon, "ab\n  \\cd");
        assert((tokens[0].location.line == 1 && tokens[0].location.column == 1) && "the first token is at 1:1");
        assert((tokens[1].location.column == 2) && "columns count across");
        assert((tokens.back().location.line == 2 && tokens.back().location.column == 3) && "lines count down");
    }

    // The same name is the same symbol, wherever it was read.
    {
        const auto first = lex(lexicon, "\\alpha");
        const auto second = lex(lexicon, "x\\alpha");
        assert((first[0].symbol == second[1].symbol) && "a name is interned once");
    }

    // A byte no character starts with is reported, and skipped.
    {
        std::size_t errors = 0;
        const auto tokens = lex(lexicon, std::string_view("a\x7F" "b", 3), &errors);
        assert((errors == 1) && "an illegal byte is reported");
        assert((spell(tokens) == "a|b") && "and the text around it still reads");
    }

    assert((lex(lexicon, "").empty()) && "nothing lexes to nothing");

    // Text read as it stands: \verb's, one token after the command, and a
    // verbatim block's body, one token between its \begin and its \end.
    {
        const auto tokens = lex(lexicon, "\\verb|a_b % $x$| c");
        assert((spell(tokens) == "\\verb|a_b % $x$| |c") && "\\verb's text is one token, comment and all");
        assert((tokens[1].symbol == syntax::none && tokens[1].category == Category::Other) &&
               "a token no primitive or macro can mean");
        assert((spell(lex(lexicon, "\\verb*+a b+")) == "\\verb|a b") && "any delimiter, and the starred form");
        assert((spell(lex(lexicon, "\\lstinline{g()}")) == "\\lstinline|g()") && "\\lstinline in braces");
        assert((spell(lex(lexicon, "\\verb|open")) == "\\verb|||o|p|e|n") &&
               "an unclosed \\verb leaves the text alone");

        const auto block = lex(lexicon, "\\begin{verbatim}\n  \\x % y\n\n z\n\\end{verbatim}");
        assert((block.size() > 12 && block[11].text == "  \\x % y\n\n z") &&
               "a verbatim body whole, without the line ends around it");
        assert((block.size() > 12 && block[12].text == "\\end") && "and the block's \\end lexed as usual after it");

        const auto listing = lex(lexicon, "\\begin{lstlisting}[language=C]\nint x;\n\\end{lstlisting}");
        const auto body = std::ranges::find_if(listing, [](const syntax::Token& token) {
            return token.symbol == syntax::none;
        });
        assert((body != listing.end() && body->text == "int x;") && "a listing's body after its options");
    }

    return 0;
}
