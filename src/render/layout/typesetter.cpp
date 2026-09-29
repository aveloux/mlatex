/// @file
/// @brief Typesetter implementation: formulas into boxes, documents into pages.
///
/// render() is one switch over the expression node kinds, not a table of
/// handlers. A formula has nine kinds of node and they are not extensible at
/// run time, so a switch is both the smaller code and the faster dispatch --
/// the compiler turns it into a jump table, and nothing has to be registered
/// before a document can be set.
#include "layout/typesetter.hpp"
#include "layout/line.hpp"
#include "typography/expression.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <array>
#include <string_view>
#include <tuple>
#include <vector>

namespace render::layout {

    using Category = syntax::expression::Unicodes::Category;
    using Expression = syntax::expression::Node;
    using Alphabet = Typesetter::Alphabet;

    /// @brief How much space goes between two adjacent atoms, in
    ///        eighteenths of an em.
    ///
    /// This is TeX's spacing table, and it is most of what makes a formula
    /// read as mathematics rather than as a row of symbols: `a+b` gets a
    /// medium space around the plus, `a=b` a thick one around the equals,
    /// and `f(x)` none at all. Rows are the atom on the left, columns the
    /// atom on the right, in Unicodes::Category order.
    ///
    /// A space marked here is dropped at script size, again as TeX does
    /// it, because an exponent has no room for it.
    static constexpr std::array<std::array<std::uint8_t, 9>, 9> spacing{{
        //         Ord  Op  Bin  Rel Open Close Punct Inner Accent
        /* Ord   */ {{ 0,  3,  4,  5,  0,  0,  0,  3,  0 }},
        /* Op    */ {{ 3,  3,  0,  5,  0,  0,  0,  3,  0 }},
        /* Bin   */ {{ 4,  4,  0,  0,  4,  0,  0,  4,  0 }},
        /* Rel   */ {{ 5,  5,  0,  0,  5,  0,  0,  5,  0 }},
        /* Open  */ {{ 0,  0,  0,  0,  0,  0,  0,  0,  0 }},
        /* Close */ {{ 0,  3,  4,  5,  0,  0,  0,  3,  0 }},
        /* Punct */ {{ 3,  3,  0,  3,  3,  3,  3,  3,  0 }},
        /* Inner */ {{ 3,  3,  4,  5,  3,  0,  3,  3,  0 }},
        /* Accent*/ {{ 0,  3,  4,  5,  0,  0,  0,  3,  0 }},
    }};

    /// @brief How wide a spacing command is, in eighteenths of an em.
    ///
    /// These are the escape hatch from the class table, for the places it
    /// reads wrongly -- `dx` wants a thin space that no rule would put
    /// there, and `\\!` takes one back out. The widths are TeX's.
    ///
    /// @param name The command as written, backslash included.
    /// @return Its width, or 0 when the name is not a spacing command --
    ///         which is also the width of a space that is not one.
    /// @complexity O(1).
    static constexpr float width(std::string_view name) noexcept {
        if (name.starts_with('\\')) name.remove_prefix(1);

        if (name == ",") return 3.0f;     // thin
        if (name == ":") return 4.0f;     // medium
        if (name == ";") return 5.0f;     // thick
        if (name == "!") return -3.0f;    // negative thin
        if (name == " " || name == "~") return 6.0f;   // a word space: `\ ` and a tie
        if (name == "quad") return 18.0f;
        if (name == "qquad") return 36.0f;
        return 0.0f;
    }

    /// @brief Which class of atom a node counts as, seen from one side.
    ///
    /// Spacing is decided by the two atoms that actually touch, so a
    /// structure is classed by its edge. `f(x)` sets no space between the
    /// f and the parenthesis because what the f meets is an opening
    /// delimiter; `(x)+1` sets a medium one after the parenthesis because
    /// a closing delimiter meets a binary operator. A group fenced with
    /// `\\left` and `\\right` is Inner as a whole, and so are fractions
    /// and radicals, which is what gives `\\frac{a}{b}+c` the same plus
    /// spacing an ordinary letter would get.
    ///
    /// @param node    Node to classify; null counts as Ordinary.
    /// @param closing True for the node's right-hand edge, the one that
    ///                meets whatever follows it; false for its left.
    /// @return Its category on that side.
    /// @complexity O(depth) of the edge it follows, which is O(1) in practice.
    static constexpr Category classify(const Expression* node, const bool closing) noexcept {
        if (!node) return Category::Ordinary;

        switch (node->type) {
            case Expression::Type::Fraction:
            case Expression::Type::Radical:
            case Expression::Type::Matrix:
                return Category::Inner;
            case Expression::Type::Group:
                if (node->open == 0 && node->close == 0) return Category::Ordinary;
                if (node->category == Category::Inner) return Category::Inner;
                return closing ? Category::Closing : Category::Opening;
            case Expression::Type::Binary:
                return classify(closing ? node->right : node->left, closing);
            case Expression::Type::Script:
                return classify(node->left, closing);
            case Expression::Type::Sequence:
                // An operator a document named, which spaces as `\\sin` does.
                if (node->value == "\\operatorname" || node->value == "\\operatorname*") {
                    return Category::Operator;
                }
                // A mark set over or under something is whatever it is set
                // on: `\\stackrel{def}{=}` is still a relation.
                if (node->arguments.count == 2 &&
                    (node->value == "\\overset" || node->value == "\\underset" || node->value == "\\stackrel")) {
                    return classify(node->arguments[1], closing);
                }
                // A named function is an operator; a spacing command is
                // not an atom at all and is left Ordinary.
                if (node->arguments.count == 0) {
                    return !node->left && width(node->value) == 0.0f ? Category::Operator
                                                                     : Category::Ordinary;
                }
                if (node->value.empty()) {
                    return classify(node->arguments[closing ? node->arguments.count - 1 : 0], closing);
                }
                return Category::Ordinary;
            default:
                return node->category;
        }
    }

    Typesetter::Typesetter(
        memory::Arena& arena,
        typography::Registry& registry,
        const typography::Shaper& shaper,
        const Metrics& metrics
    ) noexcept
        : arena(arena), registry(registry), shaper(shaper), metrics(metrics) {}

    Typesetter::Typesetter(
        memory::Arena& arena,
        typography::Registry& registry,
        const typography::Shaper& shaper
    ) noexcept
        : Typesetter(arena, registry, shaper, Metrics{}) {}

    Typesetter::Frame Typesetter::frame(
        const typography::Font& font,
        const float target,
        const Style style,
        const Alphabet alphabet
    ) const {
        const typography::Expression table(font);
        const typography::Expression::Metric math = table.metrics();
        const bool carried = table.present();

        // A script is a real smaller font, not the base font drawn small: its
        // stems keep their weight and its sidebearings stay in metrics.
        // The font's own percentages are used when it states them.
        float factor = 1.0f;
        if (style == Style::Script) {
            factor = carried && math.script > 0.0f ? math.script : metrics.script;
        } else if (style == Style::Scriptscript) {
            factor = carried && math.scriptscript > 0.0f
                         ? math.scriptscript
                         : metrics.script * metrics.script;
        }

        const typography::Font* resolved = &font;
        if (factor < 1.0f && !font.family().empty()) {
            const typography::Registry::Request smaller{
                .family = font.family(),
                .size = font.size() * factor
            };
            if (const typography::Font* found = registry.get(smaller)) {
                resolved = found;
            }
        }

        const float size = resolved->size();

        // Proportions are fractions of the size; the font's own numbers are
        // already points, and are preferred whenever it states them.
        return Frame{
            .font = resolved,
            .base = &font,
            .alphabet = alphabet,
            .target = target,
            .style = style,
            .axis = carried && math.axis != 0.0f ? math.axis * factor : size * metrics.axis,
            .thickness = carried && math.thickness > 0.0f
                             ? math.thickness * factor
                             : size * metrics.thickness,
            .numerator = carried && math.numerator > 0.0f
                             ? math.numerator * factor
                             : size * metrics.numerator,
            .denominator = carried && math.denominator > 0.0f
                               ? math.denominator * factor
                               : size * metrics.denominator,
            .ascent = carried && math.ascent > 0.0f ? math.ascent * factor : size * metrics.ascent,
            .descent = carried && math.descent > 0.0f ? math.descent * factor : size * metrics.descent,
            .upper = carried && math.upper > 0.0f ? math.upper * factor : size * metrics.upper,

            // Without the font's own, TeX's: the bar's thickness in a line
            // and three of it in a display, three and seven between the
            // parts of a binomial.
            .separation = carried && math.separation > 0.0f ? math.separation * factor
                                                            : size * metrics.thickness,
            .distance = carried && math.distance > 0.0f ? math.distance * factor
                                                        : size * metrics.thickness * 3.0f,
            .spacing = carried && math.spacing > 0.0f ? math.spacing * factor
                                                      : size * metrics.thickness * 3.0f,
            .spread = carried && math.spread > 0.0f ? math.spread * factor
                                                    : size * metrics.thickness * 7.0f,
            .superscript = carried && math.superscript > 0.0f
                               ? math.superscript * factor
                               : size * metrics.superscript,
            .subscript = carried && math.subscript > 0.0f
                             ? math.subscript * factor
                             : size * metrics.subscript,
            .gap = carried && math.gap > 0.0f ? math.gap * factor : size * metrics.gap,
            .clearance = carried && math.clearance > 0.0f ? math.clearance * factor : size * metrics.clearance,
            .rule = carried && math.rule > 0.0f ? math.rule * factor : size * metrics.thickness,
            .ascender = carried ? math.ascender * factor : size * metrics.ascender,
            .before = carried ? math.before * factor : size * metrics.before,
            .after = carried ? math.after * factor : size * metrics.after,
            .raise = carried && math.raise > 0.0f ? math.raise : metrics.raise,
            .quad = size,
            .display = carried && math.display > 0.0f ? math.display * factor
                                                      : size * metrics.display,
            .drop = carried && math.drop > 0.0f ? math.drop * factor : size * metrics.drop,
            .sink = carried && math.sink > 0.0f ? math.sink * factor : size * metrics.sink,
            .limit = carried && math.limit > 0.0f ? math.limit * factor : size * metrics.limit
        };
    }

