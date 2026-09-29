/// @file
/// @brief Illustration implementation: the `picture`/`overlay` blocks, their
///        line primitives, TikZ's pictures, and `\\includegraphics`.
#include "render/primitives/illustrations.hpp"
#include "render/primitives/colors.hpp"
#include "render/primitives/plots.hpp"
#include "render/primitives/styles.hpp"
#include "network/http.hpp"
#include "syntax/argument.hpp"
#include "syntax/number.hpp"
#include "syntax/semantics/scope.hpp"
#include "layout/line.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <system_error>
#include <vector>

namespace render::primitives {

    /// @brief Reads a run of brace-wrapped dimensions in sequence.
    ///
    /// `\begin{picture}`'s own size, `\line`'s four coordinates and
    /// `\spaceline`'s six are all this one shape read a different number of
    /// times, so this is the whole of how any of them is scanned.
    ///
    /// @param mouth     Expander to read from.
    /// @param registers Bank a dimension may resolve a register against.
    /// @param values    Filled in order; each left at 0 when its group
    ///                  did not scan as a dimension.
    /// @complexity O(n) in the values read.
    static void measure(
        syntax::Mouth& mouth,
        const syntax::semantics::Registers& registers,
        const std::span<float> values
    ) {
        for (float& value : values) {
            syntax::Token open = mouth.read();
            if (!open.is(syntax::CatCodes::Category::Group, '{') && !open.empty()) {
                mouth.stream().inject(std::span{&open, 1});
            }

            if (const auto scanned = syntax::Number::dimension(mouth, registers)) {
                value = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
            }

            syntax::Token close = mouth.read();
            if (!close.is(syntax::CatCodes::Category::Group, '}') && !close.empty()) {
                mouth.stream().inject(std::span{&close, 1});
            }
        }
    }

    /// @brief Text without the blanks around it.
    [[nodiscard]] static std::string_view trim(std::string_view text) noexcept {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.remove_suffix(1);
        return text;
    }

    /// @brief A length as TikZ writes one, in points: a number, then a unit
    ///        or none.
    /// @param text     The length, as written.
    /// @param fallback Points to a unit, when the number has no unit of its own.
    /// @param em       Points to an em, for a length in ems or exes.
    /// @return The length, or nothing when it is not one.
    [[nodiscard]] static std::optional<float> distance(std::string_view text, const float fallback,
                                                       const float em = 10.0f) noexcept {
        text = trim(text);
        float value = 0.0f;
        const auto [stop, failure] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (failure != std::errc{}) return std::nullopt;

        const std::string_view unit = trim(text.substr(static_cast<std::size_t>(stop - text.data())));
        static constexpr std::array<std::pair<std::string_view, float>, 5> units{{
            {"pt", 1.0f}, {"cm", 72.27f / 2.54f}, {"mm", 72.27f / 25.4f}, {"in", 72.27f}, {"bp", 72.27f / 72.0f},
        }};
        if (unit.empty()) return value * fallback;
        if (unit == "em") return value * em;
        if (unit == "ex") return value * em * 0.43f;
        for (const auto& [name, factor] : units) {
            if (unit == name) return value * factor;
        }
        return std::nullopt;
    }

    /// @brief A TikZ option list cut at its commas, those inside braces or
    ///        brackets left alone: `draw, -{Latex[length=2mm]}`.
    [[nodiscard]] static std::vector<std::string_view> pieces(const std::string_view list) {
        std::vector<std::string_view> found;
        int depth = 0;
        std::size_t begin = 0;
        for (std::size_t at = 0; at <= list.size(); ++at) {
            const char letter = at < list.size() ? list[at] : ',';
            if (letter == '{' || letter == '[') ++depth;
            if ((letter == '}' || letter == ']') && depth > 0) --depth;
            if (letter != ',' || depth != 0) continue;
            found.push_back(list.substr(begin, at - begin));
            begin = at + 1;
        }
        return found;
    }

    /// @brief Where an option's `=` stands, outside braces and brackets, or
    ///        npos when it has none there.
    [[nodiscard]] static std::size_t equality(const std::string_view option) noexcept {
        int depth = 0;
        for (std::size_t at = 0; at < option.size(); ++at) {
            if (option[at] == '{' || option[at] == '[') ++depth;
            if ((option[at] == '}' || option[at] == ']') && depth > 0) --depth;
            if (option[at] == '=' && depth == 0) return at;
        }
        return std::string_view::npos;
    }

    /// @brief A TikZ option list with every style it names written out in
    ///        its place, and every style it defines -- `name/.style={...}`,
    ///        `name/.append style={...}` -- kept for what follows.
    /// @param list   The options, as written.
    /// @param styles The styles known; added to.
    /// @param depth  How many styles deep this is, so a style that names
    ///               itself stops.
    /// @return The options, each style written out.
    /// @complexity O(n) in the options and the styles they name.
    static std::string expand(const std::string_view list, std::unordered_map<std::string, std::string>& styles,
                              const int depth = 0) {
        std::string written;
        for (const std::string_view piece : pieces(list)) {
            const std::string_view option = trim(piece);
            if (option.empty()) continue;
            const std::size_t equals = equality(option);
            const std::string_view key = trim(option.substr(0, equals));
            if (equals != std::string_view::npos && (key.ends_with("/.style") || key.ends_with("/.append style"))) {
                std::string_view value = trim(option.substr(equals + 1));
                if (value.starts_with('{') && value.ends_with('}')) value = value.substr(1, value.size() - 2);
                const std::string name(trim(key.substr(0, key.find('/'))));
                if (key.ends_with("/.style")) styles[name] = std::string(value);
                else styles[name] += "," + std::string(value);
                continue;
            }
            if (equals == std::string_view::npos && depth < 8) {
                if (const auto found = styles.find(std::string(option)); found != styles.end()) {
                    const std::string named = found->second;
                    written += expand(named, styles, depth + 1);
                    continue;
                }
            }
            written += option;
            written += ',';
        }
        return written;
    }

