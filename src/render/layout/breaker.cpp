/// @file
/// @brief Breaker implementation: Knuth and Plass line breaking.
///
/// Four running sums are built first, so the width, stretch, shrink and count
/// of infinite glue over any stretch of the paragraph are two subtractions
/// rather than a walk. The pass that follows keeps a set of breaks still worth
/// continuing from and drops the ones that can no longer reach the text being
/// considered.
#include "layout/breaker.hpp"
#include "layout/line.hpp"
#include "typography/font.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace render::layout {

    Breaker::Breaker(
        memory::Arena& arena,
        memory::Arena& scratch,
        const Configuration& configuration
    ) noexcept
        : arena(arena), scratch(scratch), rules(configuration) {}

    memory::Slice<Node*> Breaker::compose(const memory::Slice<Node*> input) const {
        const std::size_t count = input.size();
        if (count == 0) return {};

        // Running sums, one entry longer than the input so that the span from
        // break `a` to break `b` is `width[b] - width[a]` with no special case
        // at either end.
        const memory::Slice<float> width = scratch.allocate<float>(count + 1);
        const memory::Slice<float> stretch = scratch.allocate<float>(count + 1);
        const memory::Slice<float> shrink = scratch.allocate<float>(count + 1);
        const memory::Slice<std::uint32_t> infinite = scratch.allocate<std::uint32_t>(count + 1);

        width[0] = 0.0f;
        stretch[0] = 0.0f;
        shrink[0] = 0.0f;
        infinite[0] = 0;

        for (std::size_t index = 0; index < count; ++index) {
            const Node* item = input[index];
            const bool elastic = item && item->type() == Node::Type::Glue;

            width[index + 1] = width[index] + Line::advance(item);
            stretch[index + 1] = stretch[index] + (elastic ? item->glue().stretch : 0.0f);
            shrink[index + 1] = shrink[index] + (elastic ? item->glue().shrink : 0.0f);

            // Glue of an infinite order, counted apart from the finite kind:
            // a line holding any can always be filled, and is never loose.
            infinite[index + 1] = infinite[index] +
                                  (elastic && item->glue().expand != Node::Order::Normal ? 1u : 0u);
        }

        // Ragged and centred lines carry infinite glue at an edge, whatever
        // they hold between; so does a line cut short by a forced break.
        const bool ragged = rules.justification != Node::Justification::Full;

        /// @brief One break still worth continuing a paragraph from.
        struct Candidate {
            std::size_t index{0};       ///< Where the next line starts.
            std::size_t line{0};        ///< How many lines precede it.
            float demerits{0.0f};       ///< Total cost of getting here.
            const Candidate* link{nullptr};   ///< The break before this one.
            bool hyphenated{false};     ///< The line before it ends at a hyphen.
            int fitness{2};             ///< How that line sits: 0 very loose, 1 loose, 2 decent, 3 tight.
        };

        // One pass over the paragraph, as TeX makes one: every break within
        // the tolerance weighed by Knuth's demerits, each line's badness and
        // its break's penalty squared -- more for two hyphenated lines in a
        // row, for a hyphen ending the line before the last, and for a line
        // much looser or tighter than the one before it. Whether it had to
        // let a line run into the margin is said in `strained`.
        const auto pass = [&](const float tolerance, const bool hyphenating, bool& strained) -> const Candidate* {
            // One candidate per position at most, plus the start. Nothing
            // here grows, so the pass allocates once.
            const memory::Slice<Candidate> pool = scratch.allocate<Candidate>(count + 2);
            const memory::Slice<std::size_t> active = scratch.allocate<std::size_t>(count + 2);

            std::size_t taken = 0;
            std::size_t living = 0;

            pool[taken] = Candidate{};
            active[living++] = taken++;

            const Candidate* last = nullptr;

            for (std::size_t cursor = 0; cursor < count; ++cursor) {
                const Node* node = input[cursor];
                if (!node) continue;

                // Where a line may end. Glue preceded by something printable
                // is the usual place; a penalty says so outright -- a hyphen
                // the patterns found only on a pass that hyphenates -- and
                // the end of the paragraph is a break whether anything asked
                // for one or not.
                bool allowed = false;
                float cost = 0.0f;
                bool hyphen = false;

                if (node->type() == Node::Type::Penalty) {
                    allowed = hyphenating || !node->penalty().flag;
                    cost = static_cast<float>(node->penalty().value);
                    const Node* prior = cursor > 0 ? input[cursor - 1] : nullptr;
                    hyphen = node->penalty().flag ||
                             (prior && prior->type() == Node::Type::Glyph && prior->glyph().point == '-');
                } else if (node->type() == Node::Type::Pause) {
                    allowed = true;
                    cost = static_cast<float>(node->pause().penalty.value);
                } else if (node->type() == Node::Type::Glue && cursor > 0) {
                    const Node* prior = input[cursor - 1];
                    allowed = prior && (prior->type() == Node::Type::Glyph ||
                                        prior->type() == Node::Type::Box ||
                                        prior->type() == Node::Type::Rule);
                }

                const bool forced = cursor + 1 == count ||
                                    (node->type() == Node::Type::Penalty &&
                                     node->penalty().value <= -10000) ||
                                    (node->type() == Node::Type::Pause &&
                                     node->pause().penalty.value <= -10000);
                if (forced) allowed = true;
                if (!allowed) continue;

                // Glue and penalties at a break are discardable: the line ends
                // before them, and they do not print at the start of the next.
                const bool discarded = node->type() == Node::Type::Glue ||
                                       node->type() == Node::Type::Penalty ||
                                       node->type() == Node::Type::Pause;
                const std::size_t reach = discarded ? cursor : cursor + 1;

                const Candidate* best = nullptr;
                float lowest = std::numeric_limits<float>::max();
                int fit = 2;
                std::size_t surviving = 0;
                const Candidate* rescue = nullptr;

                for (std::size_t slot = 0; slot < living; ++slot) {
                    const Candidate& entry = pool[active[slot]];

                    const float span = width[reach] - width[entry.index];
                    const float give = stretch[reach] - stretch[entry.index];
                    const float take = shrink[reach] - shrink[entry.index];
                    const float gap = rules.target - span;

                    // Past shrinking: this break can never reach any position
                    // further on either, so it leaves the active set for good.
                    // Dropping these is what keeps the pass linear.
                    if (gap < -std::max(take, 0.0f)) {
                        if (!rescue || entry.demerits < rescue->demerits) rescue = &entry;
                        continue;
                    }

                    active[surviving++] = active[slot];

                    const bool fillable = ragged || forced || infinite[reach] != infinite[entry.index];

                    float badness = 0.0f;
                    int fitness = 2;
                    if (gap > 0.0f && !fillable) {
                        const float ratio = give > 0.0f ? gap / give : 10.0f;
                        badness = std::min(10000.0f, 100.0f * ratio * ratio * ratio);
                        fitness = badness > 99.0f ? 0 : badness > 12.0f ? 1 : 2;
                    } else if (gap < 0.0f) {
                        const float ratio = -gap / take;
                        badness = 100.0f * ratio * ratio * ratio;
                        fitness = badness > 12.0f ? 3 : 2;
                    }

                    if (!forced && badness > tolerance) continue;

                    // Knuth's demerits. The line's own cost is squared, so
                    // one dreadful line costs more than several indifferent
                    // ones; a penalty that discourages the break adds its
                    // square, and one that invites it takes its square away.
                    // A forced break adds nothing: every way through the
                    // paragraph pays for it, and squaring ten thousand in
                    // would swamp the differences that actually decide
                    // between them. Then TeX's three: \\doublehyphendemerits,
                    // \\finalhyphendemerits and \\adjdemerits.
                    const float base = rules.penalty + badness;
                    float loss = base * base + entry.demerits;
                    if (cost > 0.0f) {
                        loss += cost * cost;
                    } else if (cost > -10000.0f) {
                        loss -= cost * cost;
                    }
                    if (hyphen && entry.hyphenated) loss += 10000.0f;
                    if (cursor + 1 == count && entry.hyphenated) loss += 5000.0f;
                    if (std::abs(fitness - entry.fitness) > 1) loss += 10000.0f;

                    if (loss < lowest) {
                        lowest = loss;
                        best = &entry;
                        fit = fitness;
                    }
                }

                living = surviving;

                // Every break still open has just gone past shrinking, with
                // none taken: something wider than the column -- a long
                // formula, a word too long to hyphenate -- stands between
                // them and anywhere a line could end. TeX's answer, its
                // artificial demerits, is to take the break anyway from the
                // cheapest of them and let the line stand out into the
                // margin, overfull, rather than lose the rest of the
                // paragraph.
                if (!best && surviving == 0 && rescue) {
                    best = rescue;
                    lowest = rescue->demerits + rules.penalty * rules.penalty;
                    strained = true;
                }
                if (!best) continue;

                pool[taken] = Candidate{
                    .index = cursor + 1,
                    .line = best->line + 1,
                    .demerits = lowest,
                    .link = best,
                    .hyphenated = hyphen,
                    .fitness = fit
                };
                last = &pool[taken];

                // Every way through the paragraph has to break at a forced
                // break, so no line may start before it and end after it: the
                // breaks still active are retired and this one carries on
                // alone.
                if (forced) living = 0;
                active[living++] = taken++;
            }
            return last;
        };

        // As TeX sets a paragraph: first without hyphenating, taking lines
        // no worse than \\pretolerance; then hyphenating, up to \\tolerance;
        // and only when neither finds a way that keeps every line inside the
        // column, the looser lines the engine prefers to an overfull one.
        bool strained = false;
        const Candidate* last = pass(rules.pretolerance, false, strained);
        if (!last || strained) {
            strained = false;
            last = pass(rules.tolerance, true, strained);
        }
        if (!last || strained) {
            strained = false;
            last = pass(rules.emergency, true, strained);
        }

        if (!last || last->line == 0) return {};

        // The chain runs backwards, and its length is the line count, so the
        // lines can be written straight into their final places.
        const memory::Slice<Node*> lines = arena.allocate<Node*>(last->line);

        for (const Candidate* current = last; current && current->link; current = current->link) {
            std::size_t start = current->link->index;
            const std::size_t mark = current->index - 1;

            const Node* border = input[mark];
            const bool discarded = border && (border->type() == Node::Type::Glue ||
                                              border->type() == Node::Type::Penalty ||
                                              border->type() == Node::Type::Pause);
            const std::size_t stop = discarded ? mark : current->index;

            // What follows a break only ever separated it from what came
            // before -- the space after `\\`, say -- and is dropped from the
            // start of the next line, as TeX drops it. Not at the start of the
            // paragraph, whose indentation is a kern that has to stay.
            while (start > 0 && start < stop && input[start] &&
                   (input[start]->type() == Node::Type::Glue ||
                    input[start]->type() == Node::Type::Kern ||
                    input[start]->type() == Node::Type::Penalty)) {
                ++start;
            }

            // A break taken at a flagged penalty is a hyphenated one, and the
            // hyphen is only drawn now that the break has actually been taken.
            const Node* preceding = nullptr;
            if (border && border->type() == Node::Type::Penalty && border->penalty().flag) {
                for (std::size_t index = stop; index > start; --index) {
                    if (input[index - 1]->type() == Node::Type::Glyph &&
                        input[index - 1]->glyph().font) {
                        preceding = input[index - 1];
                        break;
                    }
                }
            }

            // Infinite glue at whichever edges the alignment wants filled.
            const bool leading = rules.justification == Node::Justification::Right ||
                                 rules.justification == Node::Justification::Center;
            const bool trailing = rules.justification == Node::Justification::Left ||
                                  rules.justification == Node::Justification::Center;

            // In a justified paragraph the last line, and a line ended by a
            // forced break, end with TeX's `\\parfillskip`: glue that
            // stretches at the first infinite order and so takes the room the
            // words leave, which then keep their natural spacing -- while an
            // `\\hfill` in the line, at the second order, takes it instead
            // and carries what follows it to the margin, as the end of a
            // proof's mark is. Every other line fills the column with the
            // glue between words when justified, and with the edge glue above
            // when not.
            const bool forced = border && ((border->type() == Node::Type::Penalty &&
                                            border->penalty().value <= -10000) ||
                                           (border->type() == Node::Type::Pause &&
                                            border->pause().penalty.value <= -10000));
            const bool natural = !ragged && (current == last || forced);

            const std::size_t length = stop - start + (preceding ? 1 : 0) +
                                       (leading ? 1 : 0) + (trailing ? 1 : 0) + (natural ? 1 : 0);
            const memory::Slice<Node*> row = arena.allocate<Node*>(length);
            std::size_t filled = 0;

            if (leading) {
                auto* fill = arena.compose<Node>(Node::Type::Glue);
                fill->glue({.stretch = 1.0f, .expand = Node::Order::Fil});
                row[filled++] = fill;
            }
            for (std::size_t index = start; index < stop; ++index) {
                row[filled++] = input[index];
            }

            if (preceding) {
                const typography::Font* font = preceding->glyph().font;
                const std::uint32_t glyph = font->index('-');

                auto* hyphen = arena.compose<Node>(Node::Type::Glyph);
                hyphen->glyph({
                    .width = font->advance(glyph),
                    .height = preceding->glyph().height,
                    .depth = preceding->glyph().depth,
                    .code = glyph,
                    .point = '-',
                    .font = font
                });
                row[filled++] = hyphen;
            }

            if (trailing || natural) {
                auto* fill = arena.compose<Node>(Node::Type::Glue);
                fill->glue({.stretch = 1.0f, .expand = Node::Order::Fil});
                row[filled++] = fill;
            }

            lines[current->line - 1] = Line::horizontal(arena, memory::Slice{row.data, filled}, rules.target);
        }

        return lines;
    }

}
