/// @file
/// @brief Document implementation: the block list, and the lists inside it.
///
/// One rule decides everything here. Material that can sit in a line joins the
/// paragraph being built; material that cannot ends that paragraph and becomes
/// a block of its own. Blocks form a singly linked list with a tail
/// pointer, so appending stays constant however long the document is.
#include "layout/document.hpp"
#include "layout/line.hpp"
#include "layout/typesetter.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace render::layout {

    Document::Document(
        memory::Arena& arena,
        memory::Arena& scratch,
        const typography::Shaper& shaper,
        const Typesetter& typesetter,
        const Configuration& page
    ) noexcept
        : arena(arena), scratch(scratch), shaper(shaper), typesetter(typesetter),
          ledger(arena), words(arena, 4096), configuration_(page) {
        pending.reserve(1024);
    }

    Document::Document(
        memory::Arena& arena,
        memory::Arena& scratch,
        const typography::Shaper& shaper,
        const Typesetter& typesetter
    ) noexcept
        : Document(arena, scratch, shaper, typesetter, Configuration{}) {}

    void Document::attach(Element* element) noexcept {
        if (tail) {
            tail->next = element;
        } else {
            head = element;
        }
        tail = element;
        ++blocks;
    }

    void Document::hyphenate(const typography::Hyphenator* hyphenator, const std::size_t left,
                             const std::size_t right) noexcept {
        if (hyphenation != hyphenator || before != left || after != right) {
            words.dispose();
            ledger.dispose();
        }
        hyphenation = hyphenator;
        before = left;
        after = right;
    }

    void Document::append(const std::string_view text, const typography::Font& font, const float size,
                          const Node::Color* color) {
        if (text.empty()) return;

        // In a color: set as any text is, then each glyph that came out
        // copied with the color on it, the shared one left as it was.
        if (color) {
            const std::size_t first = pending.size();
            append(text, font, size, nullptr);
            for (std::size_t index = first; index < pending.size(); ++index) {
                if (pending[index]->type() != Node::Type::Glyph) continue;
                Node::Glyph mark = pending[index]->glyph();
                mark.color = *color;
                auto* copy = arena.compose<Node>(Node::Type::Glyph);
                copy->glyph(mark);
                pending[index] = copy;
            }
            return;
        }

        // The whole run may have been shaped before -- a running head, a
        // repeated label -- in which case there is nothing to do but copy the
        // nodes across. At the head of a paragraph a run's leading blanks set
        // nothing, so there it is known by what follows them: the same run
        // part way through a paragraph opens with a word space, and is not
        // to be mistaken for it.
        std::string_view known = text;
        if (pending.empty()) {
            const std::size_t first = known.find_first_not_of(" \t\n");
            known.remove_prefix(first == std::string_view::npos ? known.size() : first);
        }
        const Ledger::Key key{.font = &font, .text = known, .size = size};
        if (const memory::Slice<Node*> cached = ledger.find(key); !cached.empty()) {
            pending.insert(pending.end(), cached.begin(), cached.end());
            if (const std::size_t end = text.find_last_not_of(" \t\n"); end != std::string_view::npos) {
                factor = weigh(text.substr(0, end + 1), factor);
            }
            return;
        }

        const std::size_t start = pending.size();
        divide(pending, text, font, pending.empty());

        // Remembered only when this run began a paragraph, because otherwise
        // what was produced depends on what came before it -- the leading
        // space is dropped at the start of a paragraph and kept elsewhere.
        if (start == 0 && pending.size() > start) {
            const memory::Slice<Node*> shaped = arena.allocate<Node*>(pending.size() - start);
            std::copy(pending.begin() + static_cast<std::ptrdiff_t>(start), pending.end(),
                      shaped.begin());
            ledger.insert(key, shaped);
        }
    }

    memory::Slice<Node*> Document::set(const std::string_view text, const typography::Font& font, const bool opening,
                                       const Node::Color* color) {
        // A box's text starts afresh: what came before it says nothing of
        // the space after its first word.
        std::vector<Node*> nodes;
        const int kept = std::exchange(factor, 1000);
        divide(nodes, text, font, opening);
        factor = kept;

        // The words are shared with every other place they were set, so a
        // color goes on copies of their glyphs, as append() puts it.
        const memory::Slice<Node*> slice = arena.allocate<Node*>(nodes.size());
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            slice[index] = nodes[index];
            if (!color || nodes[index]->type() != Node::Type::Glyph) continue;
            Node::Glyph mark = nodes[index]->glyph();
            mark.color = *color;
            auto* copy = arena.compose<Node>(Node::Type::Glyph);
            copy->glyph(mark);
            slice[index] = copy;
        }
        return slice;
    }

    int Document::weigh(std::string_view text, const int previous) noexcept {
        // What closes a sentence or a quotation after its stop passes the
        // stop's weight through, as TeX's space factor code of 0 does.
        while (!text.empty()) {
            if (text.ends_with(')') || text.ends_with(']') || text.ends_with('"') || text.ends_with('\'')) {
                text.remove_suffix(1);
            } else if (text.ends_with("’") || text.ends_with("”")) {
                text.remove_suffix(3);
            } else {
                break;
            }
        }
        if (text.empty()) return previous;

        // After a capital, a stop is an abbreviation's -- U.S. -- and the
        // space after it an ordinary one.
        const char last = text.back();
        if (text.size() >= 2 && text[text.size() - 2] >= 'A' && text[text.size() - 2] <= 'Z') return 1000;
        switch (last) {
            case '.': case '?': case '!': return 3000;
            case ':': return 2000;
            case ';': return 1500;
            case ',': return 1250;
            default: return 1000;
        }
    }

    void Document::divide(std::vector<Node*>& into, const std::string_view text, const typography::Font& font,
                          const bool opening) {
        const std::size_t first = into.size();
        const typography::Font* fonts[] = {&font, fallback_};
        const std::size_t faces = fallback_ && fallback_ != &font ? 2 : 1;

        // One piece of the run -- a word, a dash, a quotation mark, the space
        // between two words -- as the nodes it sets as, shaped and hyphenated
        // the first time this font meets it and looked up every time after.
        // Prose repeats a few hundred words endlessly, so nearly every piece
        // is a lookup. The key's text points into the run or at a literal,
        // either of which outlives the document. Known by the font's own
        // size, so a word in a box and the same word in running text are
        // one entry.
        const auto piece = [&](const std::string_view part, const bool breakable) -> memory::Slice<Node*> {
            const Ledger::Key known{.font = &font, .text = part, .size = font.size()};
            if (const memory::Slice<Node*> cached = words.find(known); !cached.empty()) return cached;

            const memory::Slice<Node*> glyphs = shaper.shape(memory::Slice{fonts, faces}, part, {});
            if (glyphs.empty()) return {};

            // Hyphenation works on letters; the glyphs came out of shaping.
            // Those agree one for one unless the font ligated something, and
            // when they do not there is no honest way to say which glyph a
            // letter's break belongs after -- so that word is not broken.
            //
            // The patterns are read as the bytes of their UTF-8, so the word
            // is handed over the same way, each letter folded to lower case
            // first -- Latin, Greek, Cyrillic and Armenian -- and a break
            // found after a letter's last byte is a break after that letter.
            // The language's least letters either side of a break are counted
            // in letters, not bytes, here.
            memory::Slice<std::uint8_t> breaks{};
            if (breakable && hyphenation && glyphs.count <= part.size()) {
                const memory::Slice<std::uint32_t> bytes = scratch.allocate<std::uint32_t>(part.size());
                const memory::Slice<std::size_t> ends = scratch.allocate<std::size_t>(part.size());
                std::size_t count = 0;
                std::size_t written = 0;
                for (std::size_t at = 0; at < part.size();) {
                    const auto* data = reinterpret_cast<const std::uint8_t*>(part.data() + at);
                    const std::size_t rest = part.size() - at;
                    std::uint32_t code = data[0];
                    std::size_t length = 1;
                    if ((data[0] & 0xE0) == 0xC0 && rest >= 2) {
                        code = ((data[0] & 0x1Fu) << 6) | (data[1] & 0x3Fu);
                        length = 2;
                    } else if ((data[0] & 0xF0) == 0xE0 && rest >= 3) {
                        code = ((data[0] & 0x0Fu) << 12) | ((data[1] & 0x3Fu) << 6) | (data[2] & 0x3Fu);
                        length = 3;
                    }
                    at += length;

                    if ((code >= 'A' && code <= 'Z') || (code >= 0xC0 && code <= 0xDE && code != 0xD7) ||
                        (code >= 0x391 && code <= 0x3AB) || (code >= 0x410 && code <= 0x42F)) {
                        code += 0x20;
                    } else if (code >= 0x400 && code <= 0x40F) {
                        code += 0x50;
                    } else if (code >= 0x531 && code <= 0x556) {
                        code += 0x30;
                    } else if (((code >= 0x100 && code <= 0x137) || (code >= 0x14A && code <= 0x177) ||
                                (code >= 0x460 && code <= 0x4BF) || (code >= 0x4D0 && code <= 0x4FF)) && code % 2 == 0) {
                        code += 1;
                    } else if (((code >= 0x139 && code <= 0x148) || (code >= 0x179 && code <= 0x17E)) && code % 2 == 1) {
                        code += 1;
                    }

                    if (code < 0x80) {
                        bytes[written++] = code;
                    } else if (code < 0x800) {
                        bytes[written++] = 0xC0 | (code >> 6);
                        bytes[written++] = 0x80 | (code & 0x3F);
                    } else {
                        bytes[written++] = 0xE0 | (code >> 12);
                        bytes[written++] = 0x80 | ((code >> 6) & 0x3F);
                        bytes[written++] = 0x80 | (code & 0x3F);
                    }
                    ends[count++] = written - 1;
                }

                if (count == glyphs.count && count >= before + after) {
                    const memory::Slice<std::uint8_t> found =
                        hyphenation->execute(scratch, memory::Slice{bytes.data, written}, '.', 1, 1);
                    if (!found.empty()) {
                        breaks = scratch.allocate<std::uint8_t>(count);
                        for (std::size_t index = 0; index < count; ++index) {
                            breaks[index] = index + 1 >= before && count - index - 1 >= after &&
                                            ends[index] < found.count && found[ends[index]];
                        }
                    }
                }
            }

            // A flagged penalty is a break that costs a hyphen; the breaker
            // draws one when it takes the break and nothing when it does not.
            // A word with nowhere to break is the shaper's own slice as it
            // stands.
            std::size_t penalties = 0;
            for (std::size_t index = 0; index + 1 < glyphs.count && index < breaks.count; ++index) {
                if (breaks[index]) ++penalties;
            }

            memory::Slice<Node*> nodes = glyphs;
            if (penalties > 0) {
                nodes = arena.allocate<Node*>(glyphs.count + penalties);
                std::size_t filled = 0;
                for (std::size_t index = 0; index < glyphs.count; ++index) {
                    nodes[filled++] = glyphs[index];
                    if (index + 1 < glyphs.count && index < breaks.count && breaks[index]) {
                        auto* penalty = arena.compose<Node>(Node::Type::Penalty);
                        penalty->penalty({.value = 50, .flag = true});
                        nodes[filled++] = penalty;
                    }
                }
            }

            words.insert(known, nodes);
            return nodes;
        };

        std::size_t cursor = 0;
        while (cursor < text.size()) {
            if (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\n') {
                while (cursor < text.size() &&
                       (text[cursor] == ' ' || text[cursor] == '\t' || text[cursor] == '\n')) {
                    ++cursor;
                }

                // A space at the very start of a paragraph is the source's
                // indentation, not a word gap, and would open the first line
                // with a hole.
                if (opening && into.size() == first) continue;

                // Shaped once for the font; every gap between words shares it
                // -- but for one after a sentence's stop, or after a colon,
                // semicolon or comma, which is TeX's: as much again as the
                // space factor asks to stretch and a share of it to shrink,
                // and a third of a space more after a stop or a colon.
                if (const memory::Slice<Node*> space = piece(" ", false); !space.empty()) {
                    Node* gap = space[0];
                    if (factor != 1000 && gap->type() == Node::Type::Glue) {
                        Node::Glue spread = gap->glue();
                        const float ratio = static_cast<float>(factor) / 1000.0f;
                        if (factor >= 2000) spread.width += spread.width / 3.0f;
                        spread.stretch *= ratio;
                        spread.shrink /= ratio;
                        gap = arena.compose<Node>(Node::Type::Glue);
                        gap->glue(spread);
                    }
                    into.push_back(gap);
                }
                factor = 1000;
                continue;
            }

            // A run of hyphens is a dash, and how many says which. The
            // convention is TeX's, and it is how a document writes an em dash
            // on a keyboard with no key for one. A font could do this with a
            // ligature of its own; the faces a document set ships with
            // generally do not, so the engine does it instead.
            if (text[cursor] == '-') {
                std::size_t hyphens = 0;
                while (cursor < text.size() && text[cursor] == '-') {
                    ++cursor;
                    ++hyphens;
                }

                const std::string_view dash = hyphens >= 3   ? "—"
                                              : hyphens == 2 ? "–"
                                                             : "-";
                const memory::Slice<Node*> drawn = piece(dash, false);
                into.insert(into.end(), drawn.begin(), drawn.end());
                factor = 1000;

                // A line may end after a dash, at `\\exhyphenpenalty`'s price,
                // as TeX lets it after any hyphen it is given.
                auto* chance = arena.compose<Node>(Node::Type::Penalty);
                chance->penalty({.value = 50});
                into.push_back(chance);
                continue;
            }

            // A backtick opens a quotation, an apostrophe closes one, and two
            // of either make it a double quotation -- TeX's own convention
            // for a keyboard with no curly quotes of its own, kept here for
            // the same reason the dash run above is: a document that already
            // reads as `` `like this'' '' should not have to be rewritten for
            // a font that draws no ligature over it.
            if (text[cursor] == '`' || text[cursor] == '\'') {
                const char mark = text[cursor];
                std::size_t run = 0;
                while (cursor < text.size() && text[cursor] == mark) {
                    ++cursor;
                    ++run;
                }

                const std::string_view quote =
                    mark == '`' ? (run >= 2 ? "“" : "‘")
                                : (run >= 2 ? "”" : "’");
                const memory::Slice<Node*> drawn = piece(quote, false);
                into.insert(into.end(), drawn.begin(), drawn.end());
                continue;
            }

            const std::size_t begun = cursor;
            while (cursor < text.size() && text[cursor] != ' ' && text[cursor] != '\t' &&
                   text[cursor] != '\n' && text[cursor] != '-' && text[cursor] != '`' &&
                   text[cursor] != '\'') {
                ++cursor;
            }

            const memory::Slice<Node*> word = piece(text.substr(begun, cursor - begun), true);
            into.insert(into.end(), word.begin(), word.end());
            factor = weigh(text.substr(begun, cursor - begun), factor);
        }
    }

    void Document::append(const syntax::expression::Node* expression, const typography::Font& font,
                          const Node::Color* color) {
        if (!expression) return;
        factor = 1000;

        // Inline: dropped into the line in pieces, split where TeX would let
        // a line end inside it -- after a relation or a binary operator.
        if (expression->style != syntax::expression::Node::Style::Display) {
            const memory::Slice<Node*> pieces = typesetter.unfold(expression, font);
            if (color) {
                for (Node* piece : pieces) Typesetter::paint(piece, *color);
            }
            pending.insert(pending.end(), pieces.begin(), pieces.end());
            return;
        }

        // Displayed: a block of its own, centred in the column, with the text
        // on either side of it in paragraphs of their own.
        separate();
        Node* box = typesetter.lower(expression, font, column());
        if (box && color) Typesetter::paint(box, *color);
        if (box) display(box);
    }

    void Document::display(Node* box) {
        if (!box) return;

        // \abovedisplayskip and \belowdisplayskip: the body's size either
        // side, as LaTeX's classes set them, giving a little and taking half.
        const float skip = configuration_.size;
        const auto space = [this, skip] {
            auto* glue = arena.compose<Node>(Node::Type::Glue);
            glue->glue({.width = skip, .stretch = skip * 0.2f, .shrink = skip * 0.5f});
            return glue;
        };
        append(space(), true);
        append(box, true);
        append(space(), true);
    }

    void Document::append(Node* node, const bool block) {
        if (!node) return;
        if (node->type() != Node::Type::Directive) factor = 1000;

        // An instruction, not material: it changes how the paragraphs from
        // here on are assembled and takes no room of its own.
        if (node->type() == Node::Type::Directive) {
            const Node::Directive& order = node->directive();
            switch (order.command) {
                case Node::Directive::Command::Indent:
                    // Before a paragraph, the paragraph's own indentation;
                    // part way through one, an indent where it stands.
                    if (pending.empty()) {
                        indentation = true;
                        suppression = false;
                    } else {
                        auto* space = arena.compose<Node>(Node::Type::Kern);
                        space->kern({.width = configuration_.indent});
                        pending.push_back(space);
                    }
                    break;
                case Node::Directive::Command::Flush:
                    if (pending.empty()) indentation = false;
                    break;
                case Node::Directive::Command::Suppress:
                    indentation = false;
                    suppression = true;
                    break;
                case Node::Directive::Command::Align:
                    justification = order.justification;
                    break;
                case Node::Directive::Command::Margin:
                    if (order.trailing) gutter = order.width; else margin = order.width;
                    break;
                case Node::Directive::Command::Save:
                    shapes.push_back(Shape{justification, margin, gutter, reversed});
                    break;
                case Node::Directive::Command::Restore:
                    if (!shapes.empty()) {
                        justification = shapes.back().justification;
                        margin = shapes.back().margin;
                        gutter = shapes.back().gutter;
                        reversed = shapes.back().reversed;
                        shapes.pop_back();
                    }
                    break;
                case Node::Directive::Command::Language:
                    hyphenate(order.hyphenator, order.before, order.after);
                    break;
                case Node::Directive::Command::Direction:
                    reversed = order.reversed;
                    break;
                case Node::Directive::Command::Number:
                case Node::Directive::Command::Note:
                case Node::Directive::Command::Aside:
                case Node::Directive::Command::Link:
                case Node::Directive::Command::Unlink:
                    // Material after all: the number, set in the line, and a
                    // footnote or a margin note, carried in the line to the
                    // page -- and the height on it -- it lands at.
                    pending.push_back(node);
                    break;
                case Node::Directive::Command::Anchor:
                    // In the line when there is one, so it lands where the
                    // words around it do; between blocks otherwise, where it
                    // must not open a paragraph of nothing.
                    if (!pending.empty()) {
                        pending.push_back(node);
                        break;
                    }
                    [[fallthrough]];
                case Node::Directive::Command::Hold:
                case Node::Directive::Command::Release:
                case Node::Directive::Command::Page:
                case Node::Directive::Command::Barrier:
                case Node::Directive::Command::Columns: {
                    // A block of no height in the column, so it lands on the
                    // page where the text around it did, and the composer
                    // meets it as it draws that page -- or, around a float,
                    // so the pager knows which blocks it may not part, where
                    // no float may pass, and where the columns change. The
                    // paragraph before a change is set in the columns it was
                    // written in.
                    if (!pending.empty()) separate();
                    if (order.command == Node::Directive::Command::Columns) split = order.index;
                    auto* element = arena.compose<Element>();
                    element->type = Element::Type::Directive;
                    element->node = node;
                    attach(element);
                    break;
                }
            }
            return;
        }

        if (!block) {
            pending.push_back(node);
            return;
        }

        // A block ends the paragraph it interrupts, if one is open. With none
        // open it is not a blank line either: the skip below a display leaves
        // the text after it carrying on, unindented, as LaTeX's does.
        if (!pending.empty()) separate();

        // A box set while the paragraphs are centred is centred too, as
        // `\\centering` centres a table or a picture in LaTeX; one set flush
        // right goes to the right, and any set inside margins -- a table in
        // a quotation -- stands inside them. A box already as wide as the
        // column, a display, is left as it is.
        // A line built whole -- a heading, a caption -- is put in the order
        // it is drawn in, as a paragraph's lines are: a number before its
        // title stands at the title's right, right to left.
        Line::reorder(arena, node, reversed);

        const float room = column() - margin - gutter;
        const bool material = node->type() == Node::Type::Box || node->type() == Node::Type::Bitmap;
        if (material && Line::advance(node) < room - 0.5f &&
            (justification == Node::Justification::Center || justification == Node::Justification::Right ||
             margin > 0.0f || gutter > 0.0f || reversed)) {
            const auto fill = [this] {
                auto* glue = arena.compose<Node>(Node::Type::Glue);
                glue->glue({.stretch = 1.0f, .expand = Node::Order::Fil});
                return glue;
            };
            const auto kern = [this](const float width) {
                auto* space = arena.compose<Node>(Node::Type::Kern);
                space->kern({.width = width});
                return space;
            };
            // Right to left, a block stands at the right as text does.
            const bool centred = justification == Node::Justification::Center;
            const bool right = justification == Node::Justification::Right ||
                               (reversed && justification != Node::Justification::Left);

            const memory::Slice<Node*> row = arena.allocate<Node*>(5);
            std::size_t filled = 0;
            row[filled++] = kern(margin);
            if (centred || right) row[filled++] = fill();
            row[filled++] = node;
            row[filled++] = centred || !right ? fill() : kern(0.0f);
            row[filled++] = kern(gutter);
            node = Line::horizontal(arena, memory::Slice{row.data, filled}, column());
        }

        auto* element = arena.compose<Element>();
        element->type = Element::Type::Directive;
        element->node = node;
        attach(element);

        // Text straight after a display or a table carries on from it rather
        // than starting afresh, so LaTeX does not indent it. Space and page
        // breaks are not material and leave the rule as it was.
        if (node->type() == Node::Type::Box) indentation = false;
    }

    void Document::separate() {
        // A blank line with nothing before it: a new paragraph is starting,
        // so whatever carried on from the block before no longer does --
        // unless a heading said otherwise.
        if (pending.empty()) {
            if (!suppression) indentation = true;
            return;
        }

        // The indentation is a kern of its own ahead of the first word, and
        // only a justified paragraph has one: LaTeX's ragged and centred
        // settings take it away.
        const bool indented = indentation && justification == Node::Justification::Full &&
                              configuration_.indent > 0.0f;
        const std::size_t lead = indented ? 1 : 0;

        const memory::Slice<Node*> content = arena.allocate<Node*>(pending.size() + lead);
        if (indented) {
            auto* space = arena.compose<Node>(Node::Type::Kern);
            space->kern({.width = configuration_.indent});
            content[0] = space;
        }
        std::copy(pending.begin(), pending.end(), content.begin() + lead);
        pending.clear();

        auto* element = arena.compose<Element>();
        element->type = Element::Type::Paragraph;
        element->paragraph = arena.compose<Paragraph>(arena, content, justification, margin, gutter, reversed);
        element->columns = split ? split : configuration_.columns;
        attach(element);

        indentation = true;
        suppression = false;
    }

    void Document::layout() noexcept {
        // Whatever was still being built is a paragraph too; a document rarely
        // ends with a blank line.
        separate();

        for (Element* element = head; element; element = element->next) {
            if (element->type == Element::Type::Paragraph && element->paragraph) {
                element->paragraph->layout(scratch, column(element->columns), configuration_.leading);
            }
        }
    }

    memory::Slice<Document::Element*> Document::elements() const noexcept {
        if (blocks == 0) return {};

        const memory::Slice<Element*> slice = arena.allocate<Element*>(blocks);
        std::size_t filled = 0;
        for (Element* element = head; element; element = element->next) {
            slice[filled++] = element;
        }
        return slice;
    }

}
