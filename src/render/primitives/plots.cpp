/// @file
/// @brief pgfplots: an axis and its plots, worked out as the axis closes and
///        drawn with TikZ's own `\\draw` and `\\node` in the picture around it.
#include "render/primitives/plots.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace render::primitives {

    std::optional<double> Plots::calculate(const std::string_view text, const double x, bool* const measured) {
        constexpr double degree = std::numbers::pi / 180.0;
        std::size_t at = 0;
        bool broken = false;
        const auto blank = [&] {
            while (at < text.size() && text[at] == ' ') ++at;
        };

        // By precedence, loosest first: 0 sums, 1 products, 2 a power --
        // which binds to the right, and tighter than a sign before it, so
        // -x^2 is -(x^2) as pgfplots reads it -- and 3 a value.
        const auto value = [&](this const auto& self, const int level) -> double {
            if (broken) return 0.0;
            if (level < 2) {
                double left = self(level + 1);
                for (blank(); at < text.size(); blank()) {
                    const char sign = text[at];
                    if (level == 0 ? sign != '+' && sign != '-' : sign != '*' && sign != '/') break;
                    ++at;
                    const double right = self(level + 1);
                    left = sign == '+' ? left + right : sign == '-' ? left - right : sign == '*' ? left * right
                                                                                              : left / right;
                }
                return left;
            }
            if (level == 2) {
                double base = self(3);
                blank();
                // TikZ's `r` after a value: radians, turned to the degrees
                // the functions take -- `sin(\x r)`.
                if (at < text.size() && text[at] == 'r' &&
                    (at + 1 >= text.size() || !((text[at + 1] >= 'a' && text[at + 1] <= 'z') || (text[at + 1] >= 'A' && text[at + 1] <= 'Z')))) {
                    ++at;
                    base /= degree;
                    blank();
                }
                if (at >= text.size() || text[at] != '^') return base;
                ++at;
                return std::pow(base, self(2));
            }

            blank();
            if (at >= text.size()) {
                broken = true;
                return 0.0;
            }
            const char head = text[at];
            if (head == '-' || head == '+') {
                ++at;
                const double inner = self(2);
                return head == '-' ? -inner : inner;
            }
            if (head == '(' || head == '{') {
                ++at;
                const double inner = self(0);
                blank();
                if (at < text.size() && (text[at] == ')' || text[at] == '}')) ++at;
                else broken = true;
                return inner;
            }
            if ((head >= '0' && head <= '9') || head == '.') {
                double number = 0.0;
                const auto [stop, failure] = std::from_chars(text.data() + at, text.data() + text.size(), number);
                if (failure != std::errc{}) {
                    broken = true;
                    return 0.0;
                }
                at = static_cast<std::size_t>(stop - text.data());

                // A unit after it, as TikZ's formulas take one: the length in
                // points, and the whole formula a length -- `\x1+1cm`. An em
                // is ten points here, an ex 4.3.
                static constexpr std::array<std::pair<std::string_view, double>, 11> units{{
                    {"pt", 1.0}, {"cm", 72.27 / 2.54}, {"mm", 72.27 / 25.4}, {"in", 72.27}, {"bp", 72.27 / 72.0},
                    {"pc", 12.0}, {"dd", 1238.0 / 1157.0}, {"cc", 12.0 * 1238.0 / 1157.0}, {"sp", 1.0 / 65536.0},
                    {"em", 10.0}, {"ex", 4.3},
                }};
                std::size_t after = at;
                while (after < text.size() && text[after] == ' ') ++after;
                for (const auto& [name, points] : units) {
                    const std::size_t end = after + name.size();
                    if (text.substr(after, name.size()) != name) continue;
                    if (end < text.size() && ((text[end] >= 'a' && text[end] <= 'z') || (text[end] >= 'A' && text[end] <= 'Z'))) {
                        continue;
                    }
                    at = end;
                    if (measured) *measured = true;
                    return number * points;
                }
                return number;
            }

            // A name: x -- or TikZ's \x -- pi, e, or a function of one
            // argument or two.
            if (head == '\\') ++at;
            const std::size_t start = at;
            while (at < text.size() && ((text[at] >= 'a' && text[at] <= 'z') || (text[at] >= 'A' && text[at] <= 'Z') ||
                                        (text[at] >= '0' && text[at] <= '9'))) {
                ++at;
            }
            const std::string_view name = text.substr(start, at - start);
            blank();
            if (at >= text.size() || text[at] != '(') {
                if (name == "x" || name == "t") return x;
                if (name == "pi") return std::numbers::pi;
                if (name == "e") return std::numbers::e;
                broken = true;
                return 0.0;
            }
            ++at;
            const double first = self(0);
            double second = 0.0;
            blank();
            if (at < text.size() && text[at] == ',') {
                ++at;
                second = self(0);
                blank();
            }
            if (at < text.size() && text[at] == ')') ++at;
            else broken = true;

            if (name == "sin") return std::sin(first * degree);
            if (name == "cos") return std::cos(first * degree);
            if (name == "tan") return std::tan(first * degree);
            if (name == "asin") return std::asin(first) / degree;
            if (name == "acos") return std::acos(first) / degree;
            if (name == "atan") return std::atan(first) / degree;
            if (name == "atan2") return std::atan2(first, second) / degree;
            if (name == "veclen") return std::hypot(first, second);
            if (name == "mod") return std::fmod(first, second);
            if (name == "sign") return first > 0.0 ? 1.0 : first < 0.0 ? -1.0 : 0.0;
            if (name == "int") return std::trunc(first);
            if (name == "deg") return first / degree;
            if (name == "rad") return first * degree;
            if (name == "exp") return std::exp(first);
            if (name == "ln") return std::log(first);
            if (name == "log10") return std::log10(first);
            if (name == "log2") return std::log2(first);
            if (name == "sqrt") return std::sqrt(first);
            if (name == "abs") return std::abs(first);
            if (name == "min") return std::min(first, second);
            if (name == "max") return std::max(first, second);
            if (name == "pow") return std::pow(first, second);
            if (name == "floor") return std::floor(first);
            if (name == "ceil") return std::ceil(first);
            if (name == "round") return std::round(first);
            if (name == "sinh") return std::sinh(first);
            if (name == "cosh") return std::cosh(first);
            if (name == "tanh") return std::tanh(first);
            broken = true;
            return 0.0;
        };

        const double result = value(0);
        blank();
        if (broken || at != text.size() || !std::isfinite(result)) return std::nullopt;
        return result;
    }

    Plots::Plots(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\addplot");
        lexicon.intern("\\addlegendentry");
        lexicon.intern("\\legend");
    }

    void Plots::operator()(syntax::Parser& parser, Context& context) const {
        using Category = syntax::Catcodes::Category;
        using Pairs = std::vector<std::pair<std::string_view, std::string_view>>;

        // Text without the blanks around it, or the braces around the whole.
        const auto trim = [](std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r')) text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
            if (text.size() >= 2 && text.front() == '{' && text.back() == '}') text = text.substr(1, text.size() - 2);
            return text;
        };
        // A list cut at the commas outside braces, brackets and parentheses.
        const auto split = [](const std::string_view list) {
            std::vector<std::string_view> found;
            int depth = 0;
            std::size_t begin = 0;
            for (std::size_t at = 0; at <= list.size(); ++at) {
                const char letter = at < list.size() ? list[at] : ',';
                if (letter == '{' || letter == '[' || letter == '(') ++depth;
                if ((letter == '}' || letter == ']' || letter == ')') && depth > 0) --depth;
                if (letter != ',' || depth != 0) continue;
                found.push_back(list.substr(begin, at - begin));
                begin = at + 1;
            }
            return found;
        };
        // Options as keys and their values, a value's braces taken off.
        const auto read = [trim, split](const std::string_view list) {
            Pairs found;
            for (const std::string_view piece : split(list)) {
                int depth = 0;
                std::size_t equals = std::string_view::npos;
                for (std::size_t at = 0; at < piece.size() && equals == std::string_view::npos; ++at) {
                    if (piece[at] == '{' || piece[at] == '[') ++depth;
                    if ((piece[at] == '}' || piece[at] == ']') && depth > 0) --depth;
                    if (piece[at] == '=' && depth == 0) equals = at;
                }
                const std::string_view key = trim(piece.substr(0, equals));
                if (key.empty()) continue;
                found.emplace_back(key, equals == std::string_view::npos ? std::string_view{} : trim(piece.substr(equals + 1)));
            }
            return found;
        };
        const auto find = [](const Pairs& pairs, const std::string_view key) -> std::optional<std::string_view> {
            for (auto pair = pairs.rbegin(); pair != pairs.rend(); ++pair) {
                if (pair->first == key) return pair->second;
            }
            return std::nullopt;
        };
        const auto number = [trim](std::string_view text) -> std::optional<double> {
            text = trim(text);
            double value = 0.0;
            const auto [stop, failure] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (failure != std::errc{} || stop != text.data() + text.size()) return std::nullopt;
            return value;
        };
        // What an argument holds, a control word keeping the space that
        // ended it so the text reads back as the tokens it was.
        const auto spelled = [](const std::vector<syntax::Token>& tokens) {
            std::string text;
            for (const syntax::Token& token : tokens) {
                text += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
            }
            return text;
        };

        for (const std::string_view kind : {"axis", "semilogxaxis", "semilogyaxis", "loglogaxis"}) {
            context.blocks.watch(
                kind,
                [this, kind, spelled](syntax::Mouth& mouth) {
                    axes.push_back({.kind = std::string(kind),
                                    .options = spelled(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0))});
                },
                [this, &context, trim, split, read, find, number](syntax::Mouth& mouth) {
                    if (axes.empty()) return;
                    const Axis axis = std::move(axes.back());
                    axes.pop_back();
                    const Pairs settings = read(axis.options);
                    const auto has = [&settings](const std::string_view key) {
                        return std::ranges::any_of(settings, [key](const auto& pair) { return pair.first == key; });
                    };

                    // The box: what is left of pgfplots' 240 by 207 points
                    // once its labels have their room, or all of it with
                    // `scale only axis`; a width or a height given alone
                    // scales the other with it.
                    const auto length = [&](const std::string_view key, const float fallback) {
                        const auto written = find(settings, key);
                        return written ? measure(std::string(*written), mouth, context) : fallback;
                    };
                    const bool only = has("scale only axis");
                    const double across = length("width", has("height") ? length("height", 207.0f) * 240.0f / 207.0f : 240.0f);
                    const double upward = length("height", static_cast<float>(across) * 207.0f / 240.0f);
                    const double wide = std::max(40.0, across - (only ? 0.0 : 45.0));
                    const double tall = std::max(30.0, upward - (only ? 0.0 : 45.0));

                    // Logarithmic axes work in the exponents of their values.
                    const bool xlog = axis.kind == "semilogxaxis" || axis.kind == "loglogaxis" ||
                                      find(settings, "xmode") == "log";
                    const bool ylog = axis.kind == "semilogyaxis" || axis.kind == "loglogaxis" ||
                                      find(settings, "ymode") == "log";
                    const auto tx = [xlog](const double v) { return xlog ? std::log10(v) : v; };
                    const auto ty = [ylog](const double v) { return ylog ? std::log10(v) : v; };
                    const bool barred = has("ybar");

                    // The limits: the data's, what the options set, and the
                    // margin `enlargelimits` asks for on those they did not.
                    double xlo = std::numeric_limits<double>::infinity(), xhi = -xlo, ylo = xlo, yhi = -xlo;
                    for (const Plot& plot : axis.plots) {
                        for (const auto& [x, y] : plot.points) {
                            if (!std::isfinite(x) || !std::isfinite(y) || (xlog && x <= 0.0) || (ylog && y <= 0.0)) continue;
                            xlo = std::min(xlo, tx(x));
                            xhi = std::max(xhi, tx(x));
                            ylo = std::min(ylo, ty(y));
                            yhi = std::max(yhi, ty(y));
                        }
                    }
                    if (barred && !ylog) {
                        ylo = std::min(ylo, 0.0);
                        yhi = std::max(yhi, 0.0);
                    }
                    if (!std::isfinite(xlo)) {
                        xlo = 0.0;
                        xhi = 1.0;
                    }
                    if (!std::isfinite(ylo)) {
                        ylo = 0.0;
                        yhi = 1.0;
                    }
                    const auto enlarge = [&](const std::string_view axle, double& lo, double& hi, const bool low,
                                             const bool high) {
                        auto written = find(settings, std::format("enlarge {} limits", axle));
                        if (!written) written = find(settings, "enlargelimits");
                        double share = written ? (*written == "true" || written->empty() ? 0.1 : number(*written).value_or(0.0))
                                               : 0.0;
                        const double span = hi - lo;
                        if (low) lo -= share * span;
                        if (high) hi += share * span;
                    };
                    const auto fixed = [&](const std::string_view key, double& limit, const bool log) {
                        const auto written = find(settings, key);
                        const auto value = written ? number(*written) : std::nullopt;
                        if (!value || (log && *value <= 0.0)) return false;
                        limit = log ? std::log10(*value) : *value;
                        return true;
                    };
                    const bool xmin = fixed("xmin", xlo, xlog), xmax = fixed("xmax", xhi, xlog);
                    const bool ymin = fixed("ymin", ylo, ylog), ymax = fixed("ymax", yhi, ylog);
                    if (xhi - xlo < 1e-12) {
                        xlo -= 1.0;
                        xhi += 1.0;
                    }
                    if (yhi - ylo < 1e-12) {
                        ylo -= 1.0;
                        yhi += 1.0;
                    }
                    enlarge("x", xlo, xhi, !xmin, !xmax);
                    enlarge("y", ylo, yhi, !ymin, !ymax);

                    const auto px = [&](const double v) { return (tx(v) - xlo) / (xhi - xlo) * wide; };
                    const auto py = [&](const double v) { return (ty(v) - ylo) / (yhi - ylo) * tall; };
                    const auto point = [](const double x, const double y) { return std::format("({:.2f}pt,{:.2f}pt)", x, y); };

                    // Ticks: the list the options give -- `data` for the x
                    // the plots have, `a,b,...,c` counted out -- or round
                    // numbers no more than about 35 points apart, or powers
                    // of ten on a logarithmic axis. Each in the axis's own
                    // units, as the value it stands for.
                    const auto ticks = [&](const std::string_view key, const double lo, const double hi, const double span,
                                           const bool log) {
                        std::vector<double> found;
                        if (const auto written = find(settings, key)) {
                            if (*written == "data") {
                                for (const Plot& plot : axis.plots) {
                                    for (const auto& pair : plot.points) found.push_back(pair.first);
                                }
                                std::ranges::sort(found);
                                found.erase(std::ranges::unique(found).begin(), found.end());
                                return found;
                            }
                            const std::vector<std::string_view> items = split(*written);
                            for (std::size_t index = 0; index < items.size(); ++index) {
                                if (trim(items[index]) == "..." && index >= 2 && index + 1 < items.size()) {
                                    const auto a = number(items[index - 2]), b = number(items[index - 1]);
                                    const auto c = number(items[index + 1]);
                                    if (a && b && c && *b > *a) {
                                        for (double v = *b + (*b - *a); v < *c - 1e-9 * (*b - *a); v += *b - *a) found.push_back(v);
                                    }
                                    continue;
                                }
                                if (const auto value = number(items[index])) found.push_back(*value);
                            }
                            return found;
                        }
                        if (log) {
                            for (double k = std::ceil(lo - 1e-9); k <= hi + 1e-9; k += 1.0) found.push_back(std::pow(10.0, k));
                            return found;
                        }
                        const double raw = (hi - lo) / std::max(1.0, span / 35.0);
                        const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
                        const double normal = raw / magnitude;
                        const double step = magnitude * (normal < 1.414 ? 1.0 : normal < 3.162 ? 2.0 : normal < 7.071 ? 5.0 : 10.0);
                        for (double v = std::ceil(lo / step - 1e-9) * step; v <= hi + step * 1e-9; v += step) {
                            found.push_back(std::abs(v) < step * 1e-9 ? 0.0 : v);
                        }
                        return found;
                    };
                    const std::vector<double> xs = ticks("xtick", xlo, xhi, wide, xlog);
                    const std::vector<double> ys = ticks("ytick", ylo, yhi, tall, ylog);

                    // A tick's label: the number as short as it goes, a power
                    // of ten, or the name the options give it.
                    const auto labelled = [&](const std::string_view key, const double value, const std::size_t index,
                                              const bool log) -> std::string {
                        if (const auto written = find(settings, key)) {
                            const std::vector<std::string_view> names = split(*written);
                            return index < names.size() ? std::string(trim(names[index])) : std::string{};
                        }
                        if (key == "xticklabels") {
                            if (const auto symbols = find(settings, "symbolic x coords")) {
                                const std::vector<std::string_view> names = split(*symbols);
                                const auto slot = static_cast<std::size_t>(std::lround(value));
                                if (value >= 0.0 && slot < names.size()) return std::string(trim(names[slot]));
                            }
                        }
                        if (log) return std::format("${{10^{{{}}}}}$", std::lround(std::log10(value)));
                        std::string text = std::format("{:.4f}", std::abs(value) < 1e-12 ? 0.0 : value);
                        while (text.ends_with('0')) text.pop_back();
                        if (text.ends_with('.')) text.pop_back();
                        if (text == "-0") text = "0";
                        return "$" + text + "$";
                    };

                    std::string out;
                    const bool hidden = has("hide axis");
                    std::string_view lines = find(settings, "axis lines").value_or(find(settings, "axis lines*").value_or("box"));
                    if (hidden) lines = "none";
                    const bool boxed = lines == "box";
                    const bool crossing = lines == "middle" || lines == "center";
                    const double ox = crossing ? std::clamp((0.0 - xlo) / (xhi - xlo) * wide, 0.0, wide) : 0.0;
                    const double oy = crossing ? std::clamp((0.0 - ylo) / (yhi - ylo) * tall, 0.0, tall) : 0.0;

                    // The grid, under everything else.
                    const std::string_view grid = find(settings, "grid").value_or("");
                    const bool xgrid = grid == "major" || grid == "both" || has("xmajorgrids");
                    const bool ygrid = grid == "major" || grid == "both" || has("ymajorgrids");
                    for (const double v : xs) {
                        const double at = px(v);
                        if (xgrid && at > 0.01 && at < wide - 0.01) {
                            out += std::format("\\draw[black!25] {} -- {};", point(at, 0.0), point(at, tall));
                        }
                    }
                    for (const double v : ys) {
                        const double at = py(v);
                        if (ygrid && at > 0.01 && at < tall - 0.01) {
                            out += std::format("\\draw[black!25] {} -- {};", point(0.0, at), point(wide, at));
                        }
                    }

                    // The plots, each in the colors and marks it asks for or
                    // pgfplots' next ones, clipped to the box.
                    static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> cycle{{
                        {"blue", "*"}, {"red", "square*"}, {"brown!60!black", "o"}, {"black", "star"}, {"blue", "diamond*"},
                    }};
                    const double bar = find(settings, "bar width") ? length("bar width", 10.0f) : 10.0;
                    const std::size_t bars = barred ? axis.plots.size() : 0;
                    struct Legend {
                        std::string color;
                        std::string mark;
                        bool line;
                        bool bar;
                    };
                    std::vector<Legend> samples;
                    const auto dot = [&](const std::string_view mark, const std::string_view color, const double x,
                                         const double y) {
                        constexpr double r = 2.0;
                        const bool open = !mark.ends_with('*');
                        const std::string_view shape = open ? mark : mark.substr(0, mark.size() - 1);
                        const std::string how = std::format("\\{}[{}]", open ? "draw" : "filldraw", color);
                        if (shape == "square") {
                            out += std::format("{} {} rectangle {};", how, point(x - r, y - r), point(x + r, y + r));
                        } else if (shape == "triangle") {
                            out += std::format("{} {} -- {} -- {} -- cycle;", how, point(x, y + 1.2 * r),
                                               point(x - r, y - 0.8 * r), point(x + r, y - 0.8 * r));
                        } else if (shape == "diamond") {
                            out += std::format("{} {} -- {} -- {} -- {} -- cycle;", how, point(x, y + r), point(x + r, y),
                                               point(x, y - r), point(x - r, y));
                        } else if (shape == "x" || shape == "star" || shape == "asterisk" || shape == "+") {
                            if (shape != "+") {
                                out += std::format("\\draw[{}] {} -- {};\\draw[{}] {} -- {};", color, point(x - r, y - r),
                                                   point(x + r, y + r), color, point(x - r, y + r), point(x + r, y - r));
                            }
                            if (shape != "x") {
                                out += std::format("\\draw[{}] {} -- {};\\draw[{}] {} -- {};", color, point(x - r, y),
                                                   point(x + r, y), color, point(x, y - r), point(x, y + r));
                            }
                        } else if (shape == "|" || shape == "-") {
                            out += std::format("\\draw[{}] {} -- {};", color, point(x - (shape == "-" ? r : 0.0),
                                               y - (shape == "|" ? r : 0.0)),
                                               point(x + (shape == "-" ? r : 0.0), y + (shape == "|" ? r : 0.0)));
                        } else {
                            out += std::format("{} {} circle (2pt);", how, point(x, y));
                        }
                    };
                    for (std::size_t index = 0; index < axis.plots.size(); ++index) {
                        const Plot& plot = axis.plots[index];
                        std::string color = plot.cycled ? std::string(cycle[index % cycle.size()].first) : "black";
                        std::string mark = plot.cycled ? std::string(cycle[index % cycle.size()].second) : "";
                        std::string fill;
                        std::string kept;
                        bool line = true;
                        bool forgotten = false;
                        for (const auto& [key, value] : read(plot.options)) {
                            if (key == "mark") {
                                mark = value == "none" ? "" : std::string(value);
                            } else if (key == "only marks") {
                                line = false;
                                if (mark.empty()) mark = "*";
                            } else if (key == "no marks" || key == "no markers") mark.clear();
                            else if (key == "forget plot") forgotten = true;
                            else if (key == "color" || key == "draw") color = value;
                            else if (key == "fill") fill = value;
                            else if (key.contains("thick") || key.contains("thin") || key.contains("dash") ||
                                     key.contains("dot") || key == "solid" || key == "line width" || key.contains("opacity")) {
                                kept += std::format(",{}{}{}", key, value.empty() ? "" : "=", value);
                            } else if (value.empty() && !key.contains(' ') && key != "smooth" && key != "sharp plot" &&
                                       key != "ybar" && key != "const plot") {
                                color = key;
                            }
                        }
                        if (!plot.cycled && mark.empty() && !line) mark = "*";
                        const bool histogram = barred;
                        if (!forgotten) samples.push_back({color, mark, line && !histogram, histogram});

                        if (histogram) {
                            const double shift = (static_cast<double>(index) - (static_cast<double>(bars) - 1.0) / 2.0) *
                                                 (bar + 2.0);
                            const double base = std::clamp(ylog ? 0.0 : py(0.0), 0.0, tall);
                            const std::string paint = fill.empty() ? color + "!30!white" : fill;
                            for (const auto& [x, y] : plot.points) {
                                if (!std::isfinite(x) || !std::isfinite(y)) continue;
                                const double centre = px(x) + shift;
                                if (centre < -0.01 || centre > wide + 0.01) continue;
                                out += std::format("\\filldraw[fill={},draw={}] {} rectangle {};", paint, color,
                                                   point(centre - bar / 2.0, base), point(centre + bar / 2.0,
                                                   std::clamp(py(y), 0.0, tall)));
                            }
                            continue;
                        }

                        // The line, in runs: each segment clipped to the box,
                        // a run carrying on while the next starts where the
                        // last one ended.
                        if (line) {
                            std::vector<std::pair<double, double>> run;
                            const auto flush = [&] {
                                if (run.size() >= 2) {
                                    out += std::format("\\draw[{}{}] ", color, kept);
                                    for (std::size_t at = 0; at < run.size(); ++at) {
                                        out += (at ? " -- " : "") + point(run[at].first, run[at].second);
                                    }
                                    out += ";";
                                }
                                run.clear();
                            };
                            for (std::size_t at = 1; at < plot.points.size(); ++at) {
                                const auto [a, b] = plot.points[at - 1];
                                const auto [c, d] = plot.points[at];
                                if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d) ||
                                    (xlog && (a <= 0.0 || c <= 0.0)) || (ylog && (b <= 0.0 || d <= 0.0))) {
                                    flush();
                                    continue;
                                }
                                double x0 = px(a), y0 = py(b), x1 = px(c), y1 = py(d);
                                double t0 = 0.0, t1 = 1.0;
                                const double dx = x1 - x0, dy = y1 - y0;
                                bool seen = true;
                                for (const auto [p, q] : {std::pair{-dx, x0 + 0.01}, std::pair{dx, wide + 0.01 - x0},
                                                          std::pair{-dy, y0 + 0.01}, std::pair{dy, tall + 0.01 - y0}}) {
                                    if (std::abs(p) < 1e-12) {
                                        if (q < 0.0) seen = false;
                                        continue;
                                    }
                                    const double r = q / p;
                                    if (p < 0.0) t0 = std::max(t0, r);
                                    else t1 = std::min(t1, r);
                                }
                                if (!seen || t0 > t1) {
                                    flush();
                                    continue;
                                }
                                const std::pair from{x0 + t0 * dx, y0 + t0 * dy};
                                const std::pair to{x0 + t1 * dx, y0 + t1 * dy};
                                if (!run.empty() && (std::abs(run.back().first - from.first) > 0.01 ||
                                                     std::abs(run.back().second - from.second) > 0.01)) {
                                    flush();
                                }
                                if (run.empty()) run.push_back(from);
                                run.push_back(to);
                            }
                            flush();
                        }
                        if (!mark.empty()) {
                            for (const auto& [x, y] : plot.points) {
                                if (!std::isfinite(x) || !std::isfinite(y) || (xlog && x <= 0.0) || (ylog && y <= 0.0)) continue;
                                const double a = px(x), b = py(y);
                                if (a < -0.5 || a > wide + 0.5 || b < -0.5 || b > tall + 0.5) continue;
                                dot(mark, color, a, b);
                            }
                        }
                    }

                    // The axis: its box, or its lines; the ticks, inward, and
                    // their labels outside it -- or beside its lines where
                    // they cross.
                    constexpr double tick = 4.27;   // pgfplots' 0.15cm
                    if (boxed) out += std::format("\\draw {} rectangle {};", point(0.0, 0.0), point(wide, tall));
                    if (lines == "left") {
                        out += std::format("\\draw[->] {} -- {};\\draw[->] {} -- {};", point(0.0, 0.0), point(wide, 0.0),
                                           point(0.0, 0.0), point(0.0, tall));
                    }
                    if (crossing) {
                        out += std::format("\\draw[->] {} -- {};\\draw[->] {} -- {};", point(0.0, oy), point(wide, oy),
                                           point(ox, 0.0), point(ox, tall));
                    }
                    double widest = 0.0;
                    for (std::size_t index = 0; index < xs.size() && lines != "none"; ++index) {
                        const double at = px(xs[index]);
                        if (at < -0.01 || at > wide + 0.01) continue;
                        out += std::format("\\draw {} -- {};", point(at, oy), point(at, oy + tick));
                        if (boxed) out += std::format("\\draw {} -- {};", point(at, tall), point(at, tall - tick));
                        if (crossing && std::abs(at - ox) < 0.01) continue;
                        const std::string text = labelled("xticklabels", xs[index], index, xlog);
                        if (!text.empty()) out += std::format("\\node[below] at {} {{{}}};", point(at, oy), text);
                    }
                    for (std::size_t index = 0; index < ys.size() && lines != "none"; ++index) {
                        const double at = py(ys[index]);
                        if (at < -0.01 || at > tall + 0.01) continue;
                        out += std::format("\\draw {} -- {};", point(ox, at), point(ox + tick, at));
                        if (boxed) out += std::format("\\draw {} -- {};", point(wide, at), point(wide - tick, at));
                        if (crossing && std::abs(at - oy) < 0.01) continue;
                        const std::string text = labelled("yticklabels", ys[index], index, ylog);
                        if (text.empty()) continue;
                        // How wide the label stands, near enough: five points a
                        // figure, a power of ten as wide as three.
                        widest = std::max(widest, ylog ? 16.0 : static_cast<double>(text.size() - 2) * 5.0);
                        out += std::format("\\node[left] at {} {{{}}};", point(ox, at), text);
                    }

                    // Its labels and title.
                    if (const auto xlabel = find(settings, "xlabel")) {
                        out += std::format("\\node[below] at {} {{{}}};", point(wide / 2.0, crossing ? -2.0 : -13.0), *xlabel);
                    }
                    if (const auto ylabel = find(settings, "ylabel")) {
                        out += std::format("\\node[left] at {} {{\\rotatebox{{90}}{{{}}}}};",
                                           point(crossing ? -2.0 : -(widest + 6.0), tall / 2.0), *ylabel);
                    }
                    if (const auto title = find(settings, "title")) {
                        out += std::format("\\node[above] at {} {{{}}};", point(wide / 2.0, tall + 2.0), *title);
                    }

                    // The legend: a framed table of each plot's line or mark
                    // beside its entry, in the corner `legend pos` names.
                    const std::size_t entries = std::min(axis.legends.size(), samples.size());
                    if (entries > 0) {
                        std::string rows;
                        for (std::size_t index = 0; index < entries; ++index) {
                            const Legend& sample = samples[index];
                            std::string image = sample.bar ? "\\rule{0.8em}{0.8em}"
                                                : sample.line && !sample.mark.empty()
                                                    ? "\\rule[0.55ex]{0.5em}{0.6pt}$\\bullet$\\rule[0.55ex]{0.5em}{0.6pt}"
                                                : sample.line ? "\\rule[0.55ex]{1.2em}{0.6pt}"
                                                              : "$\\bullet$";
                            rows += std::format("\\textcolor{{{}}}{{{}}} & {}\\\\", sample.color, image, axis.legends[index]);
                        }
                        const std::string_view where = find(settings, "legend pos").value_or("north east");
                        const bool outer = where.starts_with("outer");
                        const bool west = where.ends_with("west") && !outer;
                        const bool south = where.starts_with("south");
                        const double x = outer ? wide + 5.0 : west ? 3.0 : wide - 3.0;
                        const double y = south ? 3.0 : tall - 3.0;
                        const std::string anchor = outer ? "north west"
                                                         : std::format("{} {}", south ? "south" : "north", west ? "west" : "east");
                        out += std::format("\\node[draw, fill=white, inner sep=2pt, anchor={}] at {} "
                                           "{{\\small\\begin{{tabular}}{{@{{}}c@{{\\ }}l@{{}}}}{}\\end{{tabular}}}};",
                                           anchor, point(x, outer ? tall : y), rows);
                    }
                    mouth.ingest(context.arena.copy(out));
                });
        }

        // \addplot[options] and what it plots, to its semicolon: its
        // coordinates, a table inline or in a file, or a function of x --
        // or two, as a parametric curve.
        parser.mouth.bind("\\addplot", [this, &context, trim, split, read, find, number, spelled](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            bool cycled = true;
            if (mouth.lookahead().is('+')) {
                mouth.read();
            } else if (mouth.lookahead().is('[')) {
                cycled = false;
            }
            const std::string options = spelled(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
            std::string text;
            int depth = 0;
            for (syntax::Token token = mouth.expand(); !token.empty(); token = mouth.expand()) {
                if (depth == 0 && token.is(Category::Other, ';')) break;
                if (token.is(Category::Group, '{')) ++depth;
                if (token.is(Category::Group, '}')) --depth;
                text += token.text;
                if (token.text.size() > 1 && token.text.front() == '\\') text += ' ';
            }
            if (axes.empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Environment, origin, "\\addplot outside an axis");
                return;
            }
            Axis& axis = axes.back();
            Plot plot{.options = options, .cycled = cycled};
            const Pairs own = read(options);
            const Pairs settings = read(axis.options);
            constexpr double gap = std::numeric_limits<double>::quiet_NaN();

            // The braced group a part of it starts with, its braces taken off.
            const auto braced = [](const std::string_view from) -> std::string_view {
                const std::size_t open = from.find('{');
                if (open == std::string_view::npos) return {};
                int level = 0;
                for (std::size_t at = open; at < from.size(); ++at) {
                    if (from[at] == '{') ++level;
                    if (from[at] == '}' && --level == 0) return from.substr(open + 1, at - open - 1);
                }
                return from.substr(open + 1);
            };
            // An x as the data write it: a number, or one of the axis's
            // symbolic coordinates, counted from 0.
            const std::vector<std::string_view> symbols = split(find(settings, "symbolic x coords").value_or(""));
            const auto abscissa = [&](const std::string_view written) -> std::optional<double> {
                if (const auto value = number(written)) return value;
                for (std::size_t index = 0; index < symbols.size(); ++index) {
                    if (trim(symbols[index]) == trim(written)) return static_cast<double>(index);
                }
                return std::nullopt;
            };

            std::string_view spec = trim(text);
            if (spec.starts_with("coordinates")) {
                const std::string_view inside = braced(spec);
                for (std::size_t open = inside.find('('); open != std::string_view::npos; open = inside.find('(', open + 1)) {
                    const std::size_t close = inside.find(')', open);
                    if (close == std::string_view::npos) break;
                    const std::string_view pair = inside.substr(open + 1, close - open - 1);
                    const std::size_t comma = pair.find(',');
                    open = close;
                    if (comma == std::string_view::npos) continue;
                    const auto x = abscissa(pair.substr(0, comma));
                    const auto y = number(pair.substr(comma + 1));
                    if (x && y) plot.points.emplace_back(*x, *y);
                }
            } else if (spec.starts_with("table")) {
                spec.remove_prefix(5);
                while (!spec.empty() && spec.front() == ' ') spec.remove_prefix(1);
                Pairs table;
                if (spec.starts_with('[')) {
                    const std::size_t close = spec.find(']');
                    table = read(spec.substr(1, close == std::string_view::npos ? spec.size() - 1 : close - 1));
                    spec = close == std::string_view::npos ? std::string_view{} : spec.substr(close + 1);
                }
                const std::string_view inside = braced(spec);
                std::string data(inside);
                const bool named = !inside.empty() && inside.find_first_of(" \n\\") == std::string_view::npos &&
                                   !number(inside.substr(0, 1));
                if (named) {
                    const std::string* file = context.disk ? context.disk(inside) : nullptr;
                    if (!file) {
                        tracebacks.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                                 std::format("\\addplot: no table file named '{}'", inside));
                    }
                    data = file ? *file : std::string{};
                }
                // A first row of names is the header.
                std::vector<std::vector<std::string>> rows = Plots::rows(data, find(table, "col sep") == "comma");
                // A whole table on one line -- written inline, its line ends
                // read as blanks -- is its names, as many as stand before
                // the first number, then rows as wide as they are, or of two
                // cells with no names.
                std::vector<std::string> header;
                if (rows.size() == 1) {
                    std::vector<std::string>& all = rows.front();
                    const auto first = std::ranges::find_if(all, [&](const std::string& item) { return number(item).has_value(); });
                    header.assign(all.begin(), first);
                    const std::size_t across = header.empty() ? 2 : header.size();
                    std::vector<std::vector<std::string>> cut;
                    for (auto at = first; all.end() - at >= static_cast<std::ptrdiff_t>(across);
                         at += static_cast<std::ptrdiff_t>(across)) {
                        cut.emplace_back(at, at + static_cast<std::ptrdiff_t>(across));
                    }
                    rows = std::move(cut);
                } else if (!rows.empty() && !number(rows.front().front())) {
                    header = std::move(rows.front());
                    rows.erase(rows.begin());
                }
                const auto column = [&](const std::string_view axle, const std::size_t fallback) {
                    if (const auto name = find(table, axle)) {
                        for (std::size_t index = 0; index < header.size(); ++index) {
                            if (header[index] == *name) return index;
                        }
                    }
                    if (const auto index = find(table, std::format("{} index", axle))) {
                        return static_cast<std::size_t>(number(*index).value_or(static_cast<double>(fallback)));
                    }
                    return fallback;
                };
                const std::size_t xi = column("x", 0), yi = column("y", 1);
                for (const std::vector<std::string>& row : rows) {
                    if (xi >= row.size() || yi >= row.size()) continue;
                    const auto x = abscissa(row[xi]);
                    const auto y = number(row[yi]);
                    if (x && y) plot.points.emplace_back(*x, *y);
                }
            } else if (spec.starts_with("gnuplot") || spec.starts_with("shell") || spec.starts_with("file")) {
                tracebacks.emplace_back(syntax::Traceback::Type::Warning, origin,
                                         "\\addplot: a plot from gnuplot, a shell or a file of points is not drawn here");
            } else {
                // A function of x, sampled across its domain; or two, for a
                // parametric curve, `({cos(x)}, {sin(x)})`.
                if (spec.starts_with("expression")) spec = trim(spec.substr(10));
                std::string_view across;
                std::string_view along = spec;
                if (spec.starts_with('(')) {
                    const std::vector<std::string_view> parts = split(spec.substr(1, spec.rfind(')') - 1));
                    if (parts.size() == 2) {
                        across = trim(parts[0]);
                        along = trim(parts[1]);
                    }
                } else if (spec.starts_with('{')) {
                    along = braced(spec);
                }
                const std::string_view domain =
                    find(own, "domain").value_or(find(settings, "domain").value_or("-5:5"));
                const std::size_t colon = domain.find(':');
                const double low = number(domain.substr(0, colon)).value_or(-5.0);
                const double high = colon == std::string_view::npos ? 5.0 : number(domain.substr(colon + 1)).value_or(5.0);
                const double count = std::clamp(
                    number(find(own, "samples").value_or(find(settings, "samples").value_or("25"))).value_or(25.0), 2.0,
                    2000.0);
                bool any = false;
                for (double step = 0.0; step < count; step += 1.0) {
                    const double x = low + (high - low) * step / (count - 1.0);
                    const auto y = calculate(along, x);
                    const auto t = across.empty() ? std::optional<double>(x) : calculate(across, x);
                    plot.points.emplace_back(t && y ? *t : gap, t && y ? *y : gap);
                    any = any || (t && y);
                }
                if (!any) {
                    tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                             std::format("\\addplot: '{}' is not a function this can work out", along));
                }
            }
            axis.plots.push_back(std::move(plot));
        });

        // A plot's entry in the legend, and every entry at once.
        // pgfplotstable's \\pgfplotstabletypeset[options]{table or file}: the
        // table set as a tabular, its names -- a first row holding any word
        // -- as its head, each cell as written.
        parser.mouth.bind("\\pgfplotstabletypeset", [this, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            std::string options;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                options += token.text;
            }
            std::string data;
            for (const syntax::Token& token : mouth.argument({}, 0)) data += token.text;
            if (!data.empty() && data.find_first_of(" \n\\") == std::string::npos) {
                if (const std::string* file = context.disk ? context.disk(data) : nullptr) {
                    data = *file;
                } else if (context.files && context.files->contains(data)) {
                    data = context.files->at(data);
                } else {
                    tracebacks.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                             std::format("\\pgfplotstabletypeset: no table file named '{}'", data));
                    return;
                }
            }
            const std::vector<std::vector<std::string>> table = rows(data, options.contains("col sep=comma"));
            std::size_t across = 0;
            for (const std::vector<std::string>& row : table) across = std::max(across, row.size());
            if (across == 0) return;
            std::string written = "\\begin{tabular}{" + std::string(across, 'c') + "}";
            for (std::size_t index = 0; index < table.size(); ++index) {
                for (std::size_t cell = 0; cell < table[index].size(); ++cell) {
                    written += (cell == 0 ? "" : " & ") + table[index][cell];
                }
                written += "\\\\";
                const bool named = index == 0 && std::ranges::any_of(table[index], [](const std::string& item) {
                    double value = 0.0;
                    const auto [end, fault] = std::from_chars(item.data(), item.data() + item.size(), value);
                    return fault != std::errc{} || end != item.data() + item.size();
                });
                if (named) written += "\\hline ";
            }
            written += "\\end{tabular}";
            mouth.ingest(mouth.arena.copy(written), memory::Location{});
        });

        parser.mouth.bind("\\addlegendentry", [this, spelled](syntax::Mouth& mouth) {
            static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
            const std::string entry = spelled(mouth.argument({}, 0));
            if (!axes.empty()) axes.back().legends.push_back(entry);
        });
        parser.mouth.bind("\\legend", [this, trim, split, spelled](syntax::Mouth& mouth) {
            const std::string entries = spelled(mouth.argument({}, 0));
            if (axes.empty()) return;
            axes.back().legends.clear();
            for (const std::string_view entry : split(entries)) axes.back().legends.emplace_back(trim(entry));
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound plot primitives");
    }

    std::vector<std::vector<std::string>> Plots::rows(const std::string_view data, const bool commas) {
        std::vector<std::vector<std::string>> table;
        std::vector<std::string> cells;
        std::string cell;
        const auto stash = [&] {
            if (!cell.empty()) cells.push_back(std::move(cell));
            cell.clear();
        };
        const auto commit = [&] {
            stash();
            if (!cells.empty() && !cells.front().starts_with('#') && !cells.front().starts_with('%')) {
                table.push_back(std::move(cells));
            }
            cells.clear();
        };
        for (std::size_t at = 0; at < data.size(); ++at) {
            const char letter = data[at];
            if (letter == '\n' || (letter == '\\' && at + 1 < data.size() && data[at + 1] == '\\')) {
                if (letter == '\\') ++at;
                commit();
            } else if (letter == ' ' || letter == '\t' || letter == '\r' || (commas && letter == ',')) {
                stash();
            } else {
                cell += letter;
            }
        }
        commit();
        return table;
    }

}
