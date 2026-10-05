/// @file
/// @brief Pager implementation: a column of blocks cut into pages.
///
/// One pass, one running height. A column is closed the moment the next block
/// -- with the footnotes it calls -- would not fit, and the next opened at
/// that block, so the block that did not fit starts the next column rather
/// than being lost between the two, and a footnote always lands on the page
/// that calls it.
///
/// A float's blocks are measured as one and set whole, where LaTeX's output
/// routine would set them: in the text, at the head or the foot of a column,
/// or on a page of floats alone -- `\\@addtocurcol` where it is written,
/// `\\@startcolumn` as each column opens, `\\@doclearpage` at a forced break.
///
/// A page of one column is closed with its column. A page of several is
/// filled a column at a time and closed with its last, the columns side by
/// side. How many there are may change part way down a page -- a title across
/// the top of a two-column paper, a multicols block in a one-column one -- so
/// a page is a stack of regions, each as many columns wide as the
/// Command::Columns before it said, and a region that ends part way down a
/// page is balanced: its columns as nearly level as its blocks allow, as the
/// multicol package sets its last.
#include "layout/pager.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace render::layout {

    /// @brief Every footnote a block calls, however deep inside it the call is.
    /// @param node  The block.
    /// @param notes Where the notes go, in order; appended to.
    /// @complexity O(n) in the block's nodes.
    static void collect(const Node* node, std::vector<const Node*>& notes) {
        if (!node) return;
        if (node->type == Node::Type::Directive) {
            const Node::Directive& order = node->directive();
            if (order.command == Node::Directive::Command::Note && order.note) notes.push_back(order.note);
            return;
        }
        if (node->type != Node::Type::Box) return;
        for (const Node* child : node->box().list) collect(child, notes);
    }

    Pager::Pager(memory::Arena& arena) noexcept : arena(arena) {}

    memory::Slice<Pager::Page> Pager::paginate(const Node* head, const Context& context) const {
        if (!head || head->type != Node::Type::Box) return {};

        const memory::Slice<Node*>& column = head->box().list;
        const std::size_t total = column.size();
        if (total == 0 || context.height <= 0.0f) return {};

        using Command = Node::Directive::Command;
        using Directive = Node::Directive;
        const Placement& rules = context.placement;
        const auto space = [](const Node* block) {
            return block->type == Node::Type::Glue || block->type == Node::Type::Kern;
        };
        const auto kern = [this](const float amount) {
            auto* made = arena.compose<Node>(Node::Type::Kern);
            made->kern({.width = amount});
            return made;
        };

        // LaTeX's \@fpsep: between two floats on a page of floats alone,
        // which share out what the page leaves -- a part above them, two
        // between each, and a part below.
        constexpr float leaf = 8.0f;

        /// @brief A float: what it sets, and where it may go.
        struct Float {
            std::vector<Node*> blocks{};           ///< Its blocks, its own space above and below left off.
            float span{0.0f};                      ///< How tall they stand.
            std::vector<const Node*> notes{};      ///< The footnotes it calls.
            std::uint8_t place{0};                 ///< Directive::Place bits.
            std::size_t kind{0};                   ///< Its kind, which keeps its order among its kind.
            bool across{false};                    ///< Across the columns of a page of several.
            std::size_t written{0};                ///< Where it came among all the floats.
        };

        // Pages grow as they are made; a float that waits can add one.
        std::vector<Page> pages;

        // The page being made: what is set on it above the columns being
        // filled -- a region already closed, a title or floats across the
        // columns under it -- and how tall that stands.
        std::vector<Node*> above;
        float used = 0.0f;

        // The columns being filled: how many stand side by side, those of
        // them already full and the tallest of those, which a footnote added
        // later must still leave room for, and the one being filled now.
        std::size_t count = std::max<std::size_t>(context.columns, 1);
        std::vector<std::vector<Node*>> full;
        float tallest = 0.0f;
        std::vector<Node*> filling;
        float height = 0.0f;

        // The footnotes the page being made calls, and the room they take
        // at its foot -- the notes themselves, the gap between them, and the
        // space and rule above the first.
        std::vector<const Node*> notes;
        float reserved = 0.0f;
        const auto room = [&context](const std::vector<const Node*>& added, const bool first) {
            float taken = 0.0f;
            for (const Node* note : added) taken += Line::extent(note) + context.between;
            return taken + (first && !added.empty() ? context.separation : 0.0f);
        };

        // The floats at the head and the foot of the column being filled;
        // the room each area takes, their separating space included; what
        // is left of the share of the column each may take; how many floats
        // the column holds, wherever they stand; and the kinds already set
        // in its text or at its foot, none of whose floats may then go
        // above them.
        std::vector<Float> heads;
        std::vector<Float> feet;
        float crown = 0.0f;
        float sole = 0.0f;
        float headroom = 0.0f;
        float footroom = 0.0f;
        std::size_t within = 0;
        std::vector<std::size_t> below;

        // The floats waiting: for a column, and for a page's head across its
        // columns.
        std::deque<Float> waiting;
        std::deque<Float> spanning;
        std::size_t floats = 0;

        // Whether a float of this kind written before this one waits in a
        // list, which keeps this one waiting too: LaTeX sets a kind's floats
        // in the order written, across the columns or in one.
        const auto before = [](const std::deque<Float>& list, const Float& item) {
            return std::ranges::any_of(list, [&item](const Float& other) {
                return other.kind == item.kind && other.written < item.written;
            });
        };

        // A column opens: the shares of it its head and foot may give floats.
        const auto open = [&] {
            const float tall = context.height - used;
            headroom = rules.top * tall;
            footroom = rules.bottom * tall;
            within = 0;
            below.clear();
        };
        open();

        // A region's blocks in as few of its columns as they fill, each no
        // taller than it must be for every block to fit: from an even share
        // of their height upward, each time by the least that keeps one more
        // block in a column, until they fit. Space at the head of a column
        // after the first is dropped, as at a page's.
        const auto balance = [&](const std::vector<Node*>& blocks) {
            float tall = 0.0f;
            for (const Node* block : blocks) tall += Line::extent(block);
            tall /= static_cast<float>(count);

            std::vector<std::vector<Node*>> set;
            while (true) {
                set.assign(1, {});
                float filled = 0.0f;
                float least = std::numeric_limits<float>::infinity();
                for (Node* block : blocks) {
                    const float span = Line::extent(block);
                    if (!set.back().empty() && filled + span > tall + 0.01f) {
                        least = std::min(least, filled + span - tall);
                        set.emplace_back();
                        filled = 0.0f;
                    }
                    if (set.back().empty() && set.size() > 1 && space(block)) continue;
                    set.back().push_back(block);
                    filled += span;
                }
                if (set.size() <= count || !std::isfinite(least)) return set;
                tall += least;
            }
        };

        // The column's floats set round its text: those at its head
        // \\floatsep apart and \\textfloatsep above the text, those at its
        // foot the same below it -- and, in a column that is done, at its
        // very foot, whatever the text leaves: LaTeX's \\@textbottom, which
        // stretches between them.
        const auto assemble = [&](const bool done) {
            if (heads.empty() && feet.empty()) return;
            std::vector<Node*> set;
            for (std::size_t index = 0; index < heads.size(); ++index) {
                if (index > 0) set.push_back(kern(rules.apart));
                set.insert(set.end(), heads[index].blocks.begin(), heads[index].blocks.end());
            }
            if (!heads.empty() && !filling.empty()) set.push_back(kern(rules.clearance));
            set.insert(set.end(), filling.begin(), filling.end());
            if (!feet.empty() && !set.empty()) {
                const float free = done ? std::max(context.height - used - reserved - crown - height - sole, 0.0f) : 0.0f;
                set.push_back(kern(rules.clearance + free));
            }
            for (std::size_t index = 0; index < feet.size(); ++index) {
                if (index > 0) set.push_back(kern(rules.apart));
                set.insert(set.end(), feet[index].blocks.begin(), feet[index].blocks.end());
            }
            filling = std::move(set);
            height = 0.0f;
            for (const Node* block : filling) height += Line::extent(block);
            heads.clear();
            feet.clear();
            crown = 0.0f;
            sole = 0.0f;
        };

        // The columns being filled, set down on the page -- level, when the
        // region ends part way down it; as they stand, when the page is
        // full -- side by side across the text block, each as wide as its
        // share and `\\columnsep` from the next. A page's style or numbering
        // changed inside a column is lifted onto the page as well, where the
        // composer looks for it.
        const auto region = [&](const bool level) {
            assemble(!level);
            if (count == 1) {
                above.insert(above.end(), filling.begin(), filling.end());
                used += height;
            } else {
                std::vector<std::vector<Node*>> set = std::move(full);
                set.push_back(std::move(filling));
                if (level) {
                    std::vector<Node*> blocks;
                    for (const std::vector<Node*>& blocked : set) blocks.insert(blocks.end(), blocked.begin(), blocked.end());
                    set = balance(blocks);
                }

                if (std::ranges::any_of(set, [](const std::vector<Node*>& blocked) { return !blocked.empty(); })) {
                    const float share = (context.width - context.gap * static_cast<float>(count - 1)) /
                                        static_cast<float>(count);
                    const memory::Slice<Node*> row = arena.allocate<Node*>(set.size() * 2);
                    std::size_t placed = 0;
                    for (std::vector<Node*>& blocked : set) {
                        if (placed > 0) {
                            auto* gap = arena.compose<Node>(Node::Type::Kern);
                            gap->kern({.width = context.gap});
                            row[placed++] = gap;
                        }
                        Node* box = Line::vertical(arena, memory::Slice{blocked.data(), blocked.size()}, 0.0f);
                        Node::Box shape = box->box();
                        shape.width = share;
                        box->box(shape);
                        row[placed++] = box;
                        for (Node* block : blocked) {
                            if (block->type == Node::Type::Directive && block->directive().command == Command::Page) {
                                above.push_back(block);
                            }
                        }
                    }
                    Node* across = Line::horizontal(arena, memory::Slice{row.data, placed}, 0.0f);
                    above.push_back(across);
                    used += Line::extent(across);
                }
            }
            full.clear();
            filling.clear();
            height = 0.0f;
            tallest = 0.0f;
        };

        // The page being made, set down as it stands, and the next begun.
        const auto seal = [&] {
            // A page of nothing but instructions and space -- a numbering
            // changed, an anchor, between two breaks -- is no page, as TeX
            // ships none: its instructions go to the next.
            if (std::ranges::none_of(above, [&](const Node* block) {
                    return block->type != Node::Type::Directive && !space(block);
                })) {
                std::erase_if(above, space);
                used = 0.0f;
                return;
            }
            if (!above.empty()) {
                // Glue of an infinite order -- \vfill, \vspace*{\fill} -- takes
                // the room the page leaves, shared by weight among the glue of
                // the highest order on it, as TeX sets a page's glue.
                Node::Order order = Node::Order::Normal;
                float weight = 0.0f;
                for (const Node* block : above) {
                    if (block->type != Node::Type::Glue || block->glue().stretch <= 0.0f) continue;
                    if (block->glue().expand > order) {
                        order = block->glue().expand;
                        weight = 0.0f;
                    }
                    if (block->glue().expand == order) weight += block->glue().stretch;
                }
                if (order != Node::Order::Normal && weight > 0.0f) {
                    const float room = std::max(context.height - used - reserved, 0.0f);
                    for (Node*& block : above) {
                        if (block->type != Node::Type::Glue || block->glue().expand != order) continue;
                        Node::Glue grown = block->glue();
                        grown.width += room * grown.stretch / weight;
                        block = arena.compose<Node>(Node::Type::Glue);
                        block->glue(grown);
                    }
                    used += room;
                }

                const memory::Slice<Node*> nodes = arena.allocate<Node*>(above.size());
                std::ranges::copy(above, nodes.begin());
                const memory::Slice<const Node*> called = arena.allocate<const Node*>(notes.size());
                std::ranges::copy(notes, called.begin());

                // How short the page fell, cubed as line badness is, so a page
                // left nearly empty is reported as far worse than a full one.
                const float slack = std::max(context.height - used - reserved, 0.0f) / context.height;
                pages.push_back(Page{
                    .nodes = nodes,
                    .notes = called,
                    .height = used,
                    .index = static_cast<std::int32_t>(pages.size()),
                    .badness = static_cast<std::int32_t>(std::min(10000.0f, 100.0f * slack * slack * slack))
                });
                Logger::log(Logger::Type::Layout, Logger::Level::Debug,
                            "Page {} holds {} blocks and {} notes, {} of {} points", pages.size() - 1,
                            above.size(), notes.size(), used + reserved, context.height);
            }
            above.clear();
            used = 0.0f;
            notes.clear();
            reserved = 0.0f;
        };

        // A float's notes, called on the page it lands on.
        const auto call = [&](const Float& item) {
            reserved += room(item.notes, notes.empty());
            notes.insert(notes.end(), item.notes.begin(), item.notes.end());
        };

        // Floats alone, as a page of floats sets them: the room they leave
        // shared out a part above them, two between each two with \\@fpsep
        // besides, and a part below.
        const auto alone = [&](const std::vector<Float>& set, const float tall) {
            float taken = 0.0f;
            for (const Float& item : set) taken += item.span;
            taken += leaf * static_cast<float>(set.size() - 1);
            const float part = std::max(tall - taken, 0.0f) / (2.0f * static_cast<float>(set.size()));
            std::vector<Node*> nodes;
            nodes.push_back(kern(part));
            for (std::size_t index = 0; index < set.size(); ++index) {
                if (index > 0) nodes.push_back(kern(leaf + 2.0f * part));
                nodes.insert(nodes.end(), set[index].blocks.begin(), set[index].blocks.end());
            }
            return nodes;
        };

        // Which of the floats waiting in a list fit on a page of floats of
        // this height -- in order, at least the first when forced, a kind
        // whose float does not fit, or waits earlier in the other list,
        // holding back its later ones -- and how tall they stand together.
        const auto gather = [&](const std::deque<Float>& list, const std::deque<Float>& other, const float tall,
                                const bool forced) {
            std::vector<std::size_t> taken;
            std::vector<std::size_t> held;
            float filled = 0.0f;
            for (std::size_t index = 0; index < list.size(); ++index) {
                const Float& item = list[index];
                if (std::ranges::contains(held, item.kind) || before(other, item)) continue;
                const float added = item.span + (taken.empty() ? 0.0f : leaf);
                if ((forced || (item.place & Directive::Alone)) && filled + added <= tall) {
                    taken.push_back(index);
                    filled += added;
                } else if (forced && taken.empty()) {
                    // Taller than a page: set alone all the same, over its foot.
                    taken.push_back(index);
                    filled += added;
                } else {
                    held.push_back(item.kind);
                }
            }
            return std::pair{taken, filled};
        };

        // The floats across the columns that waited for this page: a page of
        // them alone when they fill enough of one or the page is forced;
        // otherwise as many at its head as \\dbltopfraction and dbltopnumber
        // allow, \\textfloatsep above the columns.
        const auto spread = [&](const bool forced) {
            while (!spanning.empty()) {
                const auto [taken, filled] = gather(spanning, waiting, context.height - used, forced);
                if (taken.empty() || (!forced && filled < rules.sheet * context.height)) break;
                if (!above.empty()) seal();
                std::vector<Float> set;
                for (const std::size_t index : taken) set.push_back(std::move(spanning[index]));
                for (auto index = taken.rbegin(); index != taken.rend(); ++index) spanning.erase(spanning.begin() + *index);
                for (Node* node : alone(set, context.height)) above.push_back(node);
                for (const Float& item : set) call(item);
                used = context.height;
                seal();
            }

            std::size_t stacked = 0;
            float taken = 0.0f;
            while (!spanning.empty()) {
                const Float& item = spanning.front();
                const bool insisted = item.place & Directive::Force;
                if (!(item.place & Directive::Top) || before(waiting, item)) break;
                if (!insisted && (stacked >= rules.spans || taken + item.span > rules.spanning * context.height)) break;
                const float added = item.span + (stacked > 0 ? rules.apart : 0.0f);
                if (used + added + rules.clearance > context.height) break;
                if (stacked > 0) above.push_back(kern(rules.apart));
                above.insert(above.end(), item.blocks.begin(), item.blocks.end());
                used += added;
                taken += item.span;
                call(item);
                ++stacked;
                spanning.pop_front();
            }
            if (stacked > 0) {
                above.push_back(kern(rules.clearance));
                used += rules.clearance;
            }
        };

        // The page being made, closed with whatever columns it has open; the
        // next opens with its floats across the columns.
        const auto close = [&] {
            region(false);
            seal();
            if (count > 1) spread(false);
            open();
        };

        // What the column being filled has left, once the notes a block
        // would call are counted.
        const auto left = [&](const std::vector<const Node*>& called) {
            return context.height - used - reserved - room(called, notes.empty()) - crown - sole;
        };
        // The text the column must keep beside its floats: \\textfraction of
        // it, or what it holds already when that is more.
        const auto text = [&](const bool forced) {
            return std::max(height, forced ? 0.0f : rules.text * (context.height - used));
        };
        // Whether a float of this kind waits already, which keeps the next
        // waiting too.
        const auto waits = [&](const std::size_t kind) {
            const auto same = [kind](const Float& other) { return other.kind == kind; };
            return std::ranges::any_of(waiting, same) || std::ranges::any_of(spanning, same);
        };

        // A float at the head of the column being filled, if it may stand
        // there: `t` allowed, fewer than topnumber there and totalnumber in
        // the column, within what is left of \\topfraction, nothing of its
        // kind set below it, and the column's text still fitting beside it.
        const auto crowned = [&](Float& item) {
            const bool forced = item.place & Directive::Force;
            if (!(item.place & Directive::Top) || std::ranges::contains(below, item.kind)) return false;
            if (!forced && (heads.size() >= rules.heads || within >= rules.most || item.span > headroom)) return false;
            const float added = item.span + (heads.empty() ? rules.clearance : rules.apart);
            if (text(forced) + added > left(item.notes)) return false;
            headroom -= item.span + rules.apart;
            crown += added;
            ++within;
            call(item);
            heads.push_back(std::move(item));
            return true;
        };

        // A float at the foot of the column, on the same terms with `b`,
        // bottomnumber and \\bottomfraction.
        const auto footed = [&](Float& item) {
            const bool forced = item.place & Directive::Force;
            if (!(item.place & Directive::Bottom)) return false;
            if (!forced && (feet.size() >= rules.feet || within >= rules.most || item.span > footroom)) return false;
            const float added = item.span + (feet.empty() ? rules.clearance : rules.apart);
            if (text(forced) + added > left(item.notes)) return false;
            footroom -= item.span + rules.apart;
            sole += added;
            ++within;
            below.push_back(item.kind);
            call(item);
            feet.push_back(std::move(item));
            return true;
        };

        // A float in the text where it is written, \\intextsep above and
        // below it, on the same terms with `h`.
        const auto amid = [&](Float& item) {
            const bool forced = item.place & Directive::Force;
            if (!(item.place & Directive::Here) || (!forced && within >= rules.most)) return false;
            if (text(forced) + item.span + 2.0f * rules.amid > left(item.notes)) return false;
            filling.push_back(kern(rules.amid));
            filling.insert(filling.end(), item.blocks.begin(), item.blocks.end());
            filling.push_back(kern(rules.amid));
            height += item.span + 2.0f * rules.amid;
            ++within;
            below.push_back(item.kind);
            call(item);
            return true;
        };

        // On to the next column: beside this one while the page has room
        // for another, or at the head of the next page -- and the column
        // opens with the floats that waited for it.
        std::function<void(bool)> turn;
        const std::function<void(bool)> settle = [&](const bool forced) {
            // A column of floats alone, when those waiting fill enough of it
            // -- \\floatpagefraction -- or the break is forced.
            if (!waiting.empty() && filling.empty() && heads.empty() && feet.empty()) {
                const float tall = context.height - used - reserved;
                const auto [taken, filled] = gather(waiting, spanning, tall, forced);
                if (!taken.empty() && (forced || filled >= rules.page * tall)) {
                    std::vector<Float> set;
                    for (const std::size_t index : taken) set.push_back(std::move(waiting[index]));
                    for (auto index = taken.rbegin(); index != taken.rend(); ++index) waiting.erase(waiting.begin() + *index);
                    filling = alone(set, tall);
                    height = tall;
                    for (const Float& item : set) call(item);
                    turn(forced);
                    return;
                }
            }
            if (forced) return;

            // Otherwise each at the column's head or foot, as far as they may
            // go, in the order written; one that cannot holds its kind back.
            std::vector<std::size_t> held;
            for (auto item = waiting.begin(); item != waiting.end();) {
                if (!std::ranges::contains(held, item->kind) && !before(spanning, *item) &&
                    (crowned(*item) || footed(*item))) {
                    item = waiting.erase(item);
                } else {
                    held.push_back(item->kind);
                    ++item;
                }
            }
        };
        turn = [&](const bool forced) {
            if (full.size() + 1 < count) {
                assemble(true);
                tallest = std::max(tallest, height);
                full.push_back(std::move(filling));
                filling.clear();
                height = 0.0f;
                open();
            } else {
                close();
            }
            settle(forced);
        };

        // Every float still waiting, on pages of its own: what LaTeX's
        // \\clearpage and the document's end do.
        const auto flush = [&] {
            close();
            while (!waiting.empty() || !spanning.empty()) {
                if (!spanning.empty()) spread(true);
                if (!waiting.empty()) settle(true);
                close();
            }
        };

        // Whether a block may start the column being filled: space may not,
        // except the first column's on the first page, or under something
        // already set higher on the page.
        const auto dropped = [&](const Node* block) {
            return filling.empty() && space(block) && (!full.empty() || (above.empty() && !pages.empty()));
        };

        // A long table being set: the rows each column it runs on into opens
        // with, the rows each column it breaks out of closes with, and how
        // many of its rows the column being filled holds.
        Node* header = nullptr;
        Node* footer = nullptr;
        std::size_t rows = 0;

        std::vector<const Node*> added;
        for (std::size_t index = 0; index < total; ++index) {
            Node* block = column[index];
            if (!block) continue;

            if (block->type == Node::Type::Directive) {
                const Node::Directive& order = block->directive();

                // A long table's rows begin, or end.
                if (order.command == Command::Repeat) {
                    header = order.head;
                    footer = order.foot;
                    rows = 0;
                    continue;
                }

                // The columns change: those open are set down level where
                // they end, and the blocks after go into as many as asked.
                if (order.command == Command::Columns) {
                    if (const std::size_t wanted = std::max<std::size_t>(order.index, 1); wanted != count) {
                        region(true);
                        count = wanted;
                        open();
                    }
                    continue;
                }

                // placeins' barrier: no float passes it.
                if (order.command == Command::Barrier) {
                    if (!waiting.empty() || !spanning.empty()) flush();
                    continue;
                }

                // A float: its blocks to its Release, measured as one. A mark
                // that opens or closes one is a directive, and nothing else
                // is. The space its opening and closing left above and below
                // it is taken off: where it lands decides the space round it.
                if (order.command == Command::Hold) {
                    std::size_t last = total - 1;
                    std::size_t depth = 0;
                    for (std::size_t scan = index + 1; scan < total; ++scan) {
                        const Node* mark = column[scan];
                        if (!mark || mark->type != Node::Type::Directive) continue;
                        if (mark->directive().command == Command::Hold) ++depth;
                        if (mark->directive().command == Command::Release) {
                            if (depth == 0) {
                                last = scan;
                                break;
                            }
                            --depth;
                        }
                    }
                    std::size_t first = last + 1;
                    std::size_t final = index;
                    for (std::size_t scan = index; scan <= last; ++scan) {
                        const Node* inner = column[scan];
                        if (inner && !space(inner) && inner->type != Node::Type::Directive) {
                            first = std::min(first, scan);
                            final = scan;
                        }
                    }
                    Float item{.place = order.place, .kind = order.index, .across = order.across, .written = floats++};
                    if (!(item.place & (Directive::Here | Directive::Top | Directive::Bottom | Directive::Alone))) {
                        item.place |= Directive::Top | Directive::Bottom | Directive::Alone;
                    }
                    for (std::size_t scan = index; scan <= last; ++scan) {
                        Node* inner = column[scan];
                        if (!inner || (space(inner) && (scan < first || scan > final))) continue;
                        item.blocks.push_back(inner);
                        item.span += Line::extent(inner);
                        collect(inner, item.notes);
                    }
                    index = last;

                    if (order.fixed) {
                        // `[H]`: here, or at the head of the next column --
                        // where the space above it goes, as space does at a
                        // column's head.
                        if (!filling.empty() && height + item.span + 2.0f * rules.amid > left(item.notes)) turn(false);
                        const float lead = filling.empty() ? 0.0f : rules.amid;
                        if (lead > 0.0f) filling.push_back(kern(lead));
                        filling.insert(filling.end(), item.blocks.begin(), item.blocks.end());
                        filling.push_back(kern(rules.amid));
                        height += lead + item.span + rules.amid;
                        call(item);
                    } else if (item.across && count > 1) {
                        spanning.push_back(std::move(item));
                    } else if (waits(item.kind) || !(amid(item) || crowned(item) || footed(item))) {
                        waiting.push_back(std::move(item));
                    }
                    continue;
                }
            }

            // Space at the head of a column is dropped, as TeX drops it: the
            // gap that separated two blocks means nothing once a break has
            // fallen between them.
            if (dropped(block)) continue;

            // A forced break ends the column here, whatever room is left in
            // it -- or the whole sheet, for `\\clearpage`, which sets every
            // float still waiting first. The break itself goes on neither
            // side; two in a row make no empty page between them, because
            // seal() refuses one.
            if (block->type == Node::Type::Pause && block->pause().penalty.value <= -10000) {
                if (block->pause().penalty.flag) {
                    flush();
                } else {
                    turn(false);
                }
                continue;
            }

            const float span = Line::extent(block);
            added.clear();
            collect(block, added);
            float extra = room(added, notes.empty());

            // A footnote takes room from every column on the page, so one
            // called from a later column that the columns before it leave no
            // room for goes, with its block, to the next page.
            if (!added.empty() && !full.empty() && used + tallest + reserved + extra > context.height) {
                close();
                settle(false);
                extra = room(added, notes.empty());
            }

            // A non-empty column keeps a block taller than a whole page in a
            // column of its own instead of looping forever trying to fit it.
            // A column that took waiting floats and has no room left for the
            // block is a column of floats, and the block goes on. A long
            // table's row leaves room under it for the table's foot, which
            // closes the column when the next row does not fit; its head
            // opens the next, as longtable sets them.
            // A head and a row too tall for a page together are set anyway,
            // rather than turned over forever.
            const bool running = header || footer;
            const float closing = running ? Line::extent(footer) : 0.0f;
            bool turned = false;
            while (used + crown + sole + height + span + closing + reserved + extra > context.height &&
                   !(filling.empty() && heads.empty() && feet.empty())) {
                if (turned && running && filling.size() <= 1 && heads.empty() && feet.empty()) break;
                turned = true;
                if (footer && rows > 0) {
                    filling.push_back(footer);
                    height += closing;
                }
                turn(false);
                if (header) {
                    filling.push_back(header);
                    height += Line::extent(header);
                }
                rows = 0;
                extra = room(added, notes.empty());
            }
            if (dropped(block)) continue;

            if (running && block->type == Node::Type::Box) ++rows;
            filling.push_back(block);
            height += span;
            reserved += extra;
            notes.insert(notes.end(), added.begin(), added.end());
        }

        // Whatever floats are still waiting have pages of their own.
        flush();
        if (pages.empty()) return {};

        const memory::Slice<Page> made = arena.allocate<Page>(pages.size());
        std::ranges::copy(pages, made.begin());
        return made;
    }

}