    Node* Typesetter::lower(
        const syntax::expression::Node* node,
        const typography::Font& font,
        const float target,
        const Style style
    ) const {
        if (!node) return nullptr;

        Node* box = render(node, frame(font, target, style));
        if (!box) return nullptr;

        // Always wrapped, so the box handed back is one this method owns.
        // A fraction sets a shift of its own to reach the axis, and a caller
        // stacking it in a column would otherwise overwrite that.
        if (node->style != syntax::expression::Node::Style::Display || target <= 0.0f) {
            const memory::Slice<Node*> only = arena.allocate<Node*>(1);
            only[0] = box;
            return Line::horizontal(arena, only, 0.0f);
        }

        // A displayed formula is centred in the column, which is glue on both
        // sides of it rather than a computed offset: the line then stays
        // centred if the column is later resized.

        auto* left = arena.compose<Node>(Node::Type::Glue);
        left->glue({.width = 0.0f, .stretch = 1.0f, .expand = Node::Order::Fil});

        auto* right = arena.compose<Node>(Node::Type::Glue);
        right->glue({.width = 0.0f, .stretch = 1.0f, .expand = Node::Order::Fil});

        const memory::Slice<Node*> row = arena.allocate<Node*>(3);
        row[0] = left;
        row[1] = box;
        row[2] = right;
        return Line::horizontal(arena, row, target);
    }

    void Typesetter::paint(Node* node, const Node::Color& color) noexcept {
        if (!node) return;
        if (node->type == Node::Type::Glyph) {
            Node::Glyph mark = node->glyph();
            mark.color = color;
            node->glyph(mark);
        } else if (node->type == Node::Type::Rule) {
            Node::Rule bar = node->rule();
            bar.color = color;
            node->rule(bar);
        } else if (node->type == Node::Type::Box) {
            for (Node* inner : node->box().list) paint(inner, color);
        }
    }

    memory::Slice<Node*> Typesetter::unfold(
        const syntax::expression::Node* node,
        const typography::Font& font
    ) const {
        if (!node) return {};

        // render() appends the outer level's pieces as it makes them, and
        // hands back whatever it built as a box of its own instead.
        std::vector<Node*> pieces;
        Frame scope = frame(font, 0.0f, Style::Text);
        scope.pieces = &pieces;
        if (Node* box = render(node, scope)) pieces.push_back(box);
        if (pieces.empty()) return {};

        // Nothing to break at: one box, wrapped as lower() wraps it, so a
        // fraction's own shift survives whatever is done to the box after.
        if (pieces.size() == 1) {
            const memory::Slice<Node*> only = arena.allocate<Node*>(1);
            only[0] = pieces[0];
            const memory::Slice<Node*> wrapped = arena.allocate<Node*>(1);
            wrapped[0] = Line::horizontal(arena, only, 0.0f);
            return wrapped;
        }

        const memory::Slice<Node*> run = arena.allocate<Node*>(pieces.size());
        std::ranges::copy(pieces, run.begin());
        return run;
    }

    Node* Typesetter::enclose(Node* part) const {
        if (!part) return nullptr;
        if (part->type == Node::Type::Box && part->box().shift == 0.0f &&
            part->box().offset == 0.0f) {
            return part;
        }
        const memory::Slice<Node*> only = arena.allocate<Node*>(1);
        only[0] = part;
        return Line::horizontal(arena, only, 0.0f);
    }

    float Typesetter::bearing(const Node* node) noexcept {
        if (!node) return 0.0f;

        switch (node->type) {
            case Node::Type::Glyph: {
                const Node::Glyph& mark = node->glyph();
                return mark.x + (mark.font ? mark.font->bounds(mark.code).x : 0.0f);
            }
            case Node::Type::Box: {
                const Node::Box& box = node->box();
                const auto blank = [](const Node* child) {
                    return child->type == Node::Type::Penalty || child->type == Node::Type::Directive;
                };

                // Along a line the edge is the first thing drawn, and space
                // in front of it moves the ink in by its own width.
                if (box.alignment == Node::Alignment::Horizontal && !box.absolute) {
                    float offset = box.offset;
                    for (const Node* child : box.list) {
                        if (!child || blank(child)) continue;
                        if (child->type == Node::Type::Kern || child->type == Node::Type::Glue) {
                            offset += Line::advance(child);
                            continue;
                        }
                        return offset + bearing(child);
                    }
                    return offset;
                }

                // In a column or on a canvas every child starts from the same
                // edge, so the edge is whichever reaches furthest left -- a
                // fraction's bar, not the narrower numerator above it.
                float least = std::numeric_limits<float>::max();
                for (const Node* child : box.list) {
                    if (!child || blank(child) || child->type == Node::Type::Kern ||
                        child->type == Node::Type::Glue) {
                        continue;
                    }
                    least = std::min(least, bearing(child));
                }
                return box.offset + (least == std::numeric_limits<float>::max() ? 0.0f : least);
            }
            default:
                // A rule or an image starts its ink at its edge.
                return 0.0f;
        }
    }

    Node* Typesetter::delimiter(
        const std::uint32_t codepoint,
        const float reach,
        const Frame& scope
    ) const {
        if (codepoint == 0 || !scope.font) return nullptr;

        const typography::Font& font = *scope.font;
        const std::uint32_t base = font.index(codepoint);
        if (base == 0) return nullptr;

        const typography::Expression math(font);
        const auto [glyph, advance] = math.stretch(base, reach);

        // Taller than the tallest size the font draws whole -- a parenthesis
        // around a matrix of several rows -- it is built from the font's
        // parts instead, and centred on the axis the same way.
        if (advance < reach) {
            if (Node* built = extend(codepoint, reach, scope); built && built->box().list.count > 1) {
                Node::Box shape = built->box();
                shape.shift = shape.height * 0.5f - scope.axis;
                built->box(shape);
                return built;
            }
        }

        const typography::Font::Box ink = font.bounds(glyph);

        // The ink's own middle, above the baseline, and how far the glyph has
        // to move for that middle to land on the axis. Down the page is
        // positive, so a glyph whose middle is below the axis moves up.
        const float middle = ink.y - ink.height * 0.5f;
        const float span = std::max(ink.height, advance);

        auto* drawn = arena.compose<Node>(Node::Type::Glyph);
        drawn->glyph({
            .width = font.advance(glyph),
            .height = scope.axis + span * 0.5f,
            .depth = std::max(span * 0.5f - scope.axis, 0.0f),
            .y = middle - scope.axis,
            .code = glyph,
            .point = codepoint,
            .font = &font
        });
        return drawn;
    }

    Node* Typesetter::extend(const std::uint32_t codepoint, const float reach, const Frame& scope) const {
        if (codepoint == 0 || !scope.font) return nullptr;

        const typography::Font& font = *scope.font;
        const std::uint32_t base = font.index(codepoint);
        if (base == 0) return nullptr;

        // A size drawn whole when the font has one tall enough, and the
        // parts it builds any other height from when it has not.
        const typography::Expression math(font);
        const typography::Expression::Variant whole = math.stretch(base, reach);
        typography::Expression::Assembly built;
        if (whole.advance < reach) built = math.assemble(base, reach);
        if (built.pieces.empty()) {
            built.pieces.push_back({.glyph = whole.glyph, .lift = 0.0f});
            built.span = font.bounds(whole.glyph).height;
        }

        // Each piece placed so its ink's bottom stands its lift above the
        // box's baseline; the box is a canvas, so each carries its own place.
        const memory::Slice<Node*> pieces = arena.allocate<Node*>(built.pieces.size());
        float width = 0.0f;
        for (std::size_t index = 0; index < built.pieces.size(); ++index) {
            const auto& [glyph, lift] = built.pieces[index];
            const typography::Font::Box ink = font.bounds(glyph);

            auto* piece = arena.compose<Node>(Node::Type::Glyph);
            piece->glyph({
                .width = font.advance(glyph),
                .height = lift + ink.height,
                .depth = 0.0f,
                .y = ink.y - ink.height - lift,
                .code = glyph,
                // Only the first says what the whole is, so the mark reads
                // back as one character however many pieces drew it.
                .point = index == 0 ? codepoint : 0u,
                .font = &font
            });
            pieces[index] = piece;
            width = std::max(width, font.advance(glyph));
        }

        auto* box = arena.compose<Node>(Node::Type::Box);
        Node::Box shape{};
        shape.width = width;
        shape.height = built.span;
        shape.list = pieces;
        shape.absolute = true;
        box->box(shape);
        return box;
    }

