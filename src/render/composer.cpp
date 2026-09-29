/// @file
/// @brief Composer implementation: the box tree into a page description.
///
/// The walk keeps one invariant. A node is drawn at the position its parent's
/// pen has reached, with its own shift and offset already added by node();
/// inside() then moves that pen past it. Measuring in Line uses the same two
/// rules -- advance() along a line, extent() down a column -- so a box always
/// occupies exactly the room it claimed.
///
/// Page coordinates run down from the top left, and a PDF's run up from the
/// bottom left. The description opens with a matrix that flips the page once,
/// so nothing below has to think about it.
#include "render/composer.hpp"
#include "layout/line.hpp"
#include "render/primitives/numeral.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace render {

    /// @brief Writes a number the way a page description wants it.
    ///
    /// Three decimals, no exponent, no trailing zeros. That is finer than
    /// a printer resolves and keeps a page of text to a few bytes per
    /// glyph, where a general formatter would spend seventeen digits on
    /// every one of them.
    ///
    /// A page of text writes several of these per glyph, so the common
    /// case is written out by hand rather than asked of a general
    /// formatter. It gives exactly the formatter's digits: a float times
    /// a thousand fits a double's mantissa with bits to spare, so the
    /// product is the float's own value in thousandths, and rounding it
    /// to the nearest whole, ties to even, is how the formatter rounds
    /// too. A value too large for that, or not a number at all, is
    /// handed to the formatter as it always was.
    ///
    /// @param text  Description to append to.
    /// @param value Number to write.
    /// @complexity O(1).
    static void number(std::string& text, const float value) {
        if (const double scaled = static_cast<double>(value) * 1000.0; std::fabs(scaled) < 1.0e15) {
            auto whole = static_cast<std::uint64_t>(std::fabs(std::nearbyint(scaled)));
            const bool negative = std::signbit(value) && whole != 0;
            std::uint64_t fraction = whole % 1000;
            whole /= 1000;

            // Written backwards from the last digit, so the trailing
            // zeros the formatter would have trimmed are never written.
            char digits[32];
            char* cursor = digits + sizeof(digits);
            if (fraction != 0) {
                int decimals = 3;
                for (; fraction % 10 == 0; fraction /= 10) --decimals;
                for (; decimals > 0; --decimals, fraction /= 10) {
                    *--cursor = static_cast<char>('0' + fraction % 10);
                }
                *--cursor = '.';
            }
            do {
                *--cursor = static_cast<char>('0' + whole % 10);
                whole /= 10;
            } while (whole != 0);

            // A value that rounds to nothing is written 0, whichever side
            // of it it fell: a cosine of a quarter turn is not -0.
            if (negative) *--cursor = '-';

            text.append(cursor, digits + sizeof(digits));
            return;
        }

        char buffer[32];
        const auto [stop, failure] =
            std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 3);
        if (failure != std::errc{}) {
            text += '0';
            return;
        }

        std::string_view digits(buffer, static_cast<std::size_t>(stop - buffer));
        if (digits.find('.') != std::string_view::npos) {
            while (digits.ends_with('0')) digits.remove_suffix(1);
            if (digits.ends_with('.')) digits.remove_suffix(1);
        }
        text += digits.empty() ? std::string_view("0") : digits;
    }

    /// @brief Writes a whole number, which may be negative.
    /// @param text  Description to append to.
    /// @param value Number to write.
    /// @complexity O(1).
    static void number(std::string& text, const int value) {
        char buffer[24];
        if (const auto [stop, failure] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            failure == std::errc{}) {
            text.append(buffer, stop);
        }
    }

    /// @brief Writes a whole number.
    /// @param text  Description to append to.
    /// @param value Number to write.
    /// @complexity O(1).
    static void number(std::string& text, const std::size_t value) {
        char buffer[24];
        if (const auto [stop, failure] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            failure == std::errc{}) {
            text.append(buffer, stop);
        }
    }

    Composer::Composer(
        memory::Arena& arena,
        memory::Arena& scratch,
        const typography::Shaper& shaper,
        layout::Typesetter& typesetter
    ) noexcept
        : typesetter(typesetter), document(arena, scratch, shaper, typesetter), shaper(shaper) {
        content.reserve(64 * 1024);
    }

    std::string_view Composer::draw(
        const memory::Slice<layout::Node*> nodes,
        const memory::Slice<const layout::Node*> notes,
        const float left,
        float top
    ) {
        content.clear();
        transcript.clear();
        links.emplace_back();

        // The page is flipped once, here, so every coordinate below runs down
        // from the top left the way the layout engine measures.
        content += "1 0 0 -1 0 ";
        number(content, document.configuration.height);
        content += " cm\n0 0 0 rg\n";
        filling = {};
        stroking = {};
        opacity = 1.0f;

        // A page colored with \pagecolor: painted over, edge to edge, before
        // anything is drawn on it.
        if (const layout::Node::Color& paint = document.furniture.background;
            paint != layout::Node::Color{1.0f, 1.0f, 1.0f}) {
            layout::Node sheet(layout::Node::Type::Rule);
            sheet.rule({.width = document.configuration.width, .height = document.configuration.height,
                        .color = paint});
            rule(&sheet, 0.0f, document.configuration.height);
        }

        // A page arrives as the children of a column, so it is walked the way
        // a column is: down to each node's reference point, draw, then on past
        // whatever hangs below it.
        // Each line of it a line of the transcript too.
        for (const layout::Node* item : nodes) {
            if (!item) continue;
            top += height(item);
            node(item, left, top);
            top += layout::Line::extent(item) - height(item);
            if (!transcript.empty() && transcript.back() != '\n') transcript += '\n';
        }

        // The footnotes, ending where the text block ends: LaTeX's
        // \\footnoterule -- two fifths of the column, three points above
        // the first -- and the notes under it, each a column of its own.
        if (!notes.empty()) {
            const layout::Document::Configuration& page = document.configuration;
            float below = 0.0f;
            for (const layout::Node* note : notes) below += layout::Line::extent(note) + 2.0f;
            float place = page.height - page.bottom - below;

            layout::Node bar(layout::Node::Type::Rule);
            bar.rule({.width = (page.width - page.left - page.right) * 0.4f, .height = 0.4f});
            rule(&bar, left, place - 2.6f);

            for (const layout::Node* note : notes) {
                place += height(note);
                node(note, left, place);
                place += layout::Line::extent(note) - height(note) + 2.0f;
                if (!transcript.empty() && transcript.back() != '\n') transcript += '\n';
            }
        }

        // The head and the foot, as the page's style says, and the page count
        // moved on. A page's style and numbering are whatever the last
        // Command::Page before its end said -- one marked local holds for its
        // own page alone -- so a `\\thispagestyle{empty}` on a title page
        // leaves the next one plain, and `\\pagenumbering{roman}` restarts
        // the count.
        {
        using Directive = layout::Node::Directive;

        // The page's own instructions, in the order they were written.
        Directive::Style own = style;
        for (const layout::Node* item : nodes) {
            if (!item || item->type != layout::Node::Type::Directive) continue;
            const Directive& order = item->directive();
            if (order.command != Directive::Command::Page) continue;

            if (order.numbering != Directive::Numbering::Keep) {
                numbering = order.numbering;
                capital = order.capital;
                count = 1;
            }
            if (order.index > 0) count = static_cast<decltype(count)>(order.index) - 1;
            if (order.style != Directive::Style::Keep) {
                own = order.style;
                if (!order.local) style = order.style;
            }
        }

        // LaTeX's article class: the head's baseline 25pt above the column,
        // the foot's 30pt below it.
        const layout::Document::Configuration& page = document.configuration;
        const layout::Document::Furniture& furniture = document.furniture;
        const float edge = page.left;
        const float span = page.width - page.left - page.right;
        const float head = page.top - 25.0f;
        const float foot = page.height - page.bottom + 30.0f;

        switch (own) {
            case Directive::Style::Plain:
                if (furniture.face) {
                    const typography::Font* fonts[] = {furniture.face};
                    float width = 0.0f;
                    for (const layout::Node* piece : shaper.shape(memory::Slice{fonts, 1uz}, folio(), {})) {
                        width += layout::Line::advance(piece);
                    }
                    numeral(furniture.face, edge + (span - width) * 0.5f, foot);
                }
                break;
            case Directive::Style::Headings:
                if (furniture.face) {
                    const typography::Font* fonts[] = {furniture.face};
                    float width = 0.0f;
                    for (const layout::Node* piece : shaper.shape(memory::Slice{fonts, 1uz}, folio(), {})) {
                        width += layout::Line::advance(piece);
                    }
                    numeral(furniture.face, edge + span - width, head);
                }
                break;
            case Directive::Style::Fancy: {
                for (int place = 0; place < 3; ++place) {
                    slot(furniture.head[static_cast<std::size_t>(place)], edge, span, place, head);
                    slot(furniture.foot[static_cast<std::size_t>(place)], edge, span, place, foot);
                }
                layout::Node bar(layout::Node::Type::Rule);
                if (furniture.rule > 0.0f) {
                    bar.rule({.width = span, .height = furniture.rule, .depth = 0.0f});
                    rule(&bar, edge, head + 4.0f);
                }
                if (furniture.line > 0.0f) {
                    bar.rule({.width = span, .height = furniture.line, .depth = 0.0f});
                    rule(&bar, edge, foot - 12.0f);
                }
                break;
            }
            default:
                break;
        }

        ++count;
        }

        // A link still open runs on over the page's end: what it covered here
        // is this page's, and it carries on on the next.
        for (Link& link : opened) {
            if (link.areas.empty()) continue;
            links.back().push_back(link);
            link.areas.clear();
        }

        flush();
        texts.push_back(transcript);
        return content;
    }

    void Composer::cover(const float left, const float top, const float right, const float bottom) {
        if (opened.empty()) return;
        std::vector<std::array<float, 4>>& areas = opened.back().areas;
        // On the line of the last area when the two overlap from top to
        // bottom; the start of another line's otherwise.
        if (!areas.empty() && top < areas.back()[3] && bottom > areas.back()[1]) {
            std::array<float, 4>& area = areas.back();
            area = {std::min(area[0], left), std::min(area[1], top), std::max(area[2], right), std::max(area[3], bottom)};
            return;
        }
        areas.push_back({left, top, right, bottom});
    }

    std::string Composer::folio() const {
        using Numbering = layout::Node::Directive::Numbering;
        switch (numbering) {
            case Numbering::Roman: return primitives::Numeral::roman(count, capital);
            case Numbering::Alphabetic: return primitives::Numeral::alphabetic(count, capital);
            default: return primitives::Numeral::arabic(count);
        }
    }

    float Composer::numeral(const typography::Font* font, float position, const float baseline) {
        if (!font) return 0.0f;
        const typography::Font* fonts[] = {font};
        const std::string text = folio();
        const memory::Slice<layout::Node*> shaped = shaper.shape(memory::Slice{fonts, 1uz}, text, {});
        const float start = position;
        for (const layout::Node* item : shaped) {
            node(item, position, baseline);
            position += layout::Line::advance(item);
        }
        return position - start;
    }

    void Composer::slot(const memory::Slice<layout::Node*> nodes, const float left, const float span, const int place,
                        const float baseline) {
        if (nodes.empty()) return;

        // How wide it is, the number included, so the centre and the right
        // can be placed before anything is drawn.
        float width = 0.0f;
        for (const layout::Node* item : nodes) {
            if (item && item->type == layout::Node::Type::Directive &&
                item->directive().command == layout::Node::Directive::Command::Number) {
                const typography::Font* fonts[] = {item->directive().font};
                if (fonts[0]) {
                    for (const layout::Node* piece : shaper.shape(memory::Slice{fonts, 1uz}, folio(), {})) {
                        width += layout::Line::advance(piece);
                    }
                }
                continue;
            }
            width += layout::Line::advance(item);
        }

        float position = left + (place == 1 ? (span - width) * 0.5f : place == 2 ? span - width : 0.0f);
        for (const layout::Node* item : nodes) {
            if (!item) continue;
            if (item->type == layout::Node::Type::Directive &&
                item->directive().command == layout::Node::Directive::Command::Number) {
                position += numeral(item->directive().font, position, baseline);
                continue;
            }
            node(item, position, baseline);
            position += layout::Line::advance(item);
        }
    }

    float Composer::height(const layout::Node* item) noexcept {
        if (!item) return 0.0f;
        switch (item->type) {
            case layout::Node::Type::Box:    return item->box().height;
            case layout::Node::Type::Glyph:  return item->glyph().height;
            case layout::Node::Type::Rule:   return item->rule().height;
            case layout::Node::Type::Bitmap: return item->bitmap().height;
            // A kern or a glue between two lines is pure space: it has a
            // reference point at its top and nothing standing above it.
            default:                        return 0.0f;
        }
    }

    void Composer::node(const layout::Node* item, const float position, const float baseline) {
        if (!item) return;

        switch (item->type) {
            case layout::Node::Type::Box:
                // The box's own displacement is applied here, once, so that
                // inside() only ever deals with its contents. An anchored
                // box ignores the pen entirely and places itself where it
                // was told to, from the page's own corner.
                if (item->box().anchored) {
                    const layout::Node::Point& corner = item->box().anchor;
                    inside(item, corner.x, corner.y);
                } else if (const layout::Node::Transform* map = item->box().transform) {
                    // Drawn through its map, in a graphics state of its own
                    // the page leaves again after: what the contents set --
                    // a color, an opacity -- is undone with it, and the colors
                    // this side remembers are the ones in force again.
                    flush();
                    const layout::Node::Color fill = filling;
                    const layout::Node::Color stroke = stroking;
                    const float faded = opacity;
                    content += "q ";
                    for (const float value : {map->a, map->b, map->c, map->d}) {
                        number(content, value == 0.0f ? 0.0f : value);   // no "-0" for a cosine of a right angle
                        content += ' ';
                    }
                    number(content, position + item->box().offset + map->x);
                    content += ' ';
                    number(content, baseline + item->box().shift);
                    content += " cm\n";
                    inside(item, 0.0f, 0.0f);
                    flush();
                    content += "Q\n";
                    filling = fill;
                    stroking = stroke;
                    opacity = faded;
                } else {
                    inside(item, position + item->box().offset, baseline + item->box().shift);
                }
                break;
            case layout::Node::Type::Glyph: {
                // One glyph onto the open run, opening a new run if need be.
                const layout::Node::Glyph& mark = item->glyph();
                if (mark.code == 0) return;

                // The open run's own font is nearly always the next glyph's too, and
                // its slot is already known. Otherwise the slot is the face's, not the
                // font's: every size of a family reads the same file, and the file is
                // what a resource names. The size is set per run, which is why a
                // second size costs nothing here.
                std::size_t slot = index;
                if (!running || mark.font != running) {
                    const typography::Face* face = mark.font ? mark.font->face() : nullptr;
                    if (!face || face->data().empty()) return;
                    slot = 0;
                    while (slot < faces.size() && !(faces[slot].font && faces[slot].font->face() == face)) ++slot;
                    if (slot == faces.size()) faces.push_back(Entry{.font = mark.font});
                }

                const float left = position + mark.x;
                const float height = baseline + mark.y;

                // A run holds one face at one size on one baseline in one color.
                // Anything else needs a run of its own, because those four are what a
                // run states up front and never restates.
                if (!running || mark.font != running || height != line || mark.color != filling) {
                    flush();
                    fill(mark.color);

                    running = mark.font;
                    index = slot;
                    pen = left;
                    line = height;

                    // The page was flipped so that everything measures downwards; the
                    // text matrix flips back, or the letters would be drawn upside
                    // down in the right places.
                    content += "BT /F";
                    number(content, index);
                    content += ' ';
                    number(content, running->size());
                    content += " Tf 1 0 0 -1 ";
                    number(content, pen);
                    content += ' ';
                    number(content, line);
                    content += " Tm [<";
                } else if (left != pen) {
                    // The pen is not where the last glyph left it: a kern, or a space
                    // that was stretched to set the line. The difference goes in as a
                    // displacement, which is subtracted from the pen and is therefore
                    // negated -- and it is applied to the model as well, so both sides
                    // stay in step to the last thousandth.
                    const float grain = running->size() / 1000.0f;
                    const auto step = static_cast<int>(std::lround((pen - left) / grain));

                    content += "> ";
                    number(content, step);
                    content += " <";
                    pen -= static_cast<float>(step) * grain;
                }

                // Identity encoding: two bytes of the number this glyph is embedded
                // under, straight through.
                //
                // The number is found by glyph rather than searched for: a face states
                // how many glyphs it holds, so the table is sized once and every lookup
                // after that is a load. Zero means "not drawn yet", which is why the
                // numbers handed out start at one -- and one is also where a font's
                // own numbering has to start, since zero is its missing-glyph slot. A
                // page of text draws a few dozen distinct glyphs from a face, so the
                // lists start with room for that rather than growing into it.
                Entry& entry = faces[slot];
                if (entry.numbers.empty()) {
                    entry.numbers.assign(entry.font->face()->count() + 1, 0);
                    entry.glyphs.reserve(128);
                    entry.points.reserve(128);
                    entry.letters.reserve(128);
                    entry.widths.reserve(128);
                }
                std::uint32_t code = 0;
                if (mark.code < entry.numbers.size()) {
                    if (entry.numbers[mark.code] == 0) {
                        entry.glyphs.push_back(mark.code);
                        entry.points.push_back(mark.point);
                        entry.letters.push_back(mark.letters);
                        entry.widths.push_back(width(*entry.font, mark.code));
                        entry.numbers[mark.code] = static_cast<std::uint32_t>(entry.glyphs.size());
                    }
                    code = entry.numbers[mark.code];
                }

                static constexpr char digits[] = "0123456789ABCDEF";
                const char written[] = {
                    digits[(code >> 12) & 0xF], digits[(code >> 8) & 0xF],
                    digits[(code >> 4) & 0xF], digits[code & 0xF]
                };
                content.append(written, sizeof(written));
                if (!opened.empty()) cover(left, height - mark.height, left + mark.width, height + mark.depth);

                // What the glyph stands for, onto the page's transcript: a
                // ligature's letters, or its own character in UTF-8.
                if (!mark.letters.empty()) {
                    transcript += mark.letters;
                } else if (const std::uint32_t point = mark.point; point < 0x80) {
                    transcript += static_cast<char>(point);
                } else if (point < 0x800) {
                    transcript += static_cast<char>(0xC0 | (point >> 6));
                    transcript += static_cast<char>(0x80 | (point & 0x3F));
                } else if (point < 0x10000) {
                    transcript += static_cast<char>(0xE0 | (point >> 12));
                    transcript += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
                    transcript += static_cast<char>(0x80 | (point & 0x3F));
                } else {
                    transcript += static_cast<char>(0xF0 | (point >> 18));
                    transcript += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
                    transcript += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
                    transcript += static_cast<char>(0x80 | (point & 0x3F));
                }

                // The reader advances the pen by the width it was given for this
                // glyph, which is the rounded one, so the model has to advance by
                // exactly that and not by the width the font would report. A glyph
                // with a number had its width taken when it got one.
                const float advance = code != 0 ? entry.widths[code - 1] : width(*entry.font, mark.code);
                pen += advance * running->size() / 1000.0f;
                break;
            }
            case layout::Node::Type::Rule:
                rule(item, position, baseline);
                break;
            case layout::Node::Type::Path: {
                // A stroked line between two points.
                // A stroke is painted straight onto the page too, like a rule.
                flush();

                const layout::Node::Path& segment = item->path();

                // A region: its corners joined, closed and filled, in its
                // color as a rule is.
                if (!segment.area.empty()) {
                    fill(segment.color);
                    for (std::size_t corner = 0; corner < segment.area.size(); ++corner) {
                        number(content, position + segment.area[corner].x);
                        content += ' ';
                        number(content, baseline + segment.area[corner].y);
                        content += corner == 0 ? " m\n" : " l\n";
                    }
                    content += "h f\n";
                    break;
                }
                if (segment.width <= 0.0f) return;

                // The stroke color, when it differs from the one already set.
                translucent(segment.color.alpha);
                if (segment.color != stroking) {
                    stroking = segment.color;
                    number(content, segment.color.r);
                    content += ' ';
                    number(content, segment.color.g);
                    content += ' ';
                    number(content, segment.color.b);
                    content += " RG\n";
                }

                number(content, segment.width);
                content += " w\n";
                content += segment.dashed ? "[3 3] 0 d\n" : "[] 0 d\n";

                number(content, position + segment.start.x);
                content += ' ';
                number(content, baseline + segment.start.y);
                content += " m\n";
                number(content, position + segment.end.x);
                content += ' ';
                number(content, baseline + segment.end.y);
                content += " l S\n";
                break;
            }
            case layout::Node::Type::Bitmap: {
                // An image, scaled to its drawn size.
                // An image is painted straight onto the page too, like a rule.
                flush();

                const layout::Node::Bitmap& bitmap = item->bitmap();
                if (bitmap.width <= 0.0f || bitmap.height <= 0.0f) return;

                // A page of another PDF: its form, clipped to the drawn box
                // and scaled so the window onto its page fills it. The form's
                // y runs up and the page's down, so its scale is negated and
                // its window's bottom-left put at the box's bottom-left.
                if (const graphics::Drawing* drawn = bitmap.drawing) {
                    std::size_t slot = 0;
                    while (slot < drawings.size() && drawings[slot] != drawn) ++slot;
                    if (slot == drawings.size()) drawings.push_back(drawn);

                    const std::array<float, 4>& window = bitmap.window;
                    const float across = bitmap.width / std::max(window[2] - window[0], 0.001f);
                    const float down = bitmap.height / std::max(window[3] - window[1], 0.001f);
                    content += "q\n";
                    number(content, position);
                    content += ' ';
                    number(content, baseline - bitmap.height);
                    content += ' ';
                    number(content, bitmap.width);
                    content += ' ';
                    number(content, bitmap.height);
                    content += " re W n\n";
                    number(content, across);
                    content += " 0 0 ";
                    number(content, -down);
                    content += ' ';
                    number(content, position - window[0] * across);
                    content += ' ';
                    number(content, baseline + window[1] * down);
                    content += " cm\n/Fm";
                    number(content, slot);
                    content += " Do\nQ\n";
                    break;
                }
                if (!bitmap.source) return;

                // The image's resource index, recorded on first sight. A document
                // draws few enough of them that a linear search costs nothing a hash
                // table would not spend just as much building.
                std::size_t slot = 0;
                while (slot < pictures.size() && pictures[slot] != bitmap.source) ++slot;
                if (slot == pictures.size()) pictures.push_back(bitmap.source);

                // The image XObject's unit square runs bottom to top; the content
                // stream around it runs top to bottom, page-flipped once already.
                // Negating the height and translating to the baseline is what turns
                // one into the other without a second flip of everything drawn
                // after it.
                content += "q\n";
                number(content, bitmap.width);
                content += " 0 0 ";
                number(content, -bitmap.height);
                content += ' ';
                number(content, position);
                content += ' ';
                number(content, baseline);
                content += " cm\n/Im";
                number(content, slot);
                content += " Do\nQ\n";
                break;
            }
            case layout::Node::Type::Directive:
                // A page number set in the text: known now, as its page is
                // drawn -- and an anchor's page, for the same reason.
                if (item->directive().command == layout::Node::Directive::Command::Number) {
                    numeral(item->directive().font, position, baseline);
                } else if (item->directive().command == layout::Node::Directive::Command::Anchor) {
                    const std::size_t slot = item->directive().index;
                    if (slot >= anchors.size()) anchors.resize(slot + 1);
                    anchors[slot] = folio();
                    if (slot >= places.size()) places.resize(slot + 1);
                    places[slot] = {.page = texts.size(), .down = baseline};
                } else if (item->directive().command == layout::Node::Directive::Command::Link) {
                    opened.push_back({.target = item->directive().target, .anchor = item->directive().index,
                                     .border = item->directive().border});
                } else if (item->directive().command == layout::Node::Directive::Command::Unlink && !opened.empty()) {
                    if (!opened.back().areas.empty()) links.back().push_back(std::move(opened.back()));
                    opened.pop_back();
                } else if (const layout::Node* aside = item->directive().note;
                           aside && item->directive().command == layout::Node::Directive::Command::Aside) {
                    // A margin note: in the right margin, clear of the column
                    // by \marginparsep, its first line level with the line it
                    // was written in.
                    const layout::Document::Configuration& page = document.configuration;
                    const layout::Node::Box& shape = aside->box();
                    const layout::Node* first = shape.list.empty() ? nullptr : shape.list[0];
                    const float top = first && first->type == layout::Node::Type::Box ? first->box().height : 0.0f;
                    node(aside, page.width - page.right + item->directive().width, baseline - top);
                }
                break;
            default:
                break;   // glue, kerns and penalties move the pen but draw nothing
        }
    }

    void Composer::inside(const layout::Node* item, float position, float baseline) {
        const layout::Node::Box& shape = item->box();

        // A canvas: every child already carries its own offset from this
        // box's own reference point, so none of them advances the pen the
        // way an ordinary line or column otherwise would.
        if (shape.absolute) {
            for (const layout::Node* child : shape.list) node(child, position, baseline);
            return;
        }

        if (shape.alignment == layout::Node::Alignment::Horizontal) {
            for (const layout::Node* child : shape.list) {
                if (!child) continue;
                node(child, position, baseline);

                // Glue is the only thing whose drawn width differs from its
                // natural one, because the line it sits in was set to a width.
                // Only glue of the order that won takes any of the difference.
                if (child->type == layout::Node::Type::Glue) {
                    const layout::Node::Glue& glue = child->glue();
                    float span = glue.width;
                    if (shape.sign == layout::Node::Sign::Stretching && glue.expand == shape.order) {
                        span += glue.stretch * shape.ratio;
                    } else if (shape.sign == layout::Node::Sign::Shrinking && glue.limit == shape.order) {
                        span -= glue.shrink * shape.ratio;
                    }

                    // Leaders: a rule drawn the glue's whole width, or a box
                    // drawn again and again on a grid from the page's edge,
                    // so the dots of one line stand under the dots of the
                    // next -- TeX's aligned \leaders.
                    if (const layout::Node* leader = glue.leader; leader && span > 0.0f) {
                        if (leader->type == layout::Node::Type::Rule) {
                            layout::Node bar(layout::Node::Type::Rule);
                            layout::Node::Rule stretched = leader->rule();
                            stretched.width = span;
                            bar.rule(stretched);
                            rule(&bar, position, baseline);
                        } else if (const float step = layout::Line::advance(leader); step > 0.0f) {
                            for (float at = std::ceil(position / step) * step; at + step <= position + span + 0.01f;
                                 at += step) {
                                node(leader, at, baseline);
                            }
                        }
                    }
                    position += span;

                    // A word space is a space in the transcript, once.
                    if (!glue.leader && span > 0.0f && !transcript.empty() && transcript.back() != ' ' &&
                        transcript.back() != '\n') {
                        transcript += ' ';
                    }
                    continue;
                }
                position += layout::Line::advance(child);
            }
            return;
        }

        // A column: down to each child's reference point, draw it, then on
        // past whatever hangs below it.
        for (const layout::Node* child : shape.list) {
            if (!child) continue;

            baseline += height(child);
            node(child, position, baseline);
            baseline += layout::Line::extent(child) - height(child);
            if (child->type == layout::Node::Type::Box && !transcript.empty() && transcript.back() != '\n') {
                transcript += '\n';
            }

            if (child->type == layout::Node::Type::Glue) {
                const layout::Node::Glue& glue = child->glue();
                if (shape.sign == layout::Node::Sign::Stretching && glue.expand == shape.order) {
                    baseline += glue.stretch * shape.ratio;
                } else if (shape.sign == layout::Node::Sign::Shrinking && glue.limit == shape.order) {
                    baseline -= glue.shrink * shape.ratio;
                }
            }
        }
    }

    float Composer::width(const typography::Font& font, const std::uint32_t glyph) noexcept {
        if (font.size() <= 0.0f) return 0.0f;
        return static_cast<float>(std::lround(font.advance(glyph) / font.size() * 1000.0f));
    }

    void Composer::flush() {
        if (!running) return;
        content += ">] TJ ET\n";
        running = nullptr;
    }

    void Composer::rule(const layout::Node* item, const float position, const float baseline) {
        // Out of order with the open run otherwise: a rectangle is painted
        // straight onto the page, so anything still open goes first.
        flush();

        const layout::Node::Rule& bar = item->rule();
        if (bar.width <= 0.0f || bar.height + bar.depth <= 0.0f) return;

        fill(bar.color);

        number(content, position);
        content += ' ';
        number(content, baseline - bar.height);
        content += ' ';
        number(content, bar.width);
        content += ' ';
        number(content, bar.height + bar.depth);
        content += " re f\n";
    }

    void Composer::fill(const layout::Node::Color& value) {
        translucent(value.alpha);
        if (value == filling) return;
        filling = value;

        number(content, value.r);
        content += ' ';
        number(content, value.g);
        content += ' ';
        number(content, value.b);
        content += " rg\n";
    }

    void Composer::translucent(const float value) {
        if (value == opacity) return;
        opacity = value;

        // Its resource index, recorded on first sight.
        std::size_t slot = 0;
        while (slot < opacities.size() && opacities[slot] != value) ++slot;
        if (slot == opacities.size()) opacities.push_back(value);

        content += "/GS";
        number(content, slot);
        content += " gs\n";
    }

}
