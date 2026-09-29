#include "layout/ledger.hpp"
#include "memory/arena.hpp"

#include <cassert>
#include <string>

// The ledger: runs already shaped, found again by their font, their text
// and their size, in one probe on average -- and a table that stops taking
// runs once three quarters full, rather than moving any it handed out.

namespace layout = render::layout;

int main() {
    memory::Arena arena(1u << 20);
    layout::Ledger ledger(arena, 16);

    const memory::Slice<layout::Node*> glyphs = arena.allocate<layout::Node*>(3);
    for (layout::Node*& glyph : glyphs) glyph = arena.compose<layout::Node>(layout::Node::Type::Glyph);

    const layout::Ledger::Key key{.font = nullptr, .text = "word", .size = 10.0f};
    assert((ledger.get(key).empty()) && "a run never shaped is not found");
    ledger.set(key, glyphs);
    assert((ledger.get(key).data == glyphs.data) && "a run shaped is found, the same glyphs handed out");

    assert((ledger.get({.font = nullptr, .text = "word", .size = 12.0f}).empty()) && "another size is another run");
    assert((ledger.get({.font = nullptr, .text = "ward", .size = 10.0f}).empty()) && "and other text another");

    // The key's text is compared, not where it lives.
    const std::string copy = "word";
    assert((!ledger.get({.font = nullptr, .text = copy, .size = 10.0f}).empty()) &&
           "the same text from elsewhere is found");

    ledger.dispose();
    assert((ledger.get(key).empty()) && "a ledger disposed of has forgotten every run");

    // Three quarters full, it takes no more.
    std::string names[16];
    for (int index = 0; index < 16; ++index) {
        names[index] = "run" + std::to_string(index);
        ledger.set({.text = names[index], .size = 10.0f}, glyphs);
    }
    std::size_t kept = 0;
    for (const std::string& name : names) kept += !ledger.get({.text = name, .size = 10.0f}).empty();
    assert((kept == 12) && "a table of sixteen keeps twelve runs and stops");

    return 0;
}
