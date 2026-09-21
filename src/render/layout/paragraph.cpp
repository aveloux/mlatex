#include "layout/paragraph.hpp"
#include "layout/breaker.hpp"
#include "layout/line.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace render::layout {

    namespace {

        memory::Slice<Node*> hyphenate(
            const typography::Shaper& shaper,
            const typography::Hyphenator& hyphenator,
            const typography::Font& font,
            const std::string_view text,
            memory::Arena& arena,
            memory::Arena& scratch
        ) {
            std::vector<Node*> nodes;
            nodes.reserve(text.size());

            const typography::Font* fonts[] = { &font };
            Node* space = nullptr;

            std::size_t cursor = 0;
            while (cursor < text.size()) {
                if (text[cursor] == ' ') {
                    while (cursor < text.size() && text[cursor] == ' ') ++cursor;
                    if (!nodes.empty()) {
                        if (!space) {
                            const memory::Slice<Node*> shaped = shaper.shape(memory::Slice{fonts, 1}, " ", {});
                            space = shaped.empty() ? nullptr : shaped[0];
                        }
                        if (space) nodes.push_back(space);
                    }
                    continue;
                }

                const std::size_t start = cursor;
                while (cursor < text.size() && text[cursor] != ' ') ++cursor;
                const std::string_view word = text.substr(start, cursor - start);

                const memory::Slice<Node*> glyphs = shaper.shape(memory::Slice{fonts, 1}, word, {});
                if (glyphs.empty()) continue;

                memory::Slice<std::uint32_t> codepoints = scratch.allocate<std::uint32_t>(word.size());
                for (std::size_t index = 0; index < word.size(); ++index) {
                    codepoints[index] = static_cast<std::uint32_t>(static_cast<unsigned char>(word[index]));
                }

                const memory::Slice<std::uint8_t> breaks = hyphenator.execute(scratch, codepoints);
                const std::size_t limit = std::min(glyphs.size(), breaks.size());

                for (std::size_t index = 0; index < glyphs.size(); ++index) {
                    nodes.push_back(glyphs[index]);

                    if (index + 1 < glyphs.size() && index < limit && breaks[index]) {
                        auto* penalty = arena.compose<Node>(Node::Type::Penalty);
                        penalty->penalty({ .value = 50, .flag = true });
                        nodes.push_back(penalty);
                    }
                }
            }

            memory::Slice<Node*> slice = arena.allocate<Node*>(nodes.size());
            for (std::size_t index = 0; index < nodes.size(); ++index) {
                slice[index] = nodes[index];
            }
            return slice;
        }

    }

    Paragraph::Paragraph(
        memory::Arena& arena,
        const std::string_view text,
        const typography::Font& font,
        const float size
    ) noexcept
        : arena(arena), content(text), face(&font), scale(size) {}

    void Paragraph::assign(const std::string_view text) noexcept {
        if (content != text) {
            content = text;
            stale = true;
        }
    }

    void Paragraph::touch() noexcept {
        stale = true;
    }

    bool Paragraph::dirty() const noexcept {
        return stale;
    }

    float Paragraph::height() const noexcept {
        return tall;
    }

    float Paragraph::offset() const noexcept {
        return shift;
    }

    void Paragraph::offset(const float value) noexcept {
        shift = value;
    }

    Node* Paragraph::node() const noexcept {
        return tree;
    }

    float Paragraph::layout(
        const typography::Shaper& shaper,
        Ledger& ledger,
        memory::Arena& scratch,
        const float width,
        const float leading,
        const typography::Hyphenator* hyphenator
    ) noexcept {
        if (!stale) return 0.0f;

        const Ledger::Key key{ .font = face, .text = content, .size = scale };
        memory::Slice<Node*> nodes = ledger.find(key);

        if (nodes.empty()) {
            if (hyphenator) {
                nodes = hyphenate(shaper, *hyphenator, *face, content, arena, scratch);
            } else {
                const typography::Font* fonts[] = { face };
                nodes = shaper.shape(memory::Slice{fonts, 1}, content, {});
            }
            if (!nodes.empty()) {
                ledger.insert(key, nodes);
            }
        }

        const Breaker::Configuration configuration{
            .target = static_cast<double>(width),
            .leading = static_cast<double>(leading),
            .tolerance = 2000.0,
            .penalty = 0.0,
            .skip = static_cast<double>(leading),
            .limit = 0.0
        };

        const Breaker breaker(arena, scratch, configuration);
        const memory::Slice<Node*> lines = breaker.compose(nodes);

        tree = Line::vertical(arena, lines, leading);

        float updated = 0.0f;
        if (tree) {
            updated = tree->box().height;
        }

        const float delta = updated - tall;
        tall = updated;
        stale = false;

        return delta;
    }

}