    void Illustrations::draw(syntax::Parser& parser, const std::string_view options, const std::string_view text,
                             const memory::Location origin, Context& context, const bool stroked,
                             const bool filled) const {
        graphics::Canvas& canvas = canvases.back();
        const float scale = scales.back();
        constexpr float centimetre = 72.27f / 2.54f;

        // TikZ's own defaults, whatever \line was last set to.
        graphics::Color ink = graphics::black;
        float width = 0.4f;
        bool broken = false;
        bool forward = false;
        bool backward = false;
        float step = centimetre;
        // Whether the path's outline is drawn, and whether its inside is
        // filled -- in the color `fill=` names, the path's own otherwise.
        bool stroking = stroked;
        bool painting = filled;
        std::optional<graphics::Color> tint;
        // A plot's domain and how many points it is drawn through: TikZ's
        // -5 to 5, in 25.
        double low = -5.0;
        double high = 5.0;
        int samples = 25;

        const auto fail = [this, origin](std::string message) {
            tracebacks_.emplace_back(syntax::Traceback::Type::Argument, origin, std::move(message));
        };
        // What TikZ draws and this engine does not: the picture is drawn
        // without it, and the document hears so.
        const auto miss = [this, origin](std::string message) {
            tracebacks_.emplace_back(syntax::Traceback::Type::Warning, origin, std::move(message));
        };

        static constexpr std::array<std::pair<std::string_view, float>, 7> widths{{
            {"ultra thin", 0.1f}, {"very thin", 0.2f}, {"thin", 0.4f}, {"semithick", 0.6f},
            {"thick", 0.8f}, {"very thick", 1.2f}, {"ultra thick", 1.6f},
        }};
        static constexpr std::array<std::string_view, 7> dashes{
            "dashed", "densely dashed", "loosely dashed", "dotted", "densely dotted", "loosely dotted", "dashdotted",
        };

        const std::string written = expand(options, styles);
        for (const std::string_view piece : pieces(written)) {
            const std::string_view option = trim(piece);
            if (option.empty()) continue;

            const std::size_t equals = equality(option);
            const std::string_view key = trim(option.substr(0, equals));
            const std::string_view value = equals == std::string_view::npos ? std::string_view{}
                                                                             : trim(option.substr(equals + 1));

            if (std::ranges::contains(dashes, option)) {
                broken = true;
            } else if (option == "solid") {
                broken = false;
            } else if (const auto named = std::ranges::find(widths, option, &std::pair<std::string_view, float>::first);
                       named != widths.end()) {
                width = named->second;
            } else if (option == "->" || option == "-latex" || option == "-stealth" || option == "-to") {
                forward = true;
            } else if (option == "<-" || option == "latex-" || option == "stealth-") {
                backward = true;
            } else if (option == "<->" || option == "latex-latex") {
                forward = backward = true;
            } else if (option == "-") {
                forward = backward = false;
            } else if (key == "line width") {
                if (const auto length = distance(value, 1.0f)) width = *length;
                else miss("\\draw: '" + std::string(value) + "' is not a line width");
            } else if (key == "color") {
                ink = Colors::resolve(value, context.variables);
            } else if (key == "draw") {
                stroking = true;
                if (!value.empty()) ink = Colors::resolve(value, context.variables);
            } else if (option == "rounded corners" || key == "rounded corners" || option == "sharp corners") {
                // Drawn sharp: a rounded corner's stroke runs within a
                // point or two of the sharp one's.
            } else if (key == "fill") {
                painting = true;
                if (!value.empty()) tint = Colors::resolve(value, context.variables);
            } else if (key == "opacity" || key == "draw opacity") {
                float alpha = 1.0f;
                if (std::from_chars(value.data(), value.data() + value.size(), alpha).ec == std::errc{}) {
                    ink.alpha = std::clamp(alpha, 0.0f, 1.0f);
                } else {
                    miss("\\draw: '" + std::string(value) + "' is not an opacity");
                }
            } else if (key == "step") {
                if (const auto length = distance(value, centimetre); length && *length > 0.0f) step = *length * scale;
                else miss("\\draw: '" + std::string(value) + "' is not a grid step");
            } else if (key == "domain") {
                // A plot's: over what, in how many steps, and in what name --
                // x, as a function of it is read.
                const std::size_t colon = value.find(':');
                std::from_chars(value.data(), value.data() + std::min(colon, value.size()), low);
                if (colon != std::string_view::npos) {
                    const std::string_view upper = trim(value.substr(colon + 1));
                    std::from_chars(upper.data(), upper.data() + upper.size(), high);
                }
            } else if (key == "samples") {
                std::from_chars(value.data(), value.data() + value.size(), samples);
                samples = std::clamp(samples, 2, 2000);
            } else if (key == "variable" || option == "smooth" || option == "sharp plot" || key == "mark" ||
                       key == "tension") {
                // Read and let go: a plot is drawn through its points, in
                // straight lines, whatever its variable is called.
            } else if (key == ">" || key == "shorten >" || key == "shorten <" || key == "node distance" ||
                       option == "auto" || option == "on grid" || key == "font" || key == "text" || key == "align") {
                // Read and let go: an arrow's default shape, how much a line
                // is cut short, and what only a node's text uses.
            } else if (equals == std::string_view::npos && option.find('-') != std::string_view::npos &&
                       option.find_first_not_of("-<>|{}[]=.0123456789 abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") ==
                           std::string_view::npos) {
                // An arrow written TikZ's way, its tips either side of its
                // dash: `-{Latex[length=2mm]}`, `Stealth-Stealth`, `|->`.
                int depth = 0;
                std::size_t dash = std::string_view::npos;
                for (std::size_t index = 0; index < option.size(); ++index) {
                    if (option[index] == '{' || option[index] == '[') ++depth;
                    if ((option[index] == '}' || option[index] == ']') && depth > 0) --depth;
                    if (option[index] == '-' && depth == 0) {
                        dash = index;
                        break;
                    }
                }
                const std::string_view before = option.substr(0, dash);
                const std::string_view after = dash == std::string_view::npos ? std::string_view{} : option.substr(dash + 1);
                backward = !before.empty() && before != "|";
                forward = !after.empty() && after != "|";
            } else if (equals == std::string_view::npos) {
                // Anything else a word can be is a color, and one the
                // document never named is black -- which is only right when
                // black is what it said.
                ink = Colors::resolve(option, context.variables);
                if (ink == graphics::black && !option.starts_with("black")) {
                    miss("\\draw: the option '" + std::string(option) + "' is not one this engine draws");
                }
            } else {
                miss("\\draw: the option '" + std::string(key) + "' is not one this engine draws");
            }
        }

        // The path, read left to right. `current` is where the pen is, `base`
        // what a `+` point is measured from, `start` where the path began, for
        // `cycle`; all in points, already scaled.
        graphics::Point2 current{};
        graphics::Point2 base{};
        graphics::Point2 start{};
        bool placed = false;

        std::optional<std::array<graphics::Point2, 2>> first;
        std::optional<std::array<graphics::Point2, 2>> last;
        // Each piece of the path's outline, for its fill: a move starts the
        // next.
        std::vector<std::vector<graphics::Point2>> shapes(1);
        const auto segment = [&](const graphics::Point2 from, const graphics::Point2 to) {
            if (stroking) canvas.line(from, to, ink, width, broken);
            if (!first) first = std::array{from, to};
            last = std::array{from, to};
            if (painting) {
                if (shapes.back().empty()) shapes.back().push_back(from);
                shapes.back().push_back(to);
            }
        };

        enum class Step : std::uint8_t { None, Line, Rectangle, Grid, Vertical, Horizontal };
        Step waiting = Step::None;
        std::size_t at = 0;

        const auto skip = [&] {
            while (at < text.size() && (text[at] == ' ' || text[at] == '\n')) ++at;
        };
        const auto next = [&](const std::string_view word) {
            if (text.substr(at).starts_with(word)) {
                at += word.size();
                return true;
            }
            return false;
        };

        // Where a line from a node's centre towards a point crosses the
        // node's outline: a rectangle's side, or an ellipse's curve.
        const auto meet = [](const Landmark& mark, const graphics::Point2 toward) {
            const float dx = toward.x - mark.centre.x;
            const float dy = toward.y - mark.centre.y;
            if ((dx == 0.0f && dy == 0.0f) || (mark.across <= 0.0f && mark.up <= 0.0f)) return mark.centre;
            float reach = 0.0f;
            if (mark.round) {
                reach = 1.0f / std::sqrt((dx * dx) / (mark.across * mark.across) + (dy * dy) / (mark.up * mark.up));
            } else {
                reach = std::min(dx != 0.0f ? mark.across / std::abs(dx) : std::numeric_limits<float>::max(),
                                 dy != 0.0f ? mark.up / std::abs(dy) : std::numeric_limits<float>::max());
            }
            return graphics::Point2{mark.centre.x + dx * reach, mark.centre.y + dy * reach};
        };
        // One of a node's anchors: its centre, a side or a corner by the
        // compass, or the point of its outline at an angle, `(a.30)`.
        const auto spot = [&meet](const Landmark& mark, const std::string_view anchor) -> std::optional<graphics::Point2> {
            if (anchor.empty() || anchor == "center" || anchor == "mid" || anchor == "base" || anchor == "text") {
                return mark.centre;
            }
            float dx = 0.0f;
            float dy = 0.0f;
            if (anchor.find("east") != std::string_view::npos) dx = 1.0f;
            if (anchor.find("west") != std::string_view::npos) dx = -1.0f;
            if (anchor.find("north") != std::string_view::npos) dy = 1.0f;
            if (anchor.find("south") != std::string_view::npos) dy = -1.0f;
            if (dx == 0.0f && dy == 0.0f) {
                float angle = 0.0f;
                if (std::from_chars(anchor.data(), anchor.data() + anchor.size(), angle).ec != std::errc{}) return std::nullopt;
                dx = std::cos(angle * std::numbers::pi_v<float> / 180.0f);
                dy = std::sin(angle * std::numbers::pi_v<float> / 180.0f);
            } else if (!mark.round) {
                return graphics::Point2{mark.centre.x + dx * mark.across, mark.centre.y + dy * mark.up};
            }
            return meet(mark, {mark.centre.x + dx, mark.centre.y + dy});
        };

        // One coordinate in its parentheses, `at` on the opening one: a
        // point, a polar one, or a node by its name -- whose outline a line
        // to it stops at, said by `touched` -- or one of its anchors.
        const Landmark* touched = nullptr;
        const auto coordinate = [&]() -> std::optional<graphics::Point2> {
            touched = nullptr;
            const std::size_t close = text.find(')', at);
            if (close == std::string_view::npos) return std::nullopt;
            const std::string_view inside = text.substr(at + 1, close - at - 1);
            at = close + 1;

            if (inside.find(',') == std::string_view::npos && inside.find(':') == std::string_view::npos) {
                std::string_view label = trim(inside);
                std::string_view anchor;
                auto found = landmarks.find(std::string(label));
                if (found == landmarks.end()) {
                    if (const std::size_t dot = label.rfind('.'); dot != std::string_view::npos) {
                        anchor = trim(label.substr(dot + 1));
                        label = trim(label.substr(0, dot));
                        found = landmarks.find(std::string(label));
                    }
                }
                if (found == landmarks.end()) return std::nullopt;
                if (anchor.empty()) {
                    touched = &found->second;
                    return found->second.centre;
                }
                return spot(found->second, anchor);
            }

            if (const std::size_t colon = inside.find(':'); colon != std::string_view::npos) {
                const auto angle = distance(inside.substr(0, colon), 1.0f);
                const auto radius = distance(inside.substr(colon + 1), centimetre);
                if (!angle || !radius) return std::nullopt;
                const float turn = *angle * std::numbers::pi_v<float> / 180.0f;
                return graphics::Point2{*radius * std::cos(turn) * scale, *radius * std::sin(turn) * scale};
            }
            const std::size_t comma = inside.find(',');
            if (comma == std::string_view::npos) return std::nullopt;
            const auto x = distance(inside.substr(0, comma), centimetre);
            const auto y = distance(inside.substr(comma + 1), centimetre);
            if (!x || !y) return std::nullopt;
            return graphics::Point2{*x * scale, *y * scale};
        };

        // Round a centre, from one angle to another, in degrees.
        const auto round = [&](const graphics::Point2 centre, const float radius, const float from, const float to) {
            const int pieces = std::max(8, static_cast<int>(std::abs(to - from) / 5.0f));
            graphics::Point2 previous{};
            for (int index = 0; index <= pieces; ++index) {
                const float angle = (from + (to - from) * static_cast<float>(index) / static_cast<float>(pieces)) *
                                    std::numbers::pi_v<float> / 180.0f;
                const graphics::Point2 point{centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle)};
                if (index > 0) segment(previous, point);
                previous = point;
            }
        };