    Node* Typesetter::fence(
        Node* inner,
        const std::uint32_t open,
        const std::uint32_t close,
        const Frame& scope
    ) const {
        Node* body = enclose(inner);

        // How far the contents reach from the axis, on whichever side reaches
        // further. TeX lets a delimiter fall a tenth short of twice that,
        // which is what keeps a parenthesis around a lone letter at the
        // letter's own size instead of one step up.
        const float height = body ? body->box().height : scope.quad * 0.5f;
        const float depth = body ? body->box().depth : scope.quad * 0.5f;
        const float reach = 2.0f * std::max(height - scope.axis, depth + scope.axis) * 0.901f;

        std::array<Node*, 3> parts{};
        std::size_t filled = 0;

        if (Node* left = delimiter(open, reach, scope)) parts[filled++] = left;
        if (body) parts[filled++] = body;
        if (Node* right = delimiter(close, reach, scope)) parts[filled++] = right;

        if (filled == 0) return nullptr;
        const memory::Slice<Node*> row = arena.allocate<Node*>(filled);
        std::copy_n(parts.begin(), filled, row.begin());
        return Line::horizontal(arena, row, 0.0f);
    }

    Node* Typesetter::render(const syntax::expression::Node* node, const Frame& scope) const {
        if (!node || !scope.font) return nullptr;

        // Only an operator and a run of atoms keep the formula's outer level
        // open to a break, appending their pieces to it rather than building
        // a row; anything else is a box of its own, which a line never enters.
        if (scope.pieces && node->type != Expression::Type::Binary &&
            !(node->type == Expression::Type::Sequence && node->value.empty())) {
            Frame closed = scope;
            closed.pieces = nullptr;
            return render(node, closed);
        }

        const typography::Font& font = *scope.font;

        switch (node->type) {

            // A symbol. The code point is what the maths tables resolved the
            // name to; index() turns it into the glyph the font actually
            // draws, and a zero there means this face has no such character.
            case Expression::Type::Variable: {
                // A delimiter at one of TeX's fixed sizes, `\\Bigl(`, grown to
                // that size whatever it stands beside: `\\big`, `\\Big`, `\\bigg`
                // and `\\Bigg`, each a step above the last in quads, the `l`,
                // `r` and `m` forms the same sizes. Asked of every symbol set,
                // so anything not a `\\b` or a `\\B` word is turned away on its
                // first two characters.
                if (std::string_view sized = node->value;
                    sized.size() >= 4 && sized[0] == '\\' && (sized[1] == 'b' || sized[1] == 'B')) {
                    sized.remove_prefix(1);
                    if (sized.ends_with('l') || sized.ends_with('r') || sized.ends_with('m')) sized.remove_suffix(1);
                    const float size = sized == "big"    ? 1.2f
                                       : sized == "Big"  ? 1.8f
                                       : sized == "bigg" ? 2.4f
                                       : sized == "Bigg" ? 3.0f
                                                         : 0.0f;
                    if (size > 0.0f) return delimiter(node->codepoint, size * scope.quad, scope);
                }

                // A large operator in a displayed formula is drawn at the size
                // the font keeps for exactly that, and centred on the axis.
                if (node->category == Category::Operator && node->codepoint != 0 &&
                    node->style == Expression::Style::Display && scope.style == Style::Text && !scope.stepped) {
                    // Only a size drawn whole: a script is placed against the
                    // glyph's own corners, which a mark built from parts has not.
                    if (Node* large = delimiter(node->codepoint, scope.display, scope);
                        large && large->type == Node::Type::Glyph) {
                        return large;
                    }
                }

                // A run of letters and digits written together is still one
                // atom per character, each in the alphabet in force.
                const std::string_view written = node->value;
                if (node->codepoint == 0 && written.size() > 1 &&
                    std::ranges::all_of(written, [](const char character) {
                        return (character >= 'a' && character <= 'z') ||
                               (character >= 'A' && character <= 'Z') ||
                               (character >= '0' && character <= '9');
                    })) {
                    const memory::Slice<Node*> row = arena.allocate<Node*>(written.size());
                    std::size_t filled = 0;
                    for (std::size_t index = 0; index < written.size(); ++index) {
                        syntax::expression::Node single = *node;
                        single.value = written.substr(index, 1);
                        if (Node* drawn = render(&single, scope)) row[filled++] = drawn;
                    }
                    if (filled == 0) return nullptr;
                    return Line::horizontal(arena, memory::Slice{row.data, filled}, 0.0f);
                }

                // The character as written, moved into the alphabet in force
                // when the face has it there. The text layer keeps the letter
                // as written, so a formula copied out of the page reads `x`
                // rather than a mathematical italic x.
                std::uint32_t codepoint = node->codepoint;
                if (codepoint == 0 && written.size() == 1) {
                    codepoint = static_cast<unsigned char>(written[0]);
                }
                if (codepoint != 0) {
                    // Where the letter sits in the maths alphabet in force. The
                    // alphabets are ranges of their own in Unicode: a blackboard
                    // R is not an R with a style applied but another character,
                    // and the maths italic is how TeX sets a variable -- Latin
                    // letters and lower-case Greek lean, capital Greek and digits
                    // stand upright. A handful of letters were encoded before the
                    // ranges existed -- the italic h, the blackboard R, the
                    // script L -- and the ranges leave holes where they would be;
                    // those are sent to where they actually live. Zero when the
                    // character stays as it is.
                    const std::uint32_t moved = [&]() -> std::uint32_t {
                        // Bold: `\\mathbf` upright, and `\\bm` the bold maths italic -- whose
                        // capital Greek stays upright, as the italic's does. Greek and the
                        // two signs that go with it have bold forms of their own in both.
                        if (scope.alphabet == Alphabet::Bold || scope.alphabet == Alphabet::Heavy) {
                            const bool slanted = scope.alphabet == Alphabet::Heavy;
                            if (codepoint >= 0x03B1 && codepoint <= 0x03C9) {
                                return (slanted ? 0x1D736 : 0x1D6C2) + (codepoint - 0x03B1);
                            }
                            if (codepoint >= 0x0391 && codepoint <= 0x03A9) return 0x1D6A8 + (codepoint - 0x0391);
                            switch (codepoint) {
                                case 0x2207: return slanted ? 0x1D735 : 0x1D6C1;   // nabla
                                case 0x2202: return slanted ? 0x1D74F : 0x1D6DB;   // partial
                                case 0x03F5: return slanted ? 0x1D750 : 0x1D6DC;   // lunate epsilon
                                case 0x03D1: return slanted ? 0x1D751 : 0x1D6DD;   // theta symbol
                                case 0x03D5: return slanted ? 0x1D753 : 0x1D6DF;   // phi symbol
                                case 0x03F1: return slanted ? 0x1D754 : 0x1D6E0;   // rho symbol
                                case 0x03D6: return slanted ? 0x1D755 : 0x1D6E1;   // pi symbol
                                default: break;
                            }
                            const bool upper = codepoint >= 'A' && codepoint <= 'Z';
                            const bool lower = codepoint >= 'a' && codepoint <= 'z';
                            if (codepoint >= '0' && codepoint <= '9') return 0x1D7CE + (codepoint - '0');
                            if (!upper && !lower) return 0;
                            const std::uint32_t letter = upper ? codepoint - 'A' : codepoint - 'a';
                            if (slanted) return (upper ? 0x1D468 : 0x1D482) + letter;
                            return (upper ? 0x1D400 : 0x1D41A) + letter;
                        }

                        const bool italic = scope.alphabet == Alphabet::Italic;

                        if (italic) {
                            // Lower-case Greek runs contiguously in both places; the
                            // variant forms and the partial sign sit after it.
                            if (codepoint >= 0x03B1 && codepoint <= 0x03C9) return 0x1D6FC + (codepoint - 0x03B1);
                            switch (codepoint) {
                                case 0x2202: return 0x1D715;   // partial
                                case 0x03F5: return 0x1D716;   // lunate epsilon
                                case 0x03D1: return 0x1D717;   // theta symbol
                                case 0x03F0: return 0x1D718;   // kappa symbol
                                case 0x03D5: return 0x1D719;   // phi symbol
                                case 0x03F1: return 0x1D71A;   // rho symbol
                                case 0x03D6: return 0x1D71B;   // pi symbol
                                default: break;
                            }
                        }

                        const bool upper = codepoint >= 'A' && codepoint <= 'Z';
                        const bool lower = codepoint >= 'a' && codepoint <= 'z';
                        const bool digit = codepoint >= '0' && codepoint <= '9';
                        if (!upper && !lower && !digit) return 0;

                        const std::uint32_t letter = upper ? codepoint - 'A' : lower ? codepoint - 'a' : codepoint - '0';

                        // The ranges are laid out capitals, then lower case, then digits,
                        // and an alphabet that has no digits of its own borrows them.
                        switch (scope.alphabet) {
                            case Alphabet::Italic:
                                if (digit) return 0;
                                if (codepoint == 'h') return 0x210E;
                                return (upper ? 0x1D434 : 0x1D44E) + letter;
                            case Alphabet::Blackboard:
                                if (digit) return 0x1D7D8 + letter;
                                switch (codepoint) {
                                    case 'C': return 0x2102;
                                    case 'H': return 0x210D;
                                    case 'N': return 0x2115;
                                    case 'P': return 0x2119;
                                    case 'Q': return 0x211A;
                                    case 'R': return 0x211D;
                                    case 'Z': return 0x2124;
                                    default: return (upper ? 0x1D538 : 0x1D552) + letter;
                                }
                            case Alphabet::Calligraphic:
                                if (digit) return 0;
                                switch (codepoint) {
                                    case 'B': return 0x212C;
                                    case 'E': return 0x2130;
                                    case 'F': return 0x2131;
                                    case 'H': return 0x210B;
                                    case 'I': return 0x2110;
                                    case 'L': return 0x2112;
                                    case 'M': return 0x2133;
                                    case 'R': return 0x211B;
                                    case 'e': return 0x212F;
                                    case 'g': return 0x210A;
                                    case 'o': return 0x2134;
                                    default: return (upper ? 0x1D49C : 0x1D4B6) + letter;
                                }
                            case Alphabet::Fraktur:
                                if (digit) return 0;
                                switch (codepoint) {
                                    case 'C': return 0x212D;
                                    case 'H': return 0x210C;
                                    case 'I': return 0x2111;
                                    case 'R': return 0x211C;
                                    case 'Z': return 0x2128;
                                    default: return (upper ? 0x1D504 : 0x1D51E) + letter;
                                }
                            case Alphabet::Sans:
                                if (digit) return 0x1D7E2 + letter;
                                return (upper ? 0x1D5A0 : 0x1D5BA) + letter;
                            case Alphabet::Typewriter:
                                if (digit) return 0x1D7F6 + letter;
                                return (upper ? 0x1D670 : 0x1D68A) + letter;
                            case Alphabet::Upright:
                            case Alphabet::Bold:
                            case Alphabet::Heavy:
                                break;
                        }
                        // Upright is what the characters already are.
                        return 0;
                    }();
                    std::uint32_t glyph = 0;
                    if (moved != 0) glyph = font.index(moved);
                    if (glyph == 0) glyph = font.index(codepoint);

                    if (glyph != 0) {
                        const typography::Font::Box ink = font.bounds(glyph);
                        auto* drawn = arena.compose<Node>(Node::Type::Glyph);
                        drawn->glyph({
                            .width = font.advance(glyph),
                            .height = std::max(ink.y, 0.0f),
                            .depth = std::max(ink.height - ink.y, 0.0f),
                            .code = glyph,
                            .point = codepoint,
                            .font = &font
                        });
                        return drawn;
                    }
                }

                // No code point, or a face that cannot draw it: shape the text
                // as written. That covers letters and digits, which arrive as
                // characters rather than as named symbols.
                if (node->value.empty()) return nullptr;
                const typography::Font* fonts[] = {&font};
                const memory::Slice<Node*> shaped =
                    shaper.shape(memory::Slice{fonts, 1uz}, node->value, {});
                if (shaped.empty()) return nullptr;
                if (shaped.count == 1) return shaped[0];
                return Line::horizontal(arena, shaped, 0.0f);
            }

            // An operator with two operands. The operator itself is a symbol,
            // so it lowers through the same path, and the spaces around it
            // come from the table rather than from the font.
            case Expression::Type::Binary:
            case Expression::Type::Unary: {
                // The operator's own glyph, looked up as a symbol would be.
                syntax::expression::Node symbol{};
                symbol.type = Expression::Type::Variable;
                symbol.value = node->value;
                symbol.codepoint = node->codepoint;
                Frame closed = scope;
                closed.pieces = nullptr;
                Node* mark = render(&symbol, closed);

                // The operator's own class: a relation, punctuation, or an
                // ordinary symbol such as the solidus spaced as its class
                // says; anything else between two operands is binary. A sign
                // is ordinary, with nothing on its left to be between.
                const Category sort = node->type != Expression::Type::Binary ? Category::Ordinary
                                      : node->category == Category::Relation ||
                                                node->category == Category::Punctuation ||
                                                node->category == Category::Ordinary
                                          ? node->category
                                          : Category::Binary;
                const auto pad = [&](const Category before, const Category after) {
                    if (scope.style != Style::Text) return 0.0f;
                    return scope.quad * static_cast<float>(spacing[static_cast<std::size_t>(before)]
                                                                  [static_cast<std::size_t>(after)]) / 18.0f;
                };
                const float leading = pad(Category::Ordinary, sort);
                const float trailing = pad(sort, Category::Ordinary);

                // On an inline formula's outer level, the operands' pieces and
                // its own go straight onto it, with TeX's \\relpenalty or
                // \\binoppenalty after the operator: the price of ending a line
                // there, which is the only place in such a formula one may end.
                if (scope.pieces) {
                    std::vector<Node*>& pieces = *scope.pieces;
                    const std::size_t before = pieces.size();
                    if (Node* box = render(node->left, scope)) pieces.push_back(box);
                    const bool left = pieces.size() > before;
                    const bool right = node->right != nullptr;
                    if (left && mark && leading > 0.0f) {
                        auto* space = arena.compose<Node>(Node::Type::Kern);
                        space->kern({.width = leading});
                        pieces.push_back(space);
                    }
                    if (mark) pieces.push_back(mark);
                    if (left && right && mark && (sort == Category::Relation || sort == Category::Binary)) {
                        auto* price = arena.compose<Node>(Node::Type::Penalty);
                        price->penalty({.value = sort == Category::Relation ? 500 : 700});
                        pieces.push_back(price);
                    }
                    if (right && mark && trailing > 0.0f) {
                        auto* space = arena.compose<Node>(Node::Type::Kern);
                        space->kern({.width = trailing});
                        pieces.push_back(space);
                    }
                    if (Node* box = render(node->right, scope)) pieces.push_back(box);
                    return nullptr;
                }

                Node* left = render(node->left, scope);
                Node* right = render(node->right, scope);

                std::array<Node*, 5> parts{};
                std::size_t filled = 0;

                if (left) parts[filled++] = left;
                if (left && mark && leading > 0.0f) {
                    auto* space = arena.compose<Node>(Node::Type::Kern);
                    space->kern({.width = leading});
                    parts[filled++] = space;
                }
                if (mark) parts[filled++] = mark;
                if (right && mark && trailing > 0.0f) {
                    auto* space = arena.compose<Node>(Node::Type::Kern);
                    space->kern({.width = trailing});
                    parts[filled++] = space;
                }
                if (right) parts[filled++] = right;

                if (filled == 0) return nullptr;
                if (filled == 1) return parts[0];

                const memory::Slice<Node*> row = arena.allocate<Node*>(filled);
                std::copy_n(parts.begin(), filled, row.begin());
                return Line::horizontal(arena, row, 0.0f);
            }

            // A run of atoms, with the table's space between each pair.
            case Expression::Type::Sequence: {
                const std::size_t count = node->arguments.count;
                if (count == 0) {
                    // A spacing command: how an author overrides the class
                    // table where it reads wrongly. TeX measures these in
                    // eighteenths of an em, and so does the table.
                    if (const float units = width(node->value); units != 0.0f) {
                        auto* space = arena.compose<Node>(Node::Type::Kern);
                        space->kern({.width = scope.quad * units / 18.0f});
                        return space;
                    }
                    if (node->left) return render(node->left, scope);

                    // A named function. Its name is set upright, in words,
                    // which is the whole difference between `sin` meaning a
                    // function and `sin` meaning three variables multiplied.
                    std::string_view name = node->value;
                    if (name.starts_with('\\')) name.remove_prefix(1);
                    if (name.empty()) return nullptr;

                    const typography::Font* fonts[] = {&font};
                    const memory::Slice<Node*> shaped =
                        shaper.shape(memory::Slice{fonts, 1uz}, name, {});
                    if (shaped.empty()) return nullptr;
                    return Line::horizontal(arena, shaped, 0.0f);
                }

                // A command over one argument: an alphabet, or a bar.
                if (count == 1 && !node->value.empty()) {
                    std::string_view name = node->value;
                    if (name.starts_with('\\')) name.remove_prefix(1);

                    // An alphabet applies to every letter inside it, scripts
                    // included, so it travels down in the frame. The maths
                    // alphabets name themselves; a named operator is its
                    // argument set upright, and so are words -- `\\text` and
                    // the text styles -- unless their style says otherwise;
                    // `\\bm` is TeX's bold maths italic.
                    std::optional<Alphabet> letters;
                    if (name == "mathbb") letters = Alphabet::Blackboard;
                    else if (name == "mathcal" || name == "mathscr") letters = Alphabet::Calligraphic;
                    else if (name == "mathfrak") letters = Alphabet::Fraktur;
                    else if (name == "mathbf" || name == "textbf") letters = Alphabet::Bold;
                    else if (name == "mathsf" || name == "textsf") letters = Alphabet::Sans;
                    else if (name == "mathtt" || name == "texttt") letters = Alphabet::Typewriter;
                    else if (name == "mathit" || name == "textit" || name == "textsl" || name == "emph") {
                        letters = Alphabet::Italic;
                    } else if (name == "bm" || name == "boldsymbol") {
                        letters = Alphabet::Heavy;
                    } else if (name.starts_with("math") || name == "operatorname" || name == "operatorname*" ||
                               name == "text" || name == "textrm" || name == "textup" || name == "textnormal" ||
                               name == "textmd" || name == "mbox" || name == "hbox") {
                        letters = Alphabet::Upright;
                    }
                    if (letters) {
                        Frame inner = scope;
                        inner.alphabet = *letters;
                        inner.pieces = nullptr;
                        return render(node->arguments[0], inner);
                    }

                    // What its argument would take up, and nothing drawn:
                    // `\\phantom` both ways, `\\hphantom` only across and
                    // `\\vphantom` only up and down, a strut as tall as it.
                    if (name == "phantom" || name == "hphantom" || name == "vphantom") {
                        const Node* body = enclose(render(node->arguments[0], scope));
                        if (!body) return nullptr;
                        Node* blank = Line::horizontal(arena, memory::Slice<Node*>{}, 0.0f);
                        Node::Box shape = blank->box();
                        shape.width = name == "vphantom" ? 0.0f : body->box().width;
                        shape.height = name == "hphantom" ? 0.0f : body->box().height;
                        shape.depth = name == "hphantom" ? 0.0f : body->box().depth;
                        blank->box(shape);
                        return blank;
                    }

                    // Drawn, but standing no taller or deeper than the line.
                    if (name == "smash") {
                        Node* body = enclose(render(node->arguments[0], scope));
                        if (!body) return nullptr;
                        Node::Box shape = body->box();
                        shape.height = 0.0f;
                        shape.depth = 0.0f;
                        body->box(shape);
                        return body;
                    }

                    // A bar above or below, drawn the width of what it covers.
                    const bool above = name == "overline";
                    if (above || name == "underline") {
                        Node* body = enclose(render(node->arguments[0], scope));
                        if (!body) return nullptr;

                        auto* bar = arena.compose<Node>(Node::Type::Rule);
                        bar->rule({.width = body->box().width, .height = scope.thickness});

                        auto* clearance = arena.compose<Node>(Node::Type::Kern);
                        clearance->kern({.width = scope.gap});

                        const memory::Slice<Node*> column = arena.allocate<Node*>(3);
                        column[0] = above ? bar : body;
                        column[1] = clearance;
                        column[2] = above ? body : bar;

                        Node* stacked = Line::vertical(arena, column, 0.0f, Line::Anchor::Middle);
                        Node::Box shape = stacked->box();

                        // A column hangs from its top edge, so it is raised
                        // until the contents keep the baseline they had.
                        shape.shift = above ? -(body->box().height + scope.gap + scope.thickness)
                                            : -body->box().height;
                        stacked->box(shape);
                        return stacked;
                    }
                }

                // A mark at script size set over or under something, centred
                // on it: `\\overset{!}{=}`, `\\underset{x}{\\max}`, and
                // `\\stackrel`, the older name for setting one over.
                if (count == 2 && (node->value == "\\overset" || node->value == "\\underset" ||
                                   node->value == "\\stackrel")) {
                    const bool over = node->value != "\\underset";
                    const Style smaller = scope.style == Style::Text ? Style::Script : Style::Scriptscript;
                    Node* mark = enclose(render(node->arguments[0], frame(*scope.base, scope.target, smaller,
                                                                             scope.alphabet)));
                    Node* body = enclose(render(node->arguments[1], scope));
                    if (!mark || !body) return body ? body : mark;

                    auto* clearance = arena.compose<Node>(Node::Type::Kern);
                    clearance->kern({.width = scope.limit});

                    const memory::Slice<Node*> column = arena.allocate<Node*>(3);
                    column[0] = over ? mark : body;
                    column[1] = clearance;
                    column[2] = over ? body : mark;

                    // A column hangs from its top edge, so it is raised until
                    // the base keeps the baseline it had.
                    Node* stacked = Line::vertical(arena, column, 0.0f, Line::Anchor::Middle);
                    Node::Box shape = stacked->box();
                    shape.shift = over ? -(Line::extent(mark) + scope.limit + body->box().height)
                                       : -body->box().height;
                    stacked->box(shape);
                    return stacked;
                }

                // On an inline formula's outer level, each atom's pieces and the
                // table's space before it go straight onto it. The space is
                // put in once the atom is known to have set something.
                if (scope.pieces) {
                    std::vector<Node*>& pieces = *scope.pieces;
                    const syntax::expression::Node* previous = nullptr;
                    for (std::size_t index = 0; index < count; ++index) {
                        const syntax::expression::Node* child = node->arguments[index];
                        const std::size_t before = pieces.size();
                        if (Node* box = render(child, scope)) pieces.push_back(box);
                        if (pieces.size() == before) continue;

                        if (previous && scope.style == Style::Text) {
                            const std::uint8_t units =
                                spacing[static_cast<std::size_t>(classify(previous, true))]
                                       [static_cast<std::size_t>(classify(child, false))];
                            if (units != 0) {
                                auto* space = arena.compose<Node>(Node::Type::Kern);
                                space->kern({.width = scope.quad * static_cast<float>(units) / 18.0f});
                                pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(before), space);
                            }
                        }
                        previous = child;
                    }
                    return nullptr;
                }

                // At most one space between each pair, so twice the atoms
                // less one covers the worst case with nothing to grow.
                const memory::Slice<Node*> row = arena.allocate<Node*>(count * 2);
                std::size_t filled = 0;
                const syntax::expression::Node* previous = nullptr;

                for (std::size_t index = 0; index < count; ++index) {
                    const syntax::expression::Node* child = node->arguments[index];
                    Node* box = render(child, scope);
                    if (!box) continue;

                    if (previous && scope.style == Style::Text) {
                        const std::uint8_t units =
                            spacing[static_cast<std::size_t>(classify(previous, true))]
                                   [static_cast<std::size_t>(classify(child, false))];
                        if (units != 0) {
                            auto* space = arena.compose<Node>(Node::Type::Kern);
                            space->kern({.width = scope.quad * static_cast<float>(units) / 18.0f});
                            row[filled++] = space;
                        }
                    }

                    row[filled++] = box;
                    previous = child;
                }

                if (filled == 0) return nullptr;
                return Line::horizontal(arena, memory::Slice{row.data, filled}, 0.0f);
            }

            // A group. A brace group only groups; a parenthesised one, or one
            // written with `\\left` and `\\right`, draws its delimiters grown
            // to whatever is inside them.
            case Expression::Type::Group: {
                Node* inner = render(node->left, scope);
                if (node->open == 0 && node->close == 0) return inner;
                return fence(inner, node->open, node->close, scope);
            }

            // A base with an index, an exponent, or both: beside it as a rule,
            // above and below it for a sum or a limit in a displayed formula.
            case Expression::Type::Script: {
                Node* base = render(node->left, scope);

                const Style inner = scope.style == Style::Text ? Style::Script : Style::Scriptscript;
                const Frame nested = frame(*scope.base, scope.target, inner, scope.alphabet);

                Node* raised = enclose(render(node->superscript, nested));
                Node* dropped = enclose(render(node->subscript, nested));
                if (!raised && !dropped) return base;

                const bool displayed = node->style == Expression::Style::Display &&
                                       scope.style == Style::Text && !scope.stepped;

                // In a displayed formula a sum, a product or a limit puts its
                // bounds over and under itself, where there is room; an
                // integral keeps them beside it, where its slant expects
                // them. That is TeX's own rule: every large operator but the
                // integral signs, which run from the single integral to the
                // contour forms; an operator named with a star, as
                // `\\DeclareMathOperator*` makes one; and the named operators
                // that behave like a sum.
                const Expression* limited = node->left;
                std::string_view named = limited && limited->type == Expression::Type::Sequence ? limited->value : "";
                if (named.starts_with('\\')) named.remove_prefix(1);
                const bool bounded =
                    limited &&
                    ((limited->type == Expression::Type::Variable && limited->category == Category::Operator &&
                      (limited->codepoint < 0x222B || limited->codepoint > 0x2233)) ||
                     (limited->type == Expression::Type::Sequence && limited->value == "\\operatorname*") ||
                     (limited->type == Expression::Type::Sequence && limited->arguments.count == 0 &&
                      (named == "lim" || named == "max" || named == "min" || named == "sup" || named == "inf" ||
                       named == "det" || named == "gcd" || named == "Pr")));

                if (displayed && bounded) {
                    Node* middle = enclose(base);

                    std::array<Node*, 5> parts{};
                    std::size_t filled = 0;
                    float reach = 0.0f;   // from the column's top to the base's baseline

                    if (raised) {
                        parts[filled++] = raised;
                        auto* clearance = arena.compose<Node>(Node::Type::Kern);
                        clearance->kern({.width = scope.limit});
                        parts[filled++] = clearance;
                        reach += Line::extent(raised) + scope.limit;
                    }
                    if (middle) {
                        parts[filled++] = middle;
                        reach += middle->box().height;
                    }
                    if (dropped) {
                        auto* clearance = arena.compose<Node>(Node::Type::Kern);
                        clearance->kern({.width = scope.limit});
                        parts[filled++] = clearance;
                        parts[filled++] = dropped;
                    }

                    const memory::Slice<Node*> column = arena.allocate<Node*>(filled);
                    std::copy_n(parts.begin(), filled, column.begin());

                    // A column hangs from its top edge; raising it by the
                    // distance down to the base's baseline puts the operator
                    // back on the line with its limits centred over and under.
                    Node* stacked = Line::vertical(arena, column, 0.0f, Line::Anchor::Middle);
                    Node::Box shape = stacked->box();
                    shape.shift = -reach;
                    stacked->box(shape);
                    return stacked;
                }

                // A letter takes its scripts at the font's fixed positions.
                // Anything taller -- a bracket, a fraction, a displayed
                // integral -- pushes them out to its own top and bottom, less
                // the drop the font allows, as TeX does for any boxed base.
                float rise = scope.superscript;
                float fall = scope.subscript;
                const bool letter = node->left && node->left->type == Expression::Type::Variable &&
                                    !(displayed && node->left->category == Category::Operator);
                if (!letter) {
                    if (const Node* measured = enclose(base)) {
                        rise = std::max(rise, measured->box().height - scope.drop);
                        fall = std::max(fall, measured->box().depth + scope.sink);
                    }
                }

                std::array<Node*, 8> parts{};
                std::size_t filled = 0;
                float width = 0.0f;

                if (base) parts[filled++] = base;

                // A slanted glyph leans out past its own advance, and the font
                // states by how much. A letter's superscript is pushed out by
                // that much so it clears the lean; a large operator's
                // subscript is pulled in by it instead, to sit under the foot
                // of an integral rather than out beyond its top.
                float lean = 0.0f;
                if (base && base->type == Node::Type::Glyph) {
                    const typography::Expression math(font);
                    lean = math.correction(base->glyph().code);
                }
                const bool large = node->left && node->left->type == Expression::Type::Variable &&
                                   node->left->category == Category::Operator;

                // Each script is shifted and nothing else: the line it goes
                // into works its height and depth out from the shift, so
                // adjusting them here as well would count the move twice.
                for (const auto& [script, shift, indent] :
                     {std::tuple{raised, -rise, large ? 0.0f : lean},
                      std::tuple{dropped, fall, large ? -lean : 0.0f}}) {
                    if (!script) continue;

                    if (indent != 0.0f) {
                        auto* inset = arena.compose<Node>(Node::Type::Kern);
                        inset->kern({.width = indent});
                        parts[filled++] = inset;
                    }

                    Node::Box shape = script->box();
                    shape.shift = shift;   // up the page is negative
                    script->box(shape);

                    parts[filled++] = script;
                    width = std::max(width, indent + shape.width);

                    // Back to where the script started, so the other one is
                    // set at the same place rather than after it.
                    auto* back = arena.compose<Node>(Node::Type::Kern);
                    back->kern({.width = -(indent + shape.width)});
                    parts[filled++] = back;
                }

                // One forward kern past whichever script is wider, so the next
                // atom clears both of them.
                auto* forward = arena.compose<Node>(Node::Type::Kern);
                forward->kern({.width = width});
                parts[filled++] = forward;

                const memory::Slice<Node*> row = arena.allocate<Node*>(filled);
                std::copy_n(parts.begin(), filled, row.begin());
                return Line::horizontal(arena, row, 0.0f);
            }

            // Numerator over bar over denominator, with the bar on the maths
            // axis so that it lines up with the minus signs beside it. A
            // binomial is the same stack with no bar, fenced in parentheses;
            // `\\atop` the stack alone.
            case Expression::Type::Fraction: {
                std::string_view name = node->value;
                if (name.starts_with('\\')) name.remove_prefix(1);
                const bool binomial = name.ends_with("binom") || name == "choose";
                const bool barless = binomial || name == "atop";

                // A displayed fraction keeps its parts at full size, which is
                // the whole reason a document displays one. Anywhere else the
                // parts step down one size, as TeX has it -- unless the
                // document asked otherwise with `\\dfrac` or `\\tfrac`.
                const bool displayed = node->style == Expression::Style::Display &&
                                       scope.style == Style::Text && !scope.stepped;
                const bool roomy = name.starts_with('d') || (displayed && !name.starts_with('t'));
                const Style inner = roomy ? Style::Text
                                          : scope.style == Style::Text ? Style::Script
                                                                       : Style::Scriptscript;

                // Its parts are in text style even in a display, so a sum in
                // one is the size it is in a line, with its limits beside it.
                Frame nested = frame(*scope.base, scope.target, inner, scope.alphabet);
                nested.stepped = true;

                Node* above = enclose(render(node->left, nested));
                Node* below = enclose(render(node->right, nested));
                if (!above && !below) return nullptr;

                const float thickness = barless ? 0.0f : scope.thickness;
                const float width = std::max(above ? above->box().width : 0.0f, below ? below->box().width : 0.0f);

                // Where each part's baseline goes, measured from the line's
                // baseline: as far as the font asks for, in a display or in a
                // line, and further when that would bring a part inside the
                // least clearance -- from the bar, or, with no bar, from the
                // other part, the two then moved apart by half the shortfall
                // each. TeX's rule 15, with the font's numbers.
                const float top = scope.axis + thickness * 0.5f;      // the bar's top edge
                const float bottom = scope.axis - thickness * 0.5f;   // and its bottom edge
                const float depth = above ? above->box().depth : 0.0f;
                const float height = below ? below->box().height : 0.0f;

                float lift = roomy ? scope.numerator : barless ? scope.upper : scope.ascent;
                float drop = roomy ? scope.denominator : scope.descent;
                if (barless) {
                    const float shortfall = (roomy ? scope.spread : scope.spacing) - ((lift - depth) - (height - drop));
                    if (shortfall > 0.0f) {
                        lift += shortfall * 0.5f;
                        drop += shortfall * 0.5f;
                    }
                } else {
                    const float least = roomy ? scope.distance : scope.separation;
                    lift = std::max(lift, top + least + depth);
                    drop = std::max(drop, least + height - bottom);
                }

                std::array<Node*, 5> parts{};
                std::size_t filled = 0;

                if (above) {
                    parts[filled++] = above;
                    auto* clearance = arena.compose<Node>(Node::Type::Kern);
                    clearance->kern({.width = lift - above->box().depth - top});
                    parts[filled++] = clearance;
                }

                auto* bar = arena.compose<Node>(Node::Type::Rule);
                bar->rule({.width = barless ? 0.0f : width, .height = thickness});
                parts[filled++] = bar;

                if (below) {
                    auto* clearance = arena.compose<Node>(Node::Type::Kern);
                    clearance->kern({.width = drop - below->box().height + bottom});
                    parts[filled++] = clearance;
                    parts[filled++] = below;
                }

                const memory::Slice<Node*> column = arena.allocate<Node*>(filled);
                std::copy_n(parts.begin(), filled, column.begin());

                // A column hangs from its top edge, which is the numerator's
                // top: raising the column by that much puts every part where
                // it was placed above. The line it joins works out its height
                // and depth from the shift, so neither is adjusted here.
                Node* stacked = Line::vertical(arena, column, 0.0f, Line::Anchor::Middle);
                Node::Box shape = stacked->box();
                shape.shift = above ? -(lift + above->box().height) : -top;
                shape.width = std::max(shape.width, width);
                stacked->box(shape);

                if (binomial) return fence(stacked, '(', ')', scope);

                // The bar is as wide as the wider part and no wider; the room
                // either side of it is TeX's null delimiters, the width of
                // `\\nulldelimiterspace`.
                auto* before = arena.compose<Node>(Node::Type::Kern);
                before->kern({.width = scope.base->size() * metrics.padding});
                auto* after = arena.compose<Node>(Node::Type::Kern);
                after->kern({.width = scope.base->size() * metrics.padding});
                const memory::Slice<Node*> row = arena.allocate<Node*>(3);
                row[0] = before;
                row[1] = stacked;
                row[2] = after;
                return Line::horizontal(arena, row, 0.0f);
            }

            // A radical sign beside its contents, with a bar over them, built
            // the way the OpenType MATH table lays one out: the sign is the
            // first of the font's sizes that covers the contents, the gap and
            // the bar together; whatever it has to spare is shared evenly above
            // and below the contents rather than all left hanging under the
            // line; and its top is where the bar is drawn.
            case Expression::Type::Radical: {
                Node* body = enclose(render(node->right, scope));

                // The contents' extent, or an x-height's worth of nothing for
                // a radical over nothing at all, so the sign still has a size.
                const float height = body ? body->box().height : scope.quad * 0.45f;
                const float depth = body ? body->box().depth : 0.0f;

                // A displayed radical stands clear of its contents by the
                // font's wider display gap, as TeX's own does. In a line the
                // font's own gap is the least there is, and contents that fill
                // the sign -- a fraction, a superscript -- get room in
                // proportion to their height on top of the bar, up to the
                // display gap, so a tall term never reads as touching its bar.
                const bool displayed = node->style == Expression::Style::Display && scope.style == Style::Text &&
                                       !scope.stepped;
                float gap = displayed ? scope.clearance
                                      : std::max(scope.gap, std::min(scope.clearance, scope.rule + (height + depth) / 10.0f));
                const float needed = height + depth + gap + scope.rule;

                Node* sign = extend(0x221A, needed, scope);
                std::array<Node*, 5> parts{};
                std::size_t filled = 0;
                float side = 0.0f;     // the drawn sign's own space after its ink

                // The bar's top, above the baseline: the contents' top, the
                // gap -- widened by half of whatever the sign has to spare, so
                // a short letter under a full-size sign sits in the middle of
                // it rather than on its floor -- and the bar itself.
                float span = 0.0f;
                if (sign) {
                    span = sign->box().height;
                    if (span > needed) gap += (span - needed) * 0.5f;
                }
                const float top = height + gap + scope.rule;

                // The degree of a cube root, set small, its baseline raised
                // the font's share of the sign's height from the sign's own
                // bottom, and tucked in against the sign by the font's own
                // kerns. The kern after it is negative, so it is held to no
                // more than the degree and the space before it, and the sign
                // never starts left of the radical's own edge.
                if (node->left && sign) {
                    const Frame nested = frame(*scope.base, scope.target, Style::Scriptscript, scope.alphabet);
                    if (Node* raw = render(node->left, nested)) {
                        Node* degree = enclose(raw);
                        Node::Box shape = degree->box();
                        shape.shift = -((top - span) + span * scope.raise);   // up the page is negative
                        degree->box(shape);

                        auto* before = arena.compose<Node>(Node::Type::Kern);
                        before->kern({.width = scope.before});
                        auto* after = arena.compose<Node>(Node::Type::Kern);
                        after->kern({.width = std::max(scope.after, -(scope.before + shape.width))});

                        parts[filled++] = before;
                        parts[filled++] = degree;
                        parts[filled++] = after;
                    }
                }

                if (sign) {
                    // Standing on its own baseline, the sign's ink runs up to
                    // its span; moved down by the span less the bar's height,
                    // its top meets the bar exactly.
                    Node::Box shape = sign->box();
                    shape.shift = span - top;
                    sign->box(shape);
                    parts[filled++] = sign;

                    const typography::Font::Box ink = scope.font->bounds(shape.list[0]->glyph().code);
                    side = shape.width - (ink.x + ink.width);
                }

                if (body) {
                    // The sign's ink ends where its advance does, so whatever
                    // follows it is only as far from it as its own first
                    // letter's side bearing: an upright digit a third of a
                    // point, an italic letter hardly any -- which is why
                    // `\\sqrt{5x}` read clear and `\\sqrt{x}` touched. So the
                    // contents move in under the bar until their ink stands
                    // at least a thirty-sixth of an em -- half a mu, a digit's
                    // own bearing -- from the sign's. The bar covers the
                    // space as well, so it still meets the sign; contents
                    // already clear of it do not move at all.
                    if (sign) {
                        const float least = scope.quad / 36.0f;
                        const float open = side + bearing(body);
                        if (open < least) {
                            auto* inset = arena.compose<Node>(Node::Type::Kern);
                            inset->kern({.width = least - open});

                            const memory::Slice<Node*> padded = arena.allocate<Node*>(2);
                            padded[0] = inset;
                            padded[1] = body;
                            body = Line::horizontal(arena, padded, 0.0f);
                        }
                    }

                    // The bar over the contents is a rule above them, with the
                    // font's room above it: a vertical stack of the room, the
                    // rule, the gap and the contents.
                    auto* room = arena.compose<Node>(Node::Type::Kern);
                    room->kern({.width = scope.ascender});

                    auto* bar = arena.compose<Node>(Node::Type::Rule);
                    bar->rule({.width = body->box().width, .height = scope.rule, .depth = 0.0f});

                    auto* space = arena.compose<Node>(Node::Type::Kern);
                    space->kern({.width = gap});

                    const memory::Slice<Node*> column = arena.allocate<Node*>(4);
                    column[0] = room;
                    column[1] = bar;
                    column[2] = space;
                    column[3] = body;

                    // Raised so the contents keep their own baseline and the
                    // bar's top sits where the sign's does.
                    Node* covered = Line::vertical(arena, column, 0.0f, Line::Anchor::Middle);
                    Node::Box shape = covered->box();
                    shape.shift = -(top + scope.ascender);
                    covered->box(shape);

                    parts[filled++] = covered;
                }

                if (filled == 0) return nullptr;
                const memory::Slice<Node*> row = arena.allocate<Node*>(filled);
                std::copy_n(parts.begin(), filled, row.begin());
                return Line::horizontal(arena, row, 0.0f);
            }

            // A grid: its rows laid out by grid(), stacked, and the whole
            // centred on the axis the way a fraction is -- then fenced, when
            // it names delimiters, exactly as a `\\left ... \\right` group is.
            case Expression::Type::Matrix: {
                // A matrix's cells, and a piecewise definition's, are in text
                // style even in a display, as TeX's `\\halign` sets them; the
                // lines of an `aligned` or a `gathered` stay displayed.
                Frame cells = scope;
                cells.stepped = scope.stepped || node->grid == Expression::Grid::Spaced;
                const memory::Slice<Node*> built = grid(node, cells);
                if (built.empty()) return nullptr;

                // A jot between the rows of anything set out as displayed
                // lines -- `aligned`, `cases` -- and none between a matrix's,
                // whose struts alone keep its rows a line apart.
                const bool spread = node->grid == Expression::Grid::Paired || node->value == "ll";
                Node* stacked = Line::vertical(arena, built, spread ? scope.quad * 0.3f : 0.0f,
                                               Line::Anchor::Middle);

                // A column hangs from its top edge; centred on the axis is
                // the reference point moved up by half the stack's total
                // extent, then up again by the axis itself.
                Node::Box shape = stacked->box();
                shape.shift = -scope.axis - (shape.height + shape.depth) * 0.5f;
                stacked->box(shape);

                if (node->open == 0 && node->close == 0) return stacked;
                return fence(stacked, node->open, node->close, scope);
            }

            // Words, from `\\text` and its kin: spaces kept, each word shaped
            // whole when upright so it keeps its kerning, and each letter in
            // another alphabet that alphabet's own character.
            case Expression::Type::Text: {
                const std::string_view text = node->value;
            if (text.empty()) return nullptr;

            const bool upright = scope.alphabet == Alphabet::Upright || scope.alphabet == Alphabet::Italic;
            const float space = scope.quad / 3.0f;

            const auto alphanumeric = [](const char letter) {
                return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
                       (letter >= '0' && letter <= '9');
            };

            std::vector<Node*> parts;
            const auto shape = [&](const std::string_view run) {
                if (run.empty()) return;
                const typography::Font* fonts[] = {&font};
                const memory::Slice<Node*> shaped = shaper.shape(memory::Slice{fonts, 1uz}, run, {});
                parts.insert(parts.end(), shaped.begin(), shaped.end());
            };

            std::size_t at = 0;
            while (at < text.size()) {
                if (text[at] == ' ') {
                    auto* gap = arena.compose<Node>(Node::Type::Kern);
                    gap->kern({.width = space});
                    parts.push_back(gap);
                    ++at;
                    continue;
                }

                std::size_t end = text.find(' ', at);
                if (end == std::string_view::npos) end = text.size();
                const std::string_view word = text.substr(at, end - at);
                at = end;

                // Upright, the word is shaped whole and keeps its kerning; in
                // another alphabet each letter is that alphabet's own character,
                // and anything the alphabet has no form of is shaped as it is.
                if (upright) {
                    shape(word);
                    continue;
                }
                std::size_t start = 0;
                for (std::size_t index = 0; index <= word.size(); ++index) {
                    if (index < word.size() && !alphanumeric(word[index])) continue;
                    shape(word.substr(start, index - start));
                    if (index < word.size()) {
                        Expression single{};
                        single.type = Expression::Type::Variable;
                        single.value = word.substr(index, 1);
                        if (Node* drawn = render(&single, scope)) parts.push_back(drawn);
                    }
                    start = index + 1;
                }
            }

            if (parts.empty()) return nullptr;
            const memory::Slice<Node*> row = arena.allocate<Node*>(parts.size());
            std::ranges::copy(parts, row.begin());
            return Line::horizontal(arena, row, 0.0f);
            }

            // A mark centred over its argument. The mark's name selects the
            // character; anything unrecognised falls through as the argument
            // alone, which is better than dropping the argument with it.
            case Expression::Type::Accent: {
                Node* body = enclose(render(node->left, scope));
                if (!body) return nullptr;

                std::string_view name = node->value;
                if (name.starts_with('\\')) name.remove_prefix(1);
                // The wide marks are the narrow ones, drawn the same.
                if (name.starts_with("wide")) name.remove_prefix(4);

                std::uint32_t code = 0;
                if (name == "hat") code = 0x0302;
                else if (name == "mathring") code = 0x030A;
                else if (name == "dddot") code = 0x20DB;
                else if (name == "ddddot") code = 0x20DC;
                else if (name == "overrightarrow") code = 0x20D7;
                else if (name == "overleftarrow") code = 0x20D6;
                else if (name == "overleftrightarrow") code = 0x20E1;
                else if (name == "tilde") code = 0x0303;
                else if (name == "bar") code = 0x0304;
                else if (name == "breve") code = 0x0306;
                else if (name == "dot") code = 0x0307;
                else if (name == "ddot") code = 0x0308;
                else if (name == "check") code = 0x030C;
                else if (name == "acute") code = 0x0301;
                else if (name == "grave") code = 0x0300;
                else if (name == "vec") code = 0x20D7;

                const std::uint32_t glyph = code != 0 ? font.index(code) : 0;
                if (glyph == 0) return body;

                auto* mark = arena.compose<Node>(Node::Type::Glyph);
                mark->glyph({
                    .width = font.advance(glyph),
                    .height = body->box().height + scope.gap,
                    .depth = 0.0f,
                    .code = glyph,
                    .point = code,
                    .font = &font
                });

                // Centred over the body, then the pen put back where the body
                // left it so the mark costs no width of its own.
                auto* lead = arena.compose<Node>(Node::Type::Kern);
                lead->kern({.width = -(body->box().width + mark->glyph().width) * 0.5f});

                auto* trail = arena.compose<Node>(Node::Type::Kern);
                trail->kern({.width = (body->box().width - mark->glyph().width) * 0.5f});

                const memory::Slice<Node*> row = arena.allocate<Node*>(4);
                row[0] = body;
                row[1] = lead;
                row[2] = mark;
                row[3] = trail;
                return Line::horizontal(arena, row, 0.0f);
            }
        }

