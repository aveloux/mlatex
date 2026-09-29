#include "syntax/cursor.hpp"

#include <cassert>
#include <span>
#include <vector>

// The cursor is a stack of buffers: what goes in last is read first, which is
// how a macro's expansion is read before whatever followed the macro.

/// @brief A letter token with nothing else about it.
static syntax::Token letter(const std::string_view text, const syntax::Symbol symbol) {
    return syntax::Token{.symbol = symbol, .category = syntax::CatCodes::Category::Letter, .text = text};
}

int main() {
    syntax::Cursor cursor({letter("a", 1), letter("b", 2), letter("c", 3)});
    assert((!cursor.empty() && cursor.size() == 3) && "a cursor holds what it was made with");
    assert((cursor.lookahead(0).text == "a" && cursor.lookahead(2).text == "c") && "lookahead reads without taking");
    assert((cursor.advance().text == "a") && "advance takes the first");
    assert((cursor.size() == 2 && cursor.consumed() == 1) && "one taken, two left");

    // Injected tokens are read before what was already pending.
    const std::vector<syntax::Token> batch{letter("x", 4), letter("y", 5)};
    cursor.inject(std::span{batch});
    assert((cursor.advance().text == "x" && cursor.advance().text == "y") && "injected tokens come first, in order");
    assert((cursor.advance().text == "b") && "then what was pending before");

    // A whole buffer adopted goes in front too.
    cursor.adopt({letter("p", 6), letter("q", 7)});
    assert((cursor.advance().text == "p" && cursor.advance().text == "q") && "an adopted buffer is read first");
    assert((cursor.advance().text == "c") && "then the rest");

    // Reading past the end is an empty token, not a crash.
    assert((cursor.empty()) && "everything read");
    assert((cursor.advance().empty()) && "past the end is an empty token");
    assert((cursor.lookahead(5).empty()) && "and so is looking past it");

    // Clearing discards everything pending.
    cursor.adopt({letter("z", 8)});
    cursor.dispose();
    assert((cursor.empty()) && "clear discards what was pending");

    const syntax::Cursor none;
    assert((none.empty() && none.size() == 0) && "a cursor made with nothing is empty");

    // The document's own buffer, beneath whatever is injected over it: read
    // out, and put back rewritten, as a \catcode change does.
    {
        syntax::Cursor source;
        source.adopt({letter("d", 9), letter("e", 10), letter("f", 11)});
        assert((source.advance().text == "d") && "the document read from");
        const std::vector<syntax::Token> over{letter("x", 12)};
        source.inject(std::span{over});
        const std::vector<syntax::Token> unread = source.source();
        assert((unread.size() == 2 && unread[0].text == "e" && unread[1].text == "f") &&
               "what the document has left, in reading order, without what was injected");
        source.source({letter("E", 13)});
        assert((source.advance().text == "x" && source.advance().text == "E" && source.empty()) &&
               "put back beneath what was injected over it");
    }

    return 0;
}