        // A cubic curve from one point to another, pulled by two controls,
        // in as many strokes as keep it smooth.
        const auto bezier = [&](const graphics::Point2 from, const graphics::Point2 one, const graphics::Point2 two,
                                const graphics::Point2 to) {
            const float reach = std::hypot(one.x - from.x, one.y - from.y) + std::hypot(two.x - one.x, two.y - one.y) +
                                std::hypot(to.x - two.x, to.y - two.y);
            const int pieces = std::clamp(static_cast<int>(reach / 2.0f), 8, 96);
            graphics::Point2 previous = from;
            for (int index = 1; index <= pieces; ++index) {
                const float t = static_cast<float>(index) / static_cast<float>(pieces);
                const float u = 1.0f - t;
                const graphics::Point2 point{
                    u * u * u * from.x + 3.0f * u * u * t * one.x + 3.0f * u * t * t * two.x + t * t * t * to.x,
                    u * u * u * from.y + 3.0f * u * u * t * one.y + 3.0f * u * t * t * two.y + t * t * t * to.y};
                segment(previous, point);
                previous = point;
            }
        };

        // How the next `to` leaves its start and arrives at its end, when
        // its options bend it -- `bend left=30`, `out=0, in=180` -- in
        // degrees, and how far its controls reach, as TikZ's looseness.
        std::optional<float> bend;
        std::optional<float> leaving;
        std::optional<float> arriving;
        float looseness = 1.0f;
        // The node the pen stands at, whose outline a line leaving it starts from.
        const Landmark* resting = nullptr;