        return nullptr;
    }

    memory::Slice<Node*> Typesetter::grid(const Expression* node, const Frame& scope) const {
        if (!node || node->type != Expression::Type::Matrix) return {};
        const std::size_t columns = node->columns;
        if (columns == 0 || node->arguments.count == 0) return {};
        const std::size_t count = node->arguments.count / columns;

        const bool paired = node->grid == Expression::Grid::Paired;
        const std::string_view preamble = node->value.empty() ? std::string_view{"c"} : node->value;

        // Every cell rendered. In a paired grid the second of each pair
        // starts with the relation it lines up on -- `&= b` -- which has
        // nothing on its left inside its own cell; amsmath puts an empty
        // atom there, so the relation is spaced on both sides, and so is it
        // here.
        const memory::Slice<Node*> cells = arena.allocate<Node*>(node->arguments.count);
        for (std::size_t index = 0; index < node->arguments.count; ++index) {
            const Expression* written = node->arguments[index];
            Node* cell = enclose(render(written, scope));

            const Category edge = classify(written, false);
            if (cell && paired && index % columns % 2 == 1 && scope.style == Style::Text &&
                (edge == Category::Relation || edge == Category::Binary)) {
                auto* lead = arena.compose<Node>(Node::Type::Kern);
                lead->kern({.width = scope.quad *
                                     static_cast<float>(spacing[static_cast<std::size_t>(Category::Ordinary)]
                                                               [static_cast<std::size_t>(edge)]) / 18.0f});
                const memory::Slice<Node*> padded = arena.allocate<Node*>(2);
                padded[0] = lead;
                padded[1] = cell;
                cell = Line::horizontal(arena, padded, 0.0f);
            }
            cells[index] = cell;
        }

        // Pass one: how wide each column has to be.
        const memory::Slice<float> widths = arena.allocate<float>(columns);
        for (std::size_t index = 0; index < columns; ++index) widths[index] = 0.0f;
        for (std::size_t index = 0; index < cells.count; ++index) {
            if (const Node* cell = cells[index]) {
                widths[index % columns] = std::max(widths[index % columns], cell->box().width);
            }
        }

        // The gap after each column: a quad between any two in a matrix, and
        // in a paired grid none inside a pair and two quads between pairs.
        const auto gap = [&](const std::size_t column) {
            if (column + 1 >= columns) return 0.0f;
            if (!paired) return scope.quad;
            return column % 2 == 0 ? 0.0f : scope.quad * 2.0f;
        };

        // A strut: TeX's array rows stand at least seven tenths of a
        // baseline tall and three tenths deep, a baseline being 1.2 quads.
        const float strut = scope.quad * 1.2f;

        // Pass two: each row, its cells placed in their columns.
        const memory::Slice<Node*> built = arena.allocate<Node*>(count);
        for (std::size_t position = 0; position < count; ++position) {
            const memory::Slice<Node*> pieces = arena.allocate<Node*>(columns * 3);
            std::size_t filled = 0;

            for (std::size_t column = 0; column < columns; ++column) {
                Node* cell = cells[position * columns + column];
                const float slack = widths[column] - (cell ? cell->box().width : 0.0f);
                const char align = preamble[column % preamble.size()];
                const float before = align == 'l' ? 0.0f : align == 'r' ? slack : slack * 0.5f;

                auto* leading = arena.compose<Node>(Node::Type::Kern);
                leading->kern({.width = before});
                pieces[filled++] = leading;

                if (cell) pieces[filled++] = cell;

                auto* trailing = arena.compose<Node>(Node::Type::Kern);
                trailing->kern({.width = slack - before + gap(column)});
                pieces[filled++] = trailing;
            }

            Node* line = Line::horizontal(arena, memory::Slice{pieces.data, filled}, 0.0f);
            Node::Box shape = line->box();
            shape.height = std::max(shape.height, strut * 0.7f);
            shape.depth = std::max(shape.depth, strut * 0.3f);
            line->box(shape);
            built[position] = line;
        }

        return built;
    }

    memory::Slice<Node*> Typesetter::rows(const Expression* node, const typography::Font& font) const {
        if (!node || node->type != Expression::Type::Matrix) return {};
        return grid(node, frame(font, 0.0f, Style::Text));
    }

    memory::Slice<Pager::Page> Typesetter::compose(Document& document) const {
        document.layout();

        const memory::Slice<Document::Element*> elements = document.elements();
        if (elements.empty()) return {};

        const Document::Configuration& page = document.configuration;

        // The page's column is every block in reading order -- and a
        // paragraph's own lines opened into it one by one rather than kept as
        // one block, so the pager may end a page between any two lines, as
        // TeX does. A column hangs from its top edge, so a line opened out is
        // drawn exactly where it was drawn inside its paragraph.
        const auto opened = [](const Document::Element* element) -> const Node* {
            if (element->type != Document::Element::Type::Paragraph || !element->paragraph) return nullptr;
            const Node* box = element->paragraph->node();
            if (!box || box->type != Node::Type::Box || box->box().alignment != Node::Alignment::Vertical ||
                box->box().absolute) {
                return nullptr;
            }
            return box;
        };

        std::size_t room = 0;
        for (const Document::Element* element : elements) {
            if (!element) continue;
            const Node* lines = opened(element);
            room += (lines ? lines->box().list.count : 1) * 2 + 1;
        }

        const memory::Slice<Node*> column = arena.allocate<Node*>(room);
        std::size_t filled = 0;

        // TeX's rule between any two boxes stacked down a page: the second's
        // baseline a \baselineskip below the first's -- the leading -- unless
        // they would then come closer than \lineskip, a tenth of it, in which
        // case that much apart. Space a document or a primitive asked for,
        // a heading's or a display's, is added on top and changes nothing of
        // this; a rule across the page stops it, as TeX's prevdepth does.
        const float least = page.leading * 0.1f;
        float depth = 0.0f;
        bool previous = false;   // a box stands above, for the rule to measure from

        const auto stack = [&](Node* box) {
            if (!box) return;
            switch (box->type) {
                case Node::Type::Glue:
                case Node::Type::Kern:
                case Node::Type::Pause:
                case Node::Type::Directive:
                    column[filled++] = box;
                    return;
                case Node::Type::Rule:
                    column[filled++] = box;
                    previous = false;
                    return;
                default:
                    break;
            }

            const float height = box->type == Node::Type::Box      ? box->box().height - box->box().shift
                                 : box->type == Node::Type::Bitmap ? box->bitmap().height
                                 : box->type == Node::Type::Glyph  ? box->glyph().height
                                                                     : 0.0f;
            if (previous) {
                auto* glue = arena.compose<Node>(Node::Type::Glue);
                glue->glue({.width = std::max(page.leading - depth - height, least)});
                column[filled++] = glue;
            }
            column[filled++] = box;
            depth = box->type == Node::Type::Box ? box->box().depth + box->box().shift : 0.0f;
            previous = true;
        };

        // \\parskip, between one paragraph and the next and nowhere else.
        bool paragraph = false;
        for (const Document::Element* element : elements) {
            if (!element) continue;
            const bool text = element->type == Document::Element::Type::Paragraph;
            if (text && paragraph && page.skip > 0.0f) {
                auto* glue = arena.compose<Node>(Node::Type::Glue);
                glue->glue({.width = page.skip});
                column[filled++] = glue;
            }
            if (text || element->type != Document::Element::Type::Directive ||
                (element->node && element->node->type != Node::Type::Directive)) {
                paragraph = text;
            }
            if (const Node* lines = opened(element)) {
                // Its own interline glue is replaced by the column's, which
                // measures from the line above whatever block that was.
                for (Node* line : lines->box().list) {
                    if (line && line->type != Node::Type::Glue) stack(line);
                }
                continue;
            }
            stack(element->type == Document::Element::Type::Paragraph && element->paragraph
                      ? element->paragraph->node()
                      : element->node);
        }

        if (filled == 0) return {};
        const Node* root = Line::vertical(arena, memory::Slice{column.data, filled}, 0.0f);

        const Pager pager(arena);
        // \\skip\\footins above a page's footnotes, the rule's room in it.
        return pager.paginate(root, Pager::Context{.height = page.height - page.top - page.bottom,
                                                   .separation = page.size * 1.2f,
                                                   .width = page.width - page.left - page.right,
                                                   .gap = page.gap,
                                                   .columns = page.columns,
                                                   .placement = page.placement});
    }

}
