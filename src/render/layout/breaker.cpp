#include "layout/breaker.hpp"
#include "layout/line.hpp"
#include "typography/font.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace render::layout {

    Breaker::Breaker(memory::Arena& arena, memory::Arena& scratch, const Configuration& configuration) noexcept
        : arena(arena), scratch(scratch), configuration(configuration) {}

    memory::Slice<Node*> Breaker::compose(memory::Slice<Node*> input) const {
        const std::size_t count = input.size();
        if (count == 0) return {};

        auto width = scratch.allocate<double>(count + 1);
        auto stretch = scratch.allocate<double>(count + 1);
        auto shrink = scratch.allocate<double>(count + 1);

        width[0] = 0.0;
        stretch[0] = 0.0;
        shrink[0] = 0.0;

        for (std::size_t step = 0; step < count; ++step) {
            const Node* item = input[step];
            double size = 0.0, give = 0.0, take = 0.0;

            switch (item->type()) {
                case Node::Type::Glyph: size = static_cast<double>(item->glyph().width); break;
                case Node::Type::Box:   size = static_cast<double>(item->box().width); break;
                case Node::Type::Kern:  size = static_cast<double>(item->kern().width); break;
                case Node::Type::Rule:  size = static_cast<double>(item->rule().width); break;
                case Node::Type::Glue:
                    size = static_cast<double>(item->glue().width);
                    give = static_cast<double>(item->glue().stretch);
                    take = static_cast<double>(item->glue().shrink);
                    break;
                default: break;
            }

            width[step + 1] = width[step] + size;
            stretch[step + 1] = stretch[step] + give;
            shrink[step + 1] = shrink[step] + take;
        }

        struct Candidate {
            std::size_t index{0};
            std::size_t line{0};
            double demerits{0.0};
            const Candidate* link{nullptr};
        };

        const std::size_t capacity = count * 4 + 16;
        auto pool = scratch.allocate<Candidate>(capacity);
        std::size_t candidate = 0;

        pool[candidate++] = Candidate{.index = 0, .line = 0, .demerits = 0.0, .link = nullptr};
        const Candidate* tail = nullptr;

        for (std::size_t cursor = 0; cursor < count; ++cursor) {
            const Node* node = input[cursor];

            bool split = false;
            double cost = 0.0;

            if (node->type() == Node::Type::Penalty) {
                split = true;
                cost = static_cast<double>(node->penalty().value);
            } else if (node->type() == Node::Type::Pause) {
                split = true;
                cost = static_cast<double>(node->pause().penalty.value);
            } else if (node->type() == Node::Type::Glue && cursor > 0) {
                if (const Node* prior = input[cursor - 1]; prior->type() == Node::Type::Glyph ||
                    prior->type() == Node::Type::Box ||
                    prior->type() == Node::Type::Rule) {
                    split = true;
                }
            }

            const bool last = cursor == count - 1;
            const bool eject = (node->type() == Node::Type::Penalty && node->penalty().value <= -10000) ||
                               (node->type() == Node::Type::Pause && node->pause().penalty.value <= -10000);
            const bool forced = last || eject;
            if (forced) split = true;

            if (!split) continue;

            const bool waste = node->type() == Node::Type::Glue ||
                                node->type() == Node::Type::Penalty ||
                                node->type() == Node::Type::Pause;
            const std::size_t reach = waste ? cursor : cursor + 1;

            const Candidate* pick = nullptr;
            double lowest = std::numeric_limits<double>::max();

            for (std::size_t slot = 0; slot < candidate; ++slot) {
                const auto& entry = pool[slot];

                const double span = width[reach] - width[entry.index];
                const double give = stretch[reach] - stretch[entry.index];
                const double take = shrink[reach] - shrink[entry.index];
                const double gap = configuration.target - span;

                double ratio = 0.0;
                double badness = 0.0;

                if (gap > 0.0) {
                    ratio = give > 0.0 ? gap / give : 10.0;
                    badness = std::min(10000.0, 100.0 * (ratio * ratio * ratio));
                } else if (gap < 0.0) {
                    ratio = take > 0.0 ? -gap / take : 10.0;
                    badness = ratio <= 1.0 ? 100.0 * (ratio * ratio * ratio) : 10000.0;
                }

                if (!forced && badness > configuration.tolerance) continue;

                const double bias = configuration.penalty + cost;
                const double base = 10.0 + badness;
                if (const double loss = (base * base) + (bias * bias) + entry.demerits; loss < lowest) {
                    lowest = loss;
                    pick = &entry;
                }
            }

            if (!pick || candidate >= capacity) continue;

            pool[candidate] = Candidate{.index = cursor + 1, .line = pick->line + 1, .demerits = lowest, .link = pick};
            tail = &pool[candidate];
            ++candidate;
        }

        if (!tail) return {};

        const std::size_t lines = tail->line;
        auto result = arena.allocate<Node*>(lines);

        const Candidate* current = tail;
        while (current && current->link) {
            const std::size_t row = current->line - 1;
            const std::size_t start = current->link->index;

            const std::size_t mark = current->index - 1;
            const Node* border = input[mark];
            const bool waste = border->type() == Node::Type::Glue ||
                                border->type() == Node::Type::Penalty ||
                                border->type() == Node::Type::Pause;
            const std::size_t end = waste ? mark : current->index;

            const bool hyphenated = border->type() == Node::Type::Penalty && border->penalty().flag;
            const Node* preceding = nullptr;

            if (hyphenated) {
                for (std::size_t cursor = end; cursor > start; --cursor) {
                    if (input[cursor - 1]->type() == Node::Type::Glyph && input[cursor - 1]->glyph().font) {
                        preceding = input[cursor - 1];
                        break;
                    }
                }
            }

            const std::size_t length = end - start + (preceding ? 1 : 0);
            auto slice = arena.allocate<Node*>(length);
            for (std::size_t step = 0; step < end - start; ++step) {
                slice[step] = input[start + step];
            }

            if (preceding) {
                const typography::Font* font = preceding->glyph().font;
                const std::uint32_t id = font->glyph(static_cast<std::uint32_t>('-'));

                auto* hyphen = arena.compose<Node>(Node::Type::Glyph);
                hyphen->glyph({
                    .width = font->advance(id),
                    .height = preceding->glyph().height,
                    .depth = preceding->glyph().depth,
                    .x = 0.0f,
                    .y = 0.0f,
                    .code = id,
                    .font = font
                });
                slice[end - start] = hyphen;
            }

            result[row] = Line::horizontal(arena, slice, static_cast<float>(configuration.target));

            current = current->link;
        }

        return result;
    }

}