        while (true) {
            skip();
            if (at >= text.size()) break;

            // A point: absolute, or measured from the last one.
            const bool moving = next("++");
            const bool relative = moving || next("+");
            if (at < text.size() && text[at] == '(') {
                const auto read = coordinate();
                if (!read) {
                    miss("\\draw: a coordinate in '" + std::string(trim(text)) + "' is not one this engine reads");
                    return;
                }
                graphics::Point2 point = *read;
                if (relative) point = {base.x + point.x, base.y + point.y};
                const Landmark* reached = relative ? nullptr : touched;
                // A line between two nodes runs from one's outline to the
                // other's, as TikZ draws it, not from centre to centre.
                const graphics::Point2 from = resting ? meet(*resting, reached ? reached->centre : point) : current;
                const graphics::Point2 to = reached ? meet(*reached, resting ? resting->centre : current) : point;

                switch (waiting) {
                    case Step::Line:
                        if (bend || leaving || arriving) {
                            // TikZ's curve for `to`: controls at 0.3915 of
                            // the distance, times the looseness, along the
                            // angles it leaves and arrives at.
                            const float degree = std::numbers::pi_v<float> / 180.0f;
                            const float heading = std::atan2(to.y - from.y, to.x - from.x) / degree;
                            const float out = leaving ? *leaving : heading + bend.value_or(0.0f);
                            const float in = arriving ? *arriving : heading + 180.0f - bend.value_or(0.0f);
                            const float pull = 0.3915f * looseness * std::hypot(to.x - from.x, to.y - from.y);
                            bezier(from, {from.x + pull * std::cos(out * degree), from.y + pull * std::sin(out * degree)},
                                   {to.x + pull * std::cos(in * degree), to.y + pull * std::sin(in * degree)}, to);
                        } else {
                            segment(from, to);
                        }
                        break;
                    case Step::Rectangle:
                        segment(current, {point.x, current.y});
                        segment({point.x, current.y}, point);
                        segment(point, {current.x, point.y});
                        segment({current.x, point.y}, current);
                        break;
                    case Step::Vertical: {
                        const graphics::Point2 corner{current.x, point.y};
                        segment(resting ? meet(*resting, corner) : current, corner);
                        segment(corner, reached ? meet(*reached, corner) : point);
                        break;
                    }
                    case Step::Horizontal: {
                        const graphics::Point2 corner{point.x, current.y};
                        segment(resting ? meet(*resting, corner) : current, corner);
                        segment(corner, reached ? meet(*reached, corner) : point);
                        break;
                    }
                    case Step::Grid: {
                        const float left = std::min(current.x, point.x);
                        const float right = std::max(current.x, point.x);
                        const float bottom = std::min(current.y, point.y);
                        const float top = std::max(current.y, point.y);
                        for (float x = std::ceil(left / step - 0.001f) * step; x <= right + 0.001f; x += step) {
                            segment({x, bottom}, {x, top});
                        }
                        for (float y = std::ceil(bottom / step - 0.001f) * step; y <= top + 0.001f; y += step) {
                            segment({left, y}, {right, y});
                        }
                        break;
                    }
                    case Step::None:
                        start = point;
                        if (!shapes.back().empty()) shapes.emplace_back();
                        break;
                }

                current = point;
                resting = reached;
                if (!relative || moving) base = point;
                placed = true;
                waiting = Step::None;
                continue;
            }
            if (relative) {
                fail("\\draw: a '+' in '" + std::string(trim(text)) + "' stands before no coordinate");
                return;
            }

            if (next("..")) {
                // A curve: `.. controls (a) and (b) .. (c)`, one control for
                // both when `and` is left out; a control written with `+` is
                // measured from the end it pulls, as TikZ has it.
                const auto control = [&]() -> std::optional<std::pair<graphics::Point2, bool>> {
                    skip();
                    const bool plus = next("++") || next("+");
                    skip();
                    if (at >= text.size() || text[at] != '(') return std::nullopt;
                    const auto read = coordinate();
                    if (!read) return std::nullopt;
                    return std::pair{*read, plus};
                };
                skip();
                if (!next("controls")) {
                    fail("\\draw: a curve needs its controls: '.. controls (a) and (b) .. (c)'");
                    return;
                }
                const auto one = control();
                skip();
                const auto two = next("and") ? control() : one;
                skip();
                if (!one || !two || !next("..")) {
                    fail("\\draw: a curve needs its controls: '.. controls (a) and (b) .. (c)'");
                    return;
                }
                skip();
                const bool moved = next("++");
                const bool measured = moved || next("+");
                skip();
                const auto read = at < text.size() && text[at] == '(' ? coordinate() : std::nullopt;
                if (!read) {
                    fail("\\draw: a curve needs the point it ends at");
                    return;
                }
                const graphics::Point2 end = measured ? graphics::Point2{base.x + read->x, base.y + read->y} : *read;
                const graphics::Point2 pull = one->second ? graphics::Point2{current.x + one->first.x, current.y + one->first.y}
                                                          : one->first;
                const graphics::Point2 push = two->second ? graphics::Point2{end.x + two->first.x, end.y + two->first.y}
                                                          : two->first;
                bezier(current, pull, push, end);
                current = end;
                if (!measured || moved) base = end;
                placed = true;
            } else if (next("--")) {
                waiting = Step::Line;
                bend.reset();
                leaving.reset();
                arriving.reset();
            } else if (next("to")) {
                // A `to`, straight unless its options bend it.
                waiting = Step::Line;
                bend.reset();
                leaving.reset();
                arriving.reset();
                looseness = 1.0f;
                skip();
                if (at < text.size() && text[at] == '[') {
                    const std::size_t close = text.find(']', at);
                    const std::string_view list = text.substr(at + 1, (close == std::string_view::npos ? text.size() : close) - at - 1);
                    at = close == std::string_view::npos ? text.size() : close + 1;
                    for (const auto piece : std::views::split(list, ',')) {
                        const std::string_view option = trim(std::string_view(piece.begin(), piece.end()));
                        const std::size_t equals = option.find('=');
                        const std::string_view key = trim(option.substr(0, equals));
                        const std::string_view value = equals == std::string_view::npos ? std::string_view{}
                                                                                         : trim(option.substr(equals + 1));
                        float amount = 30.0f;
                        if (!value.empty()) std::from_chars(value.data(), value.data() + value.size(), amount);
                        if (key == "bend left") bend = amount;
                        if (key == "bend right") bend = -amount;
                        if (key == "out") leaving = amount;
                        if (key == "in") arriving = amount;
                        if (key == "looseness") looseness = amount;
                    }
                }
            } else if (next("|-")) {
                waiting = Step::Vertical;
            } else if (next("-|")) {
                waiting = Step::Horizontal;
            } else if (next("rectangle")) {
                waiting = Step::Rectangle;
            } else if (next("grid")) {
                waiting = Step::Grid;
            } else if (next("cycle")) {
                if (waiting == Step::Line && placed) segment(current, start);
                current = base = start;
                waiting = Step::None;
            } else if (next("plot")) {
                // TikZ's plot: its coordinates, `plot coordinates {(0,0)
                // (1,2)}`, or a function of its variable across the domain
                // the options give, `plot ({\x}, {\x*\x})`, joined by lines
                // -- to where the pen is, after a `--`.
                skip();
                if (at < text.size() && text[at] == '[') {
                    const std::size_t close = text.find(']', at);
                    at = close == std::string_view::npos ? text.size() : close + 1;
                    skip();
                }
                std::vector<graphics::Point2> points;
                if (next("coordinates")) {
                    skip();
                    const std::size_t close = at < text.size() && text[at] == '{' ? text.find('}', at) : std::string_view::npos;
                    for (++at; at < close; skip()) {
                        if (text[at] != '(') {
                            ++at;
                            continue;
                        }
                        const auto read = coordinate();
                        if (!read) break;
                        points.push_back(*read);
                    }
                    at = close == std::string_view::npos ? text.size() : close + 1;
                } else if (at < text.size() && text[at] == '(') {
                    int depth = 0;
                    std::size_t close = at;
                    std::size_t comma = std::string_view::npos;
                    for (; close < text.size(); ++close) {
                        if (text[close] == '(' || text[close] == '{') ++depth;
                        if ((text[close] == ')' || text[close] == '}') && --depth == 0) break;
                        if (text[close] == ',' && depth == 1) comma = close;
                    }
                    if (comma == std::string_view::npos || close >= text.size()) {
                        fail("\\draw: a plot needs its function as (x, y)");
                        return;
                    }
                    const std::string_view across = trim(text.substr(at + 1, comma - at - 1));
                    const std::string_view along = trim(text.substr(comma + 1, close - comma - 1));
                    at = close + 1;
                    for (int index = 0; index < samples; ++index) {
                        const double t = low + (high - low) * index / std::max(samples - 1, 1);
                        const auto x = Plots::calculate(across, t);
                        const auto y = Plots::calculate(along, t);
                        if (!x || !y) continue;
                        points.push_back({static_cast<float>(*x) * centimetre * scale,
                                          static_cast<float>(*y) * centimetre * scale});
                    }
                }
                if (points.empty()) {
                    fail("\\draw: a plot with no points this engine can work out");
                    return;
                }
                if (waiting == Step::Line && placed) segment(current, points.front());
                else if (!shapes.back().empty()) shapes.emplace_back();
                if (!placed || waiting != Step::Line) start = points.front();
                for (std::size_t index = 1; index < points.size(); ++index) segment(points[index - 1], points[index]);
                current = base = points.back();
                placed = true;
                waiting = Step::None;
            } else if (next("circle")) {
                skip();
                if (at >= text.size() || text[at] != '(') {
                    fail("\\draw: a circle needs its radius in parentheses");
                    return;
                }
                const std::size_t close = text.find(')', at);
                const auto radius = distance(text.substr(at + 1, close == std::string_view::npos ? 0 : close - at - 1),
                                             centimetre);
                if (!radius || close == std::string_view::npos) {
                    miss("\\draw: a circle's radius is not one this engine reads");
                    return;
                }
                at = close + 1;
                round(current, *radius * scale, 0.0f, 360.0f);
            } else if (next("arc")) {
                skip();
                const std::size_t close = text.find(')', at);
                if (at >= text.size() || text[at] != '(' || close == std::string_view::npos) {
                    fail("\\draw: an arc needs (start:end:radius)");
                    return;
                }
                const std::string_view inside = text.substr(at + 1, close - at - 1);
                at = close + 1;
                const std::size_t one = inside.find(':');
                const std::size_t two = one == std::string_view::npos ? one : inside.find(':', one + 1);
                const auto from = two == std::string_view::npos ? std::nullopt : distance(inside.substr(0, one), 1.0f);
                const auto to = from ? distance(inside.substr(one + 1, two - one - 1), 1.0f) : std::nullopt;
                const auto radius = to ? distance(inside.substr(two + 1), centimetre) : std::nullopt;
                if (!radius) {
                    fail("\\draw: an arc needs (start:end:radius)");
                    return;
                }
                const float reach = *radius * scale;
                const float turn = std::numbers::pi_v<float> / 180.0f;
                const graphics::Point2 centre{current.x - reach * std::cos(*from * turn),
                                              current.y - reach * std::sin(*from * turn)};
                round(centre, reach, *from, *to);
                current = base = {centre.x + reach * std::cos(*to * turn), centre.y + reach * std::sin(*to * turn)};
            } else if (next("coordinate")) {
                // A named point: where the path is, or where `at` says.
                skip();
                std::string label;
                if (at < text.size() && text[at] == '(') {
                    const std::size_t close = text.find(')', at);
                    label = std::string(trim(text.substr(at + 1, (close == std::string_view::npos ? text.size() : close) - at - 1)));
                    at = close == std::string_view::npos ? text.size() : close + 1;
                    skip();
                }
                graphics::Point2 point = current;
                if (next("at")) {
                    skip();
                    const auto read = at < text.size() && text[at] == '(' ? coordinate() : std::nullopt;
                    if (!read) {
                        fail("\\coordinate: its 'at' needs a coordinate");
                        return;
                    }
                    point = *read;
                }
                if (!label.empty()) landmarks[label] = Landmark{.centre = point};
            } else if (next("node")) {
                // A node: its options -- each style it names source out,
                // `every node`'s first -- a name a later coordinate may use,
                // where it stands, and its text in braces.
                skip();
                std::string placing;
                if (const auto every = styles.find("every node"); every != styles.end()) {
                    const std::string kept = every->second;
                    placing = expand(kept, styles);
                }
                if (at < text.size() && text[at] == '[') {
                    std::size_t close = at;
                    for (int depth = 0; close < text.size(); ++close) {
                        if (text[close] == '[' || text[close] == '{') ++depth;
                        if ((text[close] == ']' || text[close] == '}') && --depth == 0) break;
                    }
                    if (close >= text.size()) {
                        fail("\\draw: a node's options are missing their ']'");
                        return;
                    }
                    placing += expand(text.substr(at + 1, close - at - 1), styles);
                    at = close + 1;
                    skip();
                }
                std::string label;
                if (at < text.size() && text[at] == '(') {
                    const std::size_t close = text.find(')', at);
                    label = std::string(trim(text.substr(at + 1, (close == std::string_view::npos ? text.size() : close) - at - 1)));
                    at = close == std::string_view::npos ? text.size() : close + 1;
                    skip();
                }
                graphics::Point2 point = current;
                if (next("at")) {
                    skip();
                    const auto read = at < text.size() && text[at] == '(' ? coordinate() : std::nullopt;
                    if (!read) {
                        fail("\\draw: a node's 'at' needs a coordinate");
                        return;
                    }
                    point = *read;
                    skip();
                }
                if (at >= text.size() || text[at] != '{') {
                    fail("\\draw: a node needs its text in braces");
                    return;
                }
                std::size_t close = at;
                for (int depth = 0; close < text.size(); ++close) {
                    if (text[close] == '{') ++depth;
                    if (text[close] == '}' && --depth == 0) break;
                }
                const std::string_view content = text.substr(at + 1, close - at - 1);
                at = std::min(close + 1, text.size());

                // Its look and its place, from its options: an outline and a
                // fill, a shape, TikZ's inner sep of a third of an em and its
                // least sizes, a width its text is set to, and where it stands
                // -- beside another node by the positioning library's
                // `right=of init`, by the side of the point that `right` or
                // `anchor=west` names, or centred on it.
                const float em = context.selection.text() ? context.selection.text()->size() : 10.0f;
                bool outlined = false;
                bool circular = false;
                bool oval = false;
                bool centred = false;
                graphics::Color edge = graphics::black;
                std::optional<graphics::Color> inside;
                float inner = em / 3.0f;
                float least = 0.0f;
                float lowest = 0.0f;
                std::string wide;
                std::string before;
                int across = 0;
                int up = 0;
                std::string_view toward;
                std::string_view beside;
                float apart = distances.empty() ? centimetre : distances.back();
                for (const std::string_view piece : pieces(placing)) {
                    const std::string_view option = trim(piece);
                    if (option.empty()) continue;
                    const std::size_t equals = equality(option);
                    const std::string_view key = trim(option.substr(0, equals));
                    std::string_view value = equals == std::string_view::npos ? std::string_view{} : trim(option.substr(equals + 1));
                    if (value.starts_with('{') && value.ends_with('}')) value = value.substr(1, value.size() - 2);
                    const bool side = key == "right" || key == "left" || key == "above" || key == "below" ||
                                      key == "above right" || key == "above left" || key == "below right" ||
                                      key == "below left";
                    if (key == "draw") {
                        outlined = true;
                        if (!value.empty()) edge = Colors::resolve(value, context.variables);
                    } else if (key == "fill") {
                        inside = Colors::resolve(value.empty() ? std::string_view{"black"} : value, context.variables);
                    } else if (option == "circle" || value == "circle") {
                        circular = true;
                    } else if (option == "ellipse" || value == "ellipse") {
                        circular = oval = true;
                    } else if (key == "inner sep") {
                        inner = distance(value, 1.0f, em).value_or(inner);
                    } else if (key == "minimum width") {
                        least = distance(value, 1.0f, em).value_or(least);
                    } else if (key == "minimum height") {
                        lowest = distance(value, 1.0f, em).value_or(lowest);
                    } else if (key == "minimum size") {
                        least = lowest = distance(value, 1.0f, em).value_or(least);
                    } else if (key == "text width") {
                        wide = std::string(value);
                    } else if (key == "font") {
                        before += std::string(value) + " ";
                    } else if (key == "text" || key == "color") {
                        before += "\\color{" + std::string(value) + "}";
                    } else if (key == "align" || option == "text centered" || option == "text badly centered") {
                        centred = key != "align" || value == "center";
                    } else if (side && equals != std::string_view::npos) {
                        const std::size_t of = value.find("of ");
                        if (of != std::string_view::npos) {
                            toward = key;
                            beside = trim(value.substr(of + 3));
                            if (of > 0) apart = distance(value.substr(0, std::min(of, value.find(" and "))), 1.0f, em).value_or(apart);
                        }
                    } else if (key == "anchor") {
                        if (value.find("west") != std::string_view::npos) across = -1;
                        if (value.find("east") != std::string_view::npos) across = 1;
                        if (value.find("south") != std::string_view::npos) up = -1;
                        if (value.find("north") != std::string_view::npos) up = 1;
                    } else if (option == "midway" || option == "pos=0.5" || option == "pos=.5" || option == "near start" ||
                               option == "near end" || option == "at start" || option == "at end") {
                        const float share = option == "near start" ? 0.25f : option == "near end" ? 0.75f
                                            : option == "at start" ? 0.0f   : option == "at end"   ? 1.0f : 0.5f;
                        if (last) {
                            point = {(*last)[0].x + ((*last)[1].x - (*last)[0].x) * share,
                                     (*last)[0].y + ((*last)[1].y - (*last)[0].y) * share};
                        }
                    } else if (equals == std::string_view::npos) {
                        if (option.find("right") != std::string_view::npos) across = -1;
                        if (option.find("left") != std::string_view::npos) across = 1;
                        if (option.find("above") != std::string_view::npos) up = -1;
                        if (option.find("below") != std::string_view::npos) up = 1;
                    }
                }

                // The node's text, typeset as text is, formulas included,
                // into a horizontal box -- set in a paragraph of its own when
                // it has a width -- and none when it sets nothing.
                std::string source = std::string(content);
                if (!wide.empty()) source = "\\parbox{" + wide + "}{" + (centred ? "\\centering " : "") + source + "}";
                source = before + source;
                layout::Node* setting = [&]() -> layout::Node* {
                    syntax::Mouth& mouth = parser.mouth();
                    memory::Arena& arena = parser.arena();
                    mouth.ingest(arena.copy("{" + source + "}"));
                    mouth.read();

                    mouth.push(syntax::semantics::Scope::Type::Group);
                    const typography::Font* restore = context.selection.text();
                    const memory::Slice<syntax::Node*> read = parser.parse('}');
                    stamp(read, context);
                    context.selection.text(restore);
                    mouth.pop(syntax::semantics::Scope::Type::Group);

                    std::vector<layout::Node*> set;
                    for (const syntax::Node* child : read) gather(set, child, context);
                    if (set.empty()) return static_cast<layout::Node*>(nullptr);
                    const memory::Slice<layout::Node*> row = arena.allocate<layout::Node*>(set.size());
                    std::ranges::copy(set, row.begin());
                    return layout::Line::horizontal(arena, row, 0.0f);
                }();

                // Its outline's half sizes: the text and its inner sep, or its
                // least size; a circle circular the text's corners, an ellipse
                // through them.
                const float breadth = setting ? setting->box().width : 0.0f;
                const float height = setting ? setting->box().height + setting->box().depth : 0.0f;
                float half = std::max(breadth * 0.5f + inner, least * 0.5f);
                float rise = std::max(height * 0.5f + inner, lowest * 0.5f);
                if (circular && !oval) half = rise = std::max(std::hypot(breadth * 0.5f, height * 0.5f) + inner, std::max(least, lowest) * 0.5f);
                if (oval) {
                    half = std::max((breadth * 0.5f + inner) * std::numbers::sqrt2_v<float>, least * 0.5f);
                    rise = std::max((height * 0.5f + inner) * std::numbers::sqrt2_v<float>, lowest * 0.5f);
                }

                graphics::Point2 centre{point.x - static_cast<float>(across) * half, point.y - static_cast<float>(up) * rise};
                if (!toward.empty()) {
                    if (const auto found = landmarks.find(std::string(beside)); found != landmarks.end()) {
                        const Landmark& other = found->second;
                        centre = other.centre;
                        if (toward.find("right") != std::string_view::npos) centre.x += other.across + apart + half;
                        if (toward.find("left") != std::string_view::npos) centre.x -= other.across + apart + half;
                        if (toward.find("above") != std::string_view::npos) centre.y += other.up + apart + rise;
                        if (toward.find("below") != std::string_view::npos) centre.y -= other.up + apart + rise;
                    } else {
                        miss("\\draw: no node named '" + std::string(beside) + "' to set a node beside");
                    }
                }

                // Its outline, filled and then drawn, under its text.
                if (outlined || inside) {
                    std::vector<graphics::Point2> outline;
                    if (circular) {
                        for (int index = 0; index < 48; ++index) {
                            const float angle = static_cast<float>(index) * std::numbers::pi_v<float> / 24.0f;
                            outline.push_back({centre.x + half * std::cos(angle), centre.y + rise * std::sin(angle)});
                        }
                    } else {
                        outline = {{centre.x - half, centre.y - rise}, {centre.x + half, centre.y - rise},
                                   {centre.x + half, centre.y + rise}, {centre.x - half, centre.y + rise}};
                    }
                    if (inside) canvas.fill(outline, *inside);
                    if (outlined) {
                        for (std::size_t index = 0; index < outline.size(); ++index) {
                            canvas.line(outline[index], outline[(index + 1) % outline.size()], edge, width, broken);
                        }
                    }
                }
                canvas.place(centre, setting, 0, 0, 0.0f);
                if (!label.empty()) landmarks[label] = Landmark{.centre = centre, .across = half, .up = rise, .round = circular};
            } else {
                miss("\\draw: '" + std::string(trim(text.substr(at))) + "' is not a path this engine draws");
                return;
            }
        }

        // Arrow tips: two short strokes back from the end, a quarter turn
        // apart, as long as the line is heavy.
        const auto tip = [&](const graphics::Point2 from, const graphics::Point2 to) {
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float length = std::hypot(dx, dy);
            if (length <= 0.0f) return;
            const float reach = 3.0f + 2.5f * width;
            const float angle = std::atan2(dy, dx);
            for (const float side : {-0.45f, 0.45f}) {
                const graphics::Point2 barb{to.x - reach * std::cos(angle + side), to.y - reach * std::sin(angle + side)};
                canvas.line(to, barb, ink, width, false);
            }
        };
        if (forward && last) tip((*last)[0], (*last)[1]);
        if (backward && first) tip((*first)[1], (*first)[0]);

        // The inside of each piece of the outline, filled.
        if (painting) {
            for (const std::vector<graphics::Point2>& shape : shapes) canvas.fill(shape, tint.value_or(ink));
        }
    }

    Illustrations::Illustrations(syntax::Lexicon& lexicon) noexcept {
        // "picture" itself is interned by Blocks::watch() below; a block's
        // name is its own table, not this one.
        lexicon.intern("\\line");
        lexicon.intern("\\spaceline");
        lexicon.intern("\\linecolor");
        lexicon.intern("\\lineopacity");
        lexicon.intern("\\linestyle");
        lexicon.intern("\\lineweight");
        lexicon.intern("\\planar");
        lexicon.intern("\\isometric");
        lexicon.intern("\\orthographic");
        lexicon.intern("\\includegraphics");
        lexicon.intern("\\draw");
        lexicon.intern(flush);
    }

    void Illustrations::operator()(syntax::Parser& parser, Context& context) const {
        // Shared by both blocks: build whatever is open, anchor it to the
        // page when it was an overlay, then hand it to flush the same way
        // either kind closes.
        const syntax::Mouth::Handler leaving = [this, &context](syntax::Mouth& mouth) {
            if (canvases.empty()) return;

            const graphics::Canvas::Room room = rooms.back();
            layout::Node* built = canvases.back().compose(context.arena, room);
            if (built && room == graphics::Canvas::Room::Declared) {
                layout::Node::Box shape = built->box();
                shape.anchored = true;
                shape.anchor = anchors.back();
                built->box(shape);
            }

            canvases.pop_back();
            rooms.pop_back();
            anchors.pop_back();
            scales.pop_back();
            if (!distances.empty()) distances.pop_back();
            if (canvases.empty()) landmarks.clear();
            pending = built;
            mouth.ingest(flush);
        };

        context.blocks.watch(
            "picture",
            [this, &context](syntax::Mouth& mouth) {
                std::array<float, 2> size{};
                measure(mouth, context.registers, size);

                canvases.emplace_back(size[0], size[1]);
                rooms.push_back(graphics::Canvas::Room::Grown);
                anchors.push_back({});
                scales.push_back(1.0f);
                distances.push_back(72.27f / 2.54f);
                color = graphics::black;
                weight = 1.0f;
                dashed = false;
                view = graphics::Projection::isometric();
            },
            leaving);

        context.blocks.watch(
            "overlay",
            [this, &context](syntax::Mouth& mouth) {
                std::array<float, 4> placement{};
                measure(mouth, context.registers, placement);

                canvases.emplace_back(placement[2], placement[3]);
                rooms.push_back(graphics::Canvas::Room::Declared);
                anchors.push_back({placement[0], placement[1]});
                scales.push_back(1.0f);
                distances.push_back(72.27f / 2.54f);
                color = graphics::black;
                weight = 1.0f;
                dashed = false;
                view = graphics::Projection::isometric();
            },
            leaving);

        // TikZ's picture: no size of its own, only its drawing's, and TikZ's
        // own defaults for a stroke. `scale=` in its options scales every
        // TikZ coordinate inside it, `node distance=` sets how far apart
        // the positioning library puts nodes, and a style it defines --
        // `block/.style={...}` -- holds from there on.
        context.blocks.watch(
            "tikzpicture",
            [this](syntax::Mouth& mouth) {
                float scale = 1.0f;
                float apart = 72.27f / 2.54f;
                std::string options;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    options += token.text;
                    if (token.text.size() > 1 && token.text.front() == '\\') options += ' ';
                }
                const std::string written = expand(options, styles);
                for (const std::string_view piece : pieces(written)) {
                    std::string_view option = trim(piece);
                    if (option.starts_with("scale=")) {
                        option.remove_prefix(6);
                        if (std::from_chars(option.data(), option.data() + option.size(), scale).ec != std::errc{}) {
                            scale = 1.0f;
                        }
                    } else if (option.starts_with("node distance=")) {
                        option.remove_prefix(14);
                        apart = distance(option.substr(0, option.find(" and ")), 72.27f / 2.54f).value_or(apart);
                    }
                }

                canvases.emplace_back(0.0f, 0.0f);
                rooms.push_back(graphics::Canvas::Room::Drawn);
                anchors.push_back({});
                scales.push_back(scale);
                distances.push_back(apart);
                color = graphics::black;
                weight = 0.4f;
                dashed = false;
                view = graphics::Projection::isometric();
            },
            leaving);

        // A path, up to its semicolon, with every macro in it expanded first
        // -- so a coordinate may come from \variable or \evaluate -- and
        // braces kept, so a semicolon inside a node's text does not end it.
        // A control word keeps the space that ended it, so a node's text
        // reads back as the tokens it was.
        const auto path = [](syntax::Mouth& mouth, std::string& options) {
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                options += token.text;
            }
            std::string text;
            int depth = 0;
            for (syntax::Token token = mouth.expand(); !token.empty(); token = mouth.expand()) {
                if (depth == 0 && token.is(syntax::CatCodes::Category::Other, ';')) break;
                if (token.is(syntax::CatCodes::Category::Group, '{')) ++depth;
                if (token.is(syntax::CatCodes::Category::Group, '}')) --depth;
                text += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
            }
            return text;
        };

        // \draw strokes its path, \fill fills its inside, \filldraw does
        // both, and \path neither, setting only its nodes -- unless their
        // options say `draw` or `fill`.
        for (const auto& [name, stroked, filled] :
             {std::tuple{"\\draw", true, false}, std::tuple{"\\fill", false, true},
              std::tuple{"\\filldraw", true, true}, std::tuple{"\\path", false, false}}) {
            parser.bind(name, [this, &context, path, stroked, filled, name](syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth();
                const memory::Location origin = mouth.lookahead().location;
                std::string options;
                const std::string text = path(mouth, options);
                if (canvases.empty()) {
                    tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                             std::string(name) + " needs an open picture");
                    return nullptr;
                }
                draw(parser, options, text, origin, context, stroked, filled);
                return nullptr;
            });
        }

        // \node[options] (name) at (x,y) {text}; -- a path of one node.
        parser.bind("\\node", [this, &context, path](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            const memory::Location origin = mouth.lookahead().location;
            std::string options;
            const std::string text = path(mouth, options);
            if (canvases.empty()) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, origin, "\\node needs an open picture");
                return nullptr;
            }
            draw(parser, "", "node[" + options + "] " + text, origin, context, false, false);
            return nullptr;
        });

        // A named point: `\\coordinate (a) at (1,2);`, a path of one.
        parser.bind("\\coordinate", [this, &context, path](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            const memory::Location origin = mouth.lookahead().location;
            std::string options;
            const std::string text = path(mouth, options);
            if (canvases.empty()) return nullptr;
            draw(parser, "", "coordinate " + text, origin, context, false, false);
            return nullptr;
        });

        // TikZ's styles, named once and used by name after: `\\tikzset{
        // block/.style={draw, fill=blue!10}}`, and the older
        // `\\tikzstyle{block}=[draw]`.
        parser.mouth().bind("\\tikzset", [this](syntax::Mouth& mouth) {
            std::string options;
            for (const syntax::Token& token : mouth.argument({}, 1)) {
                options += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') options += ' ';
            }
            static_cast<void>(expand(options, styles));
        });
        parser.mouth().bind("\\tikzstyle", [this](syntax::Mouth& mouth) {
            const std::string name = syntax::Argument::text(mouth);
            if (mouth.lookahead().is('=')) mouth.read();
            std::string options;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                options += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') options += ' ';
            }
            styles[name] = options;
        });

        parser.mouth().bind("\\linecolor", [this, &context](syntax::Mouth& mouth) {
            const float carried = color.alpha;
            color = Colors::resolve(syntax::Argument::text(mouth), context.variables);
            // A color written without its own alpha -- a hex triplet, a
            // name -- keeps whatever \lineopacity last set, rather than
            // silently opaquing a stroke that was made translucent first.
            if (color.alpha >= 1.0f) color.alpha = carried;
        });

        parser.mouth().bind("\\lineopacity", [this](syntax::Mouth& mouth) {
            const std::string text = syntax::Argument::text(mouth);
            float value = color.alpha;
            if (const auto [stop, failure] =
                    std::from_chars(text.data(), text.data() + text.size(), value);
                failure == std::errc{}) {
                color.alpha = value;
            }
        });

        parser.mouth().bind("\\linestyle", [this](syntax::Mouth& mouth) {
            dashed = syntax::Argument::text(mouth) == "dashed";
        });

        parser.mouth().bind("\\lineweight", [this, &context](syntax::Mouth& mouth) {
            measure(mouth, context.registers, std::span{&weight, 1});
        });

        parser.mouth().bind("\\planar", [this](syntax::Mouth&) { view = graphics::Projection::planar(); });
        parser.mouth().bind("\\isometric", [this](syntax::Mouth&) { view = graphics::Projection::isometric(); });

        parser.mouth().bind("\\orthographic", [this, &context](syntax::Mouth& mouth) {
            std::array<std::int32_t, 2> angles{};
            for (std::int32_t& angle : angles) {
                syntax::Token open = mouth.read();
                if (!open.is(syntax::CatCodes::Category::Group, '{') && !open.empty()) {
                    mouth.stream().inject(std::span{&open, 1});
                }
                if (const auto scanned = syntax::Number::integer(mouth, context.registers)) {
                    angle = *scanned;
                }
                syntax::Token close = mouth.read();
                if (!close.is(syntax::CatCodes::Category::Group, '}') && !close.empty()) {
                    mouth.stream().inject(std::span{&close, 1});
                }
            }
            view = graphics::Projection::orthographic(
                static_cast<float>(angles[0]), static_cast<float>(angles[1]));
        });

        parser.mouth().bind("\\line", [this, &context](syntax::Mouth& mouth) {
            if (canvases.empty()) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, mouth.lookahead().location,
                                         "\\line needs an open picture");
                return;
            }

            std::array<float, 4> coordinates{};
            measure(mouth, context.registers, coordinates);

            canvases.back().line(
                {coordinates[0], coordinates[1]}, {coordinates[2], coordinates[3]},
                color, weight, dashed);
        });

        parser.mouth().bind("\\spaceline", [this, &context](syntax::Mouth& mouth) {
            if (canvases.empty()) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, mouth.lookahead().location,
                                         "\\spaceline needs an open picture");
                return;
            }

            std::array<float, 6> coordinates{};
            measure(mouth, context.registers, coordinates);

            canvases.back().line(
                {coordinates[0], coordinates[1], coordinates[2]},
                {coordinates[3], coordinates[4], coordinates[5]},
                view, color, weight, dashed);
        });

        // graphicx's \graphicspath{{figures/}{plots/}}: the folders a picture
        // named without one is looked for in, after the document's own.
        parser.mouth().bind("\\graphicspath", [this](syntax::Mouth& mouth) {
            folders.clear();
            std::string folder;
            std::size_t depth = 0;
            for (const syntax::Token& token : mouth.argument({}, 0)) {
                if (token.is(syntax::CatCodes::Category::Group, '{') && depth++ == 0) continue;
                if (token.is(syntax::CatCodes::Category::Group, '}') && --depth == 0) {
                    if (!folder.empty() && !folder.ends_with('/')) folder += '/';
                    if (!folder.empty()) folders.push_back(std::move(folder));
                    folder.clear();
                    continue;
                }
                folder += token.text;
            }
        });

        parser.bind("\\includegraphics", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            // LaTeX's own form first, `\includegraphics[width=5cm]{file}`: the
            // keys in brackets, and the size worked out from them once the
            // picture is decoded and its proportions known. The engine's own
            // form, the file then its width and height in braces, still reads
            // exactly as it did.
            std::string keys;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                keys += token.text;
            }
            const std::string source = syntax::Argument::text(mouth);

            std::array<float, 2> size{};
            const bool given = mouth.lookahead().is(syntax::CatCodes::Category::Group, '{');
            if (given) measure(mouth, context.registers, size);

            // `plot.png`, or the name with the extensions LaTeX tries for a
            // name written without one. A picture handed in from memory comes
            // before one on disk or on the network: a chart the calling
            // program drew a moment ago never has to be written anywhere to be
            // placed, and is read where it already lies rather than copied
            // first. Then beside the document, in its own folder first and
            // then in each \graphicspath gave.
            // The forms it draws come first; a PDF, PostScript or SVG file
            // is found after them, to have its place kept.
            static constexpr std::array<std::string_view, 9> extensions{
                "", ".png", ".jpg", ".jpeg", ".webp", ".pdf", ".eps", ".ps", ".svg",
            };
            std::span<const std::byte> encoded;
            for (std::size_t tried = 0; context.files && tried < extensions.size() && encoded.empty(); ++tried) {
                if (const auto file = context.files->find(source + std::string(extensions[tried]));
                    file != context.files->end()) {
                    encoded = std::as_bytes(std::span{file->second});
                }
            }
            for (std::size_t folder = 0; context.disk && folder <= folders.size() && encoded.empty(); ++folder) {
                const std::string prefix = folder == 0 ? std::string{} : folders[folder - 1];
                for (const std::string_view extension : extensions) {
                    if (const std::string* file = context.disk(prefix + source + std::string(extension))) {
                        encoded = std::as_bytes(std::span{*file});
                        break;
                    }
                }
            }

            std::optional<std::vector<std::uint8_t>> received;
            if (encoded.empty() && (source.starts_with("http://") || source.starts_with("https://"))) {
                received = network::get(source, "");
                if (received) encoded = std::as_bytes(std::span{*received});
            }
            if (encoded.empty()) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                         "\\includegraphics could not read '" + source + "'");
                return directive(arena, nullptr, origin, true);
            }

            // The keys that say which part of a PDF is drawn: its page, and
            // the window onto that page -- `viewport=` from its box's corner,
            // `trim=` off each side -- in big points unless a unit is given,
            // the rest clipped away as `clip` would.
            const auto trim = [](std::string_view text) {
                while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
                while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
                return text;
            };
            int leaf = 1;
            std::array<float, 4> window{};
            bool windowed = false;
            bool trimmed = false;
            for (std::size_t start = 0; start <= keys.size();) {
                std::size_t stop = keys.find(',', start);
                if (stop == std::string::npos) stop = keys.size();
                const std::string_view item = trim(std::string_view(keys).substr(start, stop - start));
                start = stop + 1;
                const std::size_t equals = item.find('=');
                if (equals == std::string_view::npos) continue;
                const std::string_view key = trim(item.substr(0, equals));
                const std::string_view value = trim(item.substr(equals + 1));
                if (key == "page") {
                    std::from_chars(value.data(), value.data() + value.size(), leaf);
                } else if (key == "viewport" || key == "trim") {
                    std::size_t corner = 0;
                    for (const auto piece : std::views::split(value, ' ')) {
                        const std::string_view part(piece.begin(), piece.end());
                        if (part.empty() || corner >= 4) continue;
                        float amount = 0.0f;
                        const auto [end, fault] = std::from_chars(part.data(), part.data() + part.size(), amount);
                        if (fault == std::errc{} && end != part.data() + part.size()) {
                            amount = primitives::measure(part, mouth, context) * 72.0f / 72.27f;
                        }
                        window[corner++] = amount;
                    }
                    windowed = corner == 4;
                    trimmed = key == "trim";
                }
            }

            // A picture drawn again -- a logo on every page -- is the one
            // already decoded, and so embedded once; a PDF's page, the page
            // already read. Bytes from the network live only as long as this
            // call, and are never kept by where they lay.
            const graphics::Image* stored = nullptr;
            const graphics::Drawing* drawn = nullptr;
            const auto sheet = sheets.find(encoded.data());
            if (const auto kept = pictures.find(encoded.data()); kept != pictures.end() && !received) {
                stored = kept->second;
            } else if (sheet != sheets.end() && !received &&
                       std::ranges::any_of(sheet->second, [leaf](const auto& read) { return read.first == leaf; })) {
                drawn = std::ranges::find(sheet->second, leaf, &std::pair<int, const graphics::Drawing*>::first)->second;
            } else if (std::optional<graphics::Image> decoded = graphics::Image::decode(encoded)) {
                images.push_back(std::move(*decoded));
                stored = &images.back();
                if (!received) pictures.emplace(encoded.data(), stored);
            } else if (std::optional<graphics::Drawing> page = graphics::Drawing::decode(encoded, leaf)) {
                // A PDF: its page drawn as a form, as pdfTeX draws one.
                drawings.push_back(std::move(*page));
                drawn = &drawings.back();
                if (!received) sheets[encoded.data()].emplace_back(leaf, drawn);
            } else {
                // A picture in a form this engine does not draw --
                // PostScript, SVG -- leaves its place framed and empty, as
                // wide and as tall as its keys ask, and says so.
                tracebacks_.emplace_back(
                    syntax::Traceback::Type::Warning, origin,
                    "\\includegraphics: '" + source + "' is in a form this engine does not draw; its place is kept");
                std::string width = given ? std::format("{:.2f}pt", size[0]) : std::string("0.5\\linewidth");
                std::string height = given ? std::format("{:.2f}pt", size[1]) : std::string{};
                for (std::size_t start = 0; start <= keys.size();) {
                    std::size_t stop = keys.find(',', start);
                    if (stop == std::string::npos) stop = keys.size();
                    const std::string item = keys.substr(start, stop - start);
                    start = stop + 1;
                    const std::size_t equals = item.find('=');
                    if (equals == std::string::npos) continue;
                    std::string key = item.substr(0, equals);
                    std::erase(key, ' ');
                    if (key == "width") width = item.substr(equals + 1);
                    if (key == "height" || key == "totalheight") height = item.substr(equals + 1);
                }
                if (height.empty()) height = "\\dimexpr(" + width + ")*3/4\\relax";
                mouth.ingest(arena.copy("\\fbox{\\makebox[" + width + "]{\\rule{0pt}{" + height + "}}}"));
                return directive(arena, nullptr, origin, true);
            }

            // The part of a PDF's page shown, in its own points; the whole
            // box unless the keys chose less.
            if (drawn) {
                const std::array<float, 4>& box = drawn->box();
                if (!windowed) {
                    window = box;
                } else if (trimmed) {
                    window = {box[0] + window[0], box[1] + window[1], box[2] - window[2], box[3] - window[3]};
                } else {
                    window = {box[0] + window[0], box[1] + window[1], box[0] + window[2], box[1] + window[3]};
                }
                if (window[2] <= window[0] || window[3] <= window[1]) window = box;
            }

            // Its own size in points: a picture's pixels at the resolution
            // its file gives, a point each when it gives none; a PDF page's
            // window onto it.
            const float point = stored ? 72.0f / stored->resolution() : 1.0f;
            const std::array<float, 2> natural =
                stored ? std::array{static_cast<float>(stored->width()) * point, static_cast<float>(stored->height()) * point}
                       : std::array{window[2] - window[0], window[3] - window[1]};

            if (!given) {
                // The size the keys ask for, against the picture's own:
                // `width=`, `height=`, `scale=`, `keepaspectratio`.
                size = [&, list = std::string_view(keys)]() -> std::array<float, 2> {

                    // A length, read as any dimension is: `0.8\\linewidth` is a
                    // fraction of the line the picture stands in -- a column's,
                    // a minipage's -- and `\\textwidth` of the page's.
                    const auto length = [&](const std::string_view value) -> float {
                        return primitives::measure(value, mouth, context);
                    };

                    float width = 0.0f;
                    float height = 0.0f;
                    float factor = 1.0f;
                    bool fitted = false;

                    std::size_t start = 0;
                    while (start <= list.size()) {
                        std::size_t stop = list.find(',', start);
                        if (stop == std::string_view::npos) stop = list.size();
                        const std::string_view item = trim(list.substr(start, stop - start));
                        start = stop + 1;
                        if (item.empty()) continue;

                        const std::size_t equals = item.find('=');
                        const std::string_view key = trim(item.substr(0, equals));
                        const std::string_view value = equals == std::string_view::npos ? std::string_view{}
                                                                                         : trim(item.substr(equals + 1));

                        if (key == "width") {
                            width = length(value);
                        } else if (key == "height" || key == "totalheight") {
                            height = length(value);
                        } else if (key == "scale") {
                            if (std::from_chars(value.data(), value.data() + value.size(), factor).ec != std::errc{}) factor = 1.0f;
                        } else if (key == "keepaspectratio") {
                            fitted = true;
                        } else if (key == "page" || key == "viewport" || key == "trim" || key == "clip") {
                            // Read above: which part of a PDF is drawn.
                        } else {
                            tracebacks_.emplace_back(syntax::Traceback::Type::Warning, mouth.lookahead().location,
                                                     "\\includegraphics: the '" + std::string(key) + "' key is not supported");
                        }
                    }

                    if (natural[0] <= 0.0f || natural[1] <= 0.0f) return {width, height};

                    // One side given: the other keeps the picture's proportions. Both,
                    // with keepaspectratio: the largest size that fits inside them.
                    if (width > 0.0f && height > 0.0f && fitted) {
                        const float ratio = std::min(width / natural[0], height / natural[1]);
                        return {natural[0] * ratio, natural[1] * ratio};
                    }
                    if (width > 0.0f && height <= 0.0f) return {width, width * natural[1] / natural[0]};
                    if (height > 0.0f && width <= 0.0f) return {height * natural[0] / natural[1], height};
                    if (width > 0.0f && height > 0.0f) return {width, height};
                    return {natural[0] * factor, natural[1] * factor};
                }();
            }

            auto* picture = arena.compose<layout::Node>();
            picture->bitmap({.width = size[0], .height = size[1], .source = stored, .drawing = drawn, .window = window});
            return directive(arena, picture, origin, true);
        });

        // Handed to the document through a parser-level primitive rather
        // than appended directly from the Mouth-level leaving hook above, so
        // the picture lands where its block stood in the parse, by the same
        // clock every other node does.
        parser.bind(flush, [this](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena();
            const memory::Location origin = parser.mouth().lookahead().location;
            layout::Node* built = pending;
            pending = nullptr;
            // A picture stands in the line, as LaTeX's boxes do -- two side by
            // side with `\hfill` between, one centred by `\centering` -- and
            // an overlay, pinned to the page, apart from it.
            const bool pinned = built && built->type() == layout::Node::Type::Box && built->box().anchored;
            return directive(arena, built, origin, pinned);
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound picture primitives");
    }

}
