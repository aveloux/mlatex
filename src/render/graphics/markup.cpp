/// @file
/// @brief Drawing's SVG: an SVG file's elements read into the content of a
///        page as large as its picture.
///
/// markup() reads the file's elements into a tree, then draws it: each shape
/// and path through the transformations of the groups around it and its
/// own, in the style it inherits and gives itself -- its presentation
/// attributes, the `<style>` rules naming its element, class or id, and its
/// `style` -- as PDF's operators in the page's own points, y turned upward.
#include "render/graphics/drawing.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace render::graphics {

    std::optional<Drawing> Drawing::markup(const std::string_view file) {
        // --- The elements --------------------------------------------------------------------

        // One element: its name, its attributes, the elements in it, and
        // the text it holds -- a `<style>`'s rules -- each run of which is
        // also a child named `#text`, where it stands among the others, so
        // a `<text>`'s words and its spans' come in their order.
        struct Element {
            std::string name{};
            std::vector<std::pair<std::string, std::string>> attributes{};
            std::vector<Element> children{};
            std::string text{};
        };
        const auto blank = [](const char letter) { return letter == ' ' || letter == '\n' || letter == '\r' || letter == '\t'; };
        // An attribute's or a text's entities written out.
        const auto plain = [](std::string_view text) {
            std::string out;
            for (std::size_t at = 0; at < text.size(); ++at) {
                if (text[at] != '&') {
                    out += text[at];
                    continue;
                }
                const std::size_t end = text.find(';', at);
                if (end == std::string_view::npos || end - at > 10) {
                    out += '&';
                    continue;
                }
                const std::string_view entity = text.substr(at + 1, end - at - 1);
                if (entity == "amp") out += '&';
                else if (entity == "lt") out += '<';
                else if (entity == "gt") out += '>';
                else if (entity == "quot") out += '"';
                else if (entity == "apos") out += '\'';
                else if (entity == "nbsp" || entity == "#160") out += ' ';
                else if (entity.starts_with('#')) {
                    unsigned code = 0;
                    const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                    std::from_chars(entity.data() + (hex ? 2 : 1), entity.data() + entity.size(), code, hex ? 16 : 10);
                    out += code < 256 ? static_cast<char>(code) : '?';
                }
                at = end;
            }
            return out;
        };

        // The tree, read element by element: a declaration, a comment, a
        // document type and an element of the file's own are passed over,
        // CDATA kept as text.
        Element root{.name = "#document"};
        std::vector<Element*> nested{&root};
        for (std::size_t at = 0; at < file.size();) {
            if (file[at] != '<') {
                const std::size_t next = file.find('<', at);
                const std::string words = plain(file.substr(at, (next == std::string_view::npos ? file.size() : next) - at));
                nested.back()->text += words;
                nested.back()->children.push_back(Element{.name = "#text", .text = words});
                at = next == std::string_view::npos ? file.size() : next;
                continue;
            }
            const std::string_view rest = file.substr(at);
            if (rest.starts_with("<!--")) {
                const std::size_t end = file.find("-->", at + 4);
                at = end == std::string_view::npos ? file.size() : end + 3;
            } else if (rest.starts_with("<![CDATA[")) {
                const std::size_t end = file.find("]]>", at + 9);
                const std::string words(file.substr(at + 9, (end == std::string_view::npos ? file.size() : end) - at - 9));
                nested.back()->text += words;
                nested.back()->children.push_back(Element{.name = "#text", .text = words});
                at = end == std::string_view::npos ? file.size() : end + 3;
            } else if (rest.starts_with("<?") || rest.starts_with("<!")) {
                const std::size_t end = file.find('>', at);
                at = end == std::string_view::npos ? file.size() : end + 1;
            } else if (rest.starts_with("</")) {
                const std::size_t end = file.find('>', at);
                if (nested.size() > 1) nested.pop_back();
                at = end == std::string_view::npos ? file.size() : end + 1;
            } else {
                std::size_t cursor = at + 1;
                Element element;
                while (cursor < file.size() && !blank(file[cursor]) && file[cursor] != '>' && file[cursor] != '/') {
                    element.name += file[cursor++];
                }
                // Its namespace's prefix, `svg:rect`, let go.
                if (const std::size_t colon = element.name.find(':'); colon != std::string::npos) element.name.erase(0, colon + 1);
                bool closed = false;
                while (cursor < file.size()) {
                    while (cursor < file.size() && blank(file[cursor])) ++cursor;
                    if (cursor >= file.size()) break;
                    if (file[cursor] == '>') {
                        ++cursor;
                        break;
                    }
                    if (file[cursor] == '/') {
                        closed = true;
                        ++cursor;
                        continue;
                    }
                    std::string key;
                    while (cursor < file.size() && !blank(file[cursor]) && file[cursor] != '=' && file[cursor] != '>' &&
                           file[cursor] != '/') {
                        key += file[cursor++];
                    }
                    while (cursor < file.size() && blank(file[cursor])) ++cursor;
                    std::string value;
                    if (cursor < file.size() && file[cursor] == '=') {
                        ++cursor;
                        while (cursor < file.size() && blank(file[cursor])) ++cursor;
                        const char quote = cursor < file.size() ? file[cursor] : '"';
                        if (quote == '"' || quote == '\'') {
                            const std::size_t end = file.find(quote, cursor + 1);
                            value = plain(file.substr(cursor + 1, (end == std::string_view::npos ? file.size() : end) - cursor - 1));
                            cursor = end == std::string_view::npos ? file.size() : end + 1;
                        }
                    }
                    if (const std::size_t colon = key.find(':'); colon != std::string::npos && !key.starts_with("xml")) {
                        key.erase(0, colon + 1);
                    }
                    if (!key.empty()) element.attributes.emplace_back(std::move(key), std::move(value));
                }
                nested.back()->children.push_back(std::move(element));
                if (!closed) nested.push_back(&nested.back()->children.back());
                at = cursor;
            }
        }

        // The `<svg>` element, wherever the declarations put it.
        const auto seek = [](this const auto& self, const Element& in, const std::string_view name) -> const Element* {
            for (const Element& child : in.children) {
                if (child.name == name) return &child;
                if (const Element* found = self(child, name)) return found;
            }
            return nullptr;
        };
        const Element* picture = seek(root, "svg");
        if (!picture) return std::nullopt;

        const auto attribute = [](const Element& element, const std::string_view key) -> std::optional<std::string_view> {
            for (const auto& [name, value] : element.attributes) {
                if (name == key) return std::string_view(value);
            }
            return std::nullopt;
        };
        // Every element by its id, for `<use>`, a gradient and a clip.
        std::map<std::string, const Element*, std::less<>> named;
        const auto index = [&](this const auto& self, const Element& element) -> void {
            if (const auto id = attribute(element, "id")) named[std::string(*id)] = &element;
            for (const Element& child : element.children) self(child);
        };
        index(*picture);

        // The `<style>` rules: each selector, an element's name, `.class` or
        // `#id`, with what it declares.
        std::vector<std::pair<std::string, std::string>> rules;
        const auto gather = [&](this const auto& self, const Element& element) -> void {
            if (element.name == "style") {
                std::string_view sheet = element.text;
                for (std::size_t at = 0; at < sheet.size();) {
                    const std::size_t open = sheet.find('{', at);
                    if (open == std::string_view::npos) break;
                    const std::size_t close = sheet.find('}', open);
                    const std::string_view declarations = sheet.substr(open + 1, (close == std::string_view::npos ? sheet.size() : close) - open - 1);
                    std::string_view selectors = sheet.substr(at, open - at);
                    for (std::size_t from = 0; from <= selectors.size();) {
                        std::size_t comma = selectors.find(',', from);
                        if (comma == std::string_view::npos) comma = selectors.size();
                        std::string_view selector = selectors.substr(from, comma - from);
                        while (!selector.empty() && (selector.front() == ' ' || selector.front() == '\n' || selector.front() == '\t')) {
                            selector.remove_prefix(1);
                        }
                        while (!selector.empty() && (selector.back() == ' ' || selector.back() == '\n' || selector.back() == '\t')) {
                            selector.remove_suffix(1);
                        }
                        if (!selector.empty()) rules.emplace_back(std::string(selector), std::string(declarations));
                        from = comma + 1;
                    }
                    at = close == std::string_view::npos ? sheet.size() : close + 1;
                }
            }
            for (const Element& child : element.children) self(child);
        };
        gather(*picture);

        // --- Lengths, colours and transformations ---------------------------------------------

        // The numbers in a list, as SVG writes them: blanks or commas
        // between, or none where a sign or a second point starts the next.
        const auto numbers = [&blank](const std::string_view text) {
            std::vector<double> found;
            for (std::size_t at = 0; at < text.size();) {
                const char letter = text[at];
                if (blank(letter) || letter == ',') {
                    ++at;
                    continue;
                }
                double value = 0.0;
                const char* start = text.data() + at;
                if (*start == '+') ++start;
                const auto [stop, failure] = std::from_chars(start, text.data() + text.size(), value);
                if (failure != std::errc{}) {
                    ++at;
                    continue;
                }
                found.push_back(value);
                at = static_cast<std::size_t>(stop - text.data());
            }
            return found;
        };
        // A length in points: pixels, three quarters of a point, unless a
        // unit says; a share of `whole` for a percentage.
        const auto length = [](std::string_view text, const double whole = 0.0) -> std::optional<double> {
            while (!text.empty() && (text.front() == ' ')) text.remove_prefix(1);
            double value = 0.0;
            const auto [stop, failure] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (failure != std::errc{}) return std::nullopt;
            const std::string_view unit = text.substr(static_cast<std::size_t>(stop - text.data()));
            if (unit.starts_with('%')) return value / 100.0 * whole;
            if (unit.starts_with("pt")) return value;
            if (unit.starts_with("pc")) return value * 12.0;
            if (unit.starts_with("mm")) return value * 72.0 / 25.4;
            if (unit.starts_with("cm")) return value * 72.0 / 2.54;
            if (unit.starts_with("in")) return value * 72.0;
            if (unit.starts_with("em")) return value * 12.0;
            return value * 0.75;
        };
        // A colour by its name, `#rgb`, `#rrggbb` or `rgb(...)`; none for
        // `none`; a gradient's first stop for `url(#gradient)`.
        const auto colour = [&](this const auto& self, std::string_view text, const std::optional<std::array<double, 3>> inherited)
            -> std::optional<std::array<double, 3>> {
            while (!text.empty() && blank(text.front())) text.remove_prefix(1);
            while (!text.empty() && blank(text.back())) text.remove_suffix(1);
            if (text.empty() || text == "none" || text == "transparent") return std::nullopt;
            if (text == "currentColor" || text == "inherit") return inherited;
            if (text.starts_with("url(")) {
                std::string_view target = text.substr(4, text.find(')') - 4);
                if (target.starts_with('#')) target.remove_prefix(1);
                const auto found = named.find(target);
                if (found == named.end()) return std::array<double, 3>{0.0, 0.0, 0.0};
                for (const Element& stop : found->second->children) {
                    if (stop.name != "stop") continue;
                    std::string written(attribute(stop, "stop-color").value_or("black"));
                    if (const auto style = attribute(stop, "style")) {
                        if (const std::size_t at = style->find("stop-color:"); at != std::string_view::npos) {
                            written = style->substr(at + 11, style->find(';', at) - at - 11);
                        }
                    }
                    return self(written, inherited);
                }
                // A gradient made of another's stops.
                if (const auto link = attribute(*found->second, "href")) return self(std::format("url({})", *link), inherited);
                return std::array<double, 3>{0.0, 0.0, 0.0};
            }
            if (text.starts_with('#')) {
                const std::string_view digits = text.substr(1);
                unsigned value = 0;
                std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
                if (digits.size() == 3) {
                    return std::array<double, 3>{((value >> 8) & 0xF) / 15.0, ((value >> 4) & 0xF) / 15.0, (value & 0xF) / 15.0};
                }
                return std::array<double, 3>{((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0, (value & 0xFF) / 255.0};
            }
            if (text.starts_with("rgb")) {
                const std::size_t open = text.find('(');
                const std::string_view inside = text.substr(open + 1, text.find(')') - open - 1);
                const std::vector<double> parts = numbers(inside);
                const bool shares = inside.contains('%');
                if (parts.size() < 3) return std::array<double, 3>{0.0, 0.0, 0.0};
                const double scale = shares ? 100.0 : 255.0;
                return std::array<double, 3>{parts[0] / scale, parts[1] / scale, parts[2] / scale};
            }
            static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 40> names{{
                {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000}, {"blue", 0x0000FF},
                {"yellow", 0xFFFF00}, {"cyan", 0x00FFFF}, {"aqua", 0x00FFFF}, {"magenta", 0xFF00FF}, {"fuchsia", 0xFF00FF},
                {"gray", 0x808080}, {"grey", 0x808080}, {"silver", 0xC0C0C0}, {"maroon", 0x800000}, {"olive", 0x808000},
                {"lime", 0x00FF00}, {"navy", 0x000080}, {"purple", 0x800080}, {"teal", 0x008080}, {"orange", 0xFFA500},
                {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9},
                {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3}, {"darkblue", 0x00008B}, {"darkred", 0x8B0000},
                {"darkgreen", 0x006400}, {"lightblue", 0xADD8E6}, {"steelblue", 0x4682B4}, {"tomato", 0xFF6347},
                {"violet", 0xEE82EE}, {"indigo", 0x4B0082}, {"crimson", 0xDC143C}, {"coral", 0xFF7F50},
                {"salmon", 0xFA8072}, {"khaki", 0xF0E68C}, {"beige", 0xF5F5DC},
            }};
            for (const auto& [name, value] : names) {
                if (text == name) {
                    return std::array<double, 3>{((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0, (value & 0xFF) / 255.0};
                }
            }
            return std::array<double, 3>{0.0, 0.0, 0.0};
        };
        // The matrix that does `first` and then `then`.
        const auto compose = [](const std::array<double, 6>& first, const std::array<double, 6>& then) {
            return std::array<double, 6>{then[0] * first[0] + then[2] * first[1], then[1] * first[0] + then[3] * first[1],
                                         then[0] * first[2] + then[2] * first[3], then[1] * first[2] + then[3] * first[3],
                                         then[0] * first[4] + then[2] * first[5] + then[4],
                                         then[1] * first[4] + then[3] * first[5] + then[5]};
        };
        // A `transform`'s list, each applied before those written left of it.
        const auto transform = [&](const std::string_view text, std::array<double, 6> matrix) {
            const double turn = std::numbers::pi / 180.0;
            for (std::size_t at = 0; at < text.size();) {
                const std::size_t open = text.find('(', at);
                if (open == std::string_view::npos) break;
                const std::size_t close = text.find(')', open);
                std::string_view kind = text.substr(at, open - at);
                while (!kind.empty() && (blank(kind.front()) || kind.front() == ',')) kind.remove_prefix(1);
                while (!kind.empty() && blank(kind.back())) kind.remove_suffix(1);
                const std::vector<double> values = numbers(text.substr(open + 1, (close == std::string_view::npos ? text.size() : close) - open - 1));
                const auto value = [&values](const std::size_t which, const double fallback) {
                    return which < values.size() ? values[which] : fallback;
                };
                std::array<double, 6> step{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                if (kind == "matrix" && values.size() == 6) {
                    std::ranges::copy(values, step.begin());
                } else if (kind == "translate") {
                    step[4] = value(0, 0.0);
                    step[5] = value(1, 0.0);
                } else if (kind == "scale") {
                    step[0] = value(0, 1.0);
                    step[3] = value(1, step[0]);
                } else if (kind == "rotate") {
                    const double angle = value(0, 0.0) * turn;
                    const double cx = value(1, 0.0);
                    const double cy = value(2, 0.0);
                    step = {std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle),
                            cx - std::cos(angle) * cx + std::sin(angle) * cy, cy - std::sin(angle) * cx - std::cos(angle) * cy};
                } else if (kind == "skewX") {
                    step[2] = std::tan(value(0, 0.0) * turn);
                } else if (kind == "skewY") {
                    step[1] = std::tan(value(0, 0.0) * turn);
                }
                matrix = compose(step, matrix);
                at = close == std::string_view::npos ? text.size() : close + 1;
            }
            return matrix;
        };

        // --- The page -------------------------------------------------------------------------

        // Its size from `width` and `height`, or from the `viewBox` -- a
        // pixel three quarters of a point -- and the view box fitted into
        // it, centred, as `xMidYMid meet` has it, y turned upward.
        const std::vector<double> view = numbers(attribute(*picture, "viewBox").value_or(""));
        const bool viewed = view.size() == 4 && view[2] > 0.0 && view[3] > 0.0;
        const double wide = length(attribute(*picture, "width").value_or(""), viewed ? view[2] * 0.75 : 300.0)
                                .value_or(viewed ? view[2] * 0.75 : 225.0);
        const double tall = length(attribute(*picture, "height").value_or(""), viewed ? view[3] * 0.75 : 150.0)
                                .value_or(viewed ? view[3] * 0.75 : 112.5);
        if (wide <= 0.0 || tall <= 0.0) return std::nullopt;
        const std::array<double, 4> frame = viewed ? std::array{view[0], view[1], view[2], view[3]}
                                                   : std::array{0.0, 0.0, wide / 0.75, tall / 0.75};
        const double fit = std::min(wide / frame[2], tall / frame[3]);
        const double across = (wide - fit * frame[2]) / 2.0;
        const double down = (tall - fit * frame[3]) / 2.0;
        const std::array<double, 6> base{fit, 0.0, 0.0, -fit, across - fit * frame[0], tall - down + fit * frame[1]};

        Drawing drawing;
        drawing.box = {0.0f, 0.0f, static_cast<float>(wide), static_cast<float>(tall)};
        std::string& out = drawing.content;
        std::vector<std::string> fonts;
        std::vector<std::pair<double, double>> alphas;   // each opacity for fills and for strokes, an ExtGState

        // A number as the content writes it.
        const auto put = [](std::string& into, const double value) {
            std::array<char, 32> buffer{};
            const double rounded = std::abs(value) < 0.0005 ? 0.0 : value;
            const auto [stop, failure] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), rounded, std::chars_format::fixed, 3);
            std::string_view written(buffer.data(), failure == std::errc{} ? static_cast<std::size_t>(stop - buffer.data()) : 0);
            while (written.contains('.') && (written.ends_with('0') || written.ends_with('.'))) {
                const bool dot = written.ends_with('.');
                written.remove_suffix(1);
                if (dot) break;
            }
            into += written.empty() ? std::string_view{"0"} : written;
            into += ' ';
        };

        // --- Drawing ----------------------------------------------------------------------------

        // What an element inherits and gives what is in it.
        struct Style {
            std::optional<std::array<double, 3>> fill{std::array<double, 3>{0.0, 0.0, 0.0}};
            std::optional<std::array<double, 3>> stroke{};
            std::optional<std::array<double, 3>> color{std::array<double, 3>{0.0, 0.0, 0.0}};
            double width{1.0};
            int cap{0};
            int join{0};
            std::vector<double> dashes{};
            double offset{0.0};
            double opacity{1.0};
            double filling{1.0};
            double stroking{1.0};
            bool odd{false};
            double size{16.0};
            std::string family{"sans-serif"};
            bool bold{false};
            bool italic{false};
            std::string anchor{"start"};
            bool hidden{false};
        };

        // One property onto a style, as an attribute or a declaration gives it.
        const auto apply = [&](Style& style, const std::string_view key, std::string_view value) {
            while (!value.empty() && blank(value.front())) value.remove_prefix(1);
            while (!value.empty() && (blank(value.back()) || value.back() == ';')) value.remove_suffix(1);
            if (value.ends_with("!important")) value.remove_suffix(10);
            const auto amount = [&value]() {
                double found = 1.0;
                std::from_chars(value.data(), value.data() + value.size(), found);
                return value.ends_with('%') ? found / 100.0 : found;
            };
            if (key == "fill") style.fill = colour(value, style.color);
            else if (key == "stroke") style.stroke = colour(value, style.color);
            else if (key == "color") style.color = colour(value, style.color);
            else if (key == "stroke-width") style.width = length(value).value_or(0.75) / 0.75;
            else if (key == "stroke-linecap") style.cap = value == "round" ? 1 : value == "square" ? 2 : 0;
            else if (key == "stroke-linejoin") style.join = value == "round" ? 1 : value == "bevel" ? 2 : 0;
            else if (key == "stroke-dasharray") style.dashes = value == "none" ? std::vector<double>{} : numbers(value);
            else if (key == "stroke-dashoffset") style.offset = length(value).value_or(0.0) / 0.75;
            else if (key == "opacity") style.opacity *= amount();
            else if (key == "fill-opacity") style.filling = amount();
            else if (key == "stroke-opacity") style.stroking = amount();
            else if (key == "fill-rule") style.odd = value == "evenodd";
            else if (key == "font-size") style.size = length(value, style.size * 0.75).value_or(style.size * 0.75) / 0.75;
            else if (key == "font-family") style.family = value;
            else if (key == "font-weight") style.bold = value == "bold" || value == "bolder" || (value.size() == 3 && value >= "600");
            else if (key == "font-style") style.italic = value == "italic" || value == "oblique";
            else if (key == "text-anchor") style.anchor = value;
            else if (key == "display") style.hidden = style.hidden || value == "none";
            else if (key == "visibility") style.hidden = value == "hidden" || value == "collapse";
            else if (key == "font") {
                // The shorthand: its size and its family are what are read.
                for (const double size : numbers(value)) {
                    style.size = size;
                    break;
                }
            }
        };
        // A `style`'s or a rule's declarations onto a style.
        const auto declare = [&](Style& style, const std::string_view declarations) {
            for (std::size_t at = 0; at < declarations.size();) {
                std::size_t end = declarations.find(';', at);
                if (end == std::string_view::npos) end = declarations.size();
                const std::string_view declaration = declarations.substr(at, end - at);
                if (const std::size_t colon = declaration.find(':'); colon != std::string_view::npos) {
                    std::string_view key = declaration.substr(0, colon);
                    while (!key.empty() && blank(key.front())) key.remove_prefix(1);
                    while (!key.empty() && blank(key.back())) key.remove_suffix(1);
                    apply(style, key, declaration.substr(colon + 1));
                }
                at = end + 1;
            }
        };
        // An element's own style: what it inherits, its presentation
        // attributes, the rules naming it, its `style`.
        const auto styled = [&](const Element& element, Style style) {
            static constexpr std::array<std::string_view, 19> properties{
                "fill", "stroke", "color", "stroke-width", "stroke-linecap", "stroke-linejoin", "stroke-dasharray",
                "stroke-dashoffset", "opacity", "fill-opacity", "stroke-opacity", "fill-rule", "font-size", "font-family",
                "font-weight", "font-style", "text-anchor", "display", "visibility",
            };
            for (const auto& [key, value] : element.attributes) {
                if (std::ranges::contains(properties, key)) apply(style, key, value);
            }
            const std::string_view classes = attribute(element, "class").value_or("");
            const std::string_view id = attribute(element, "id").value_or("");
            for (const auto& [selector, declarations] : rules) {
                bool matched = selector == element.name || selector == "*" || (selector.starts_with('#') && selector.substr(1) == id);
                if (selector.starts_with('.')) {
                    for (std::size_t at = 0; at <= classes.size() && !matched;) {
                        std::size_t end = classes.find(' ', at);
                        if (end == std::string_view::npos) end = classes.size();
                        matched = classes.substr(at, end - at) == std::string_view(selector).substr(1);
                        at = end + 1;
                    }
                }
                if (matched) declare(style, declarations);
            }
            if (const auto written = attribute(element, "style")) declare(style, *written);
            return style;
        };

        // A path's commands in the page's points, for a matrix: every SVG
        // command, a smooth curve's first control the last one mirrored, a
        // quadratic raised to a cubic, an arc in cubic quarters at most.
        const auto trace = [&](const std::string_view data, const std::array<double, 6>& m) {
            std::string path;
            const auto point = [&](const double x, const double y) {
                put(path, m[0] * x + m[2] * y + m[4]);
                put(path, m[1] * x + m[3] * y + m[5]);
            };
            double x = 0.0, y = 0.0, startx = 0.0, starty = 0.0, pullx = 0.0, pully = 0.0;
            char last = ' ';
            const auto cubic = [&](const double ax, const double ay, const double bx, const double by, const double ex, const double ey) {
                point(ax, ay);
                point(bx, by);
                point(ex, ey);
                path += "c\n";
                pullx = bx;
                pully = by;
                x = ex;
                y = ey;
            };
            char command = ' ';
            for (std::size_t at = 0; at < data.size();) {
                const char letter = data[at];
                if (blank(letter) || letter == ',') {
                    ++at;
                    continue;
                }
                if (std::string_view("MmLlHhVvCcSsQqTtAaZz").contains(letter)) {
                    command = letter;
                    ++at;
                    if (command == 'Z' || command == 'z') {
                        path += "h\n";
                        x = startx;
                        y = starty;
                        last = command;
                    }
                    continue;
                }
                // The command's numbers: an arc's flags, one digit each, may
                // run on into what follows them.
                const std::size_t wanted = std::string_view("MmLlTt").contains(command) ? 2 : std::string_view("HhVv").contains(command) ? 1
                                         : std::string_view("SsQq").contains(command) ? 4 : std::string_view("Cc").contains(command) ? 6
                                         : std::string_view("Aa").contains(command) ? 7 : 0;
                if (wanted == 0) {
                    ++at;
                    continue;
                }
                std::array<double, 7> values{};
                std::size_t read = 0;
                while (read < wanted && at < data.size()) {
                    while (at < data.size() && (blank(data[at]) || data[at] == ',')) ++at;
                    if (at >= data.size()) break;
                    if ((command == 'A' || command == 'a') && (read == 3 || read == 4) && (data[at] == '0' || data[at] == '1')) {
                        values[read++] = data[at++] - '0';
                        continue;
                    }
                    const char* start = data.data() + at;
                    if (*start == '+') ++start;
                    const auto [stop, failure] = std::from_chars(start, data.data() + data.size(), values[read]);
                    if (failure != std::errc{}) break;
                    at = static_cast<std::size_t>(stop - data.data());
                    ++read;
                }
                if (read < wanted) break;
                const bool relative = command >= 'a' && command <= 'z';
                const double ox = relative ? x : 0.0;
                const double oy = relative ? y : 0.0;
                switch (command) {
                    case 'M':
                    case 'm':
                        x = startx = values[0] + ox;
                        y = starty = values[1] + oy;
                        point(x, y);
                        path += "m\n";
                        // Further pairs after a move are lines.
                        command = relative ? 'l' : 'L';
                        break;
                    case 'L':
                    case 'l':
                    case 'H':
                    case 'h':
                    case 'V':
                    case 'v':
                        if (command == 'H' || command == 'h') x = values[0] + ox;
                        else if (command == 'V' || command == 'v') y = values[0] + oy;
                        else {
                            x = values[0] + ox;
                            y = values[1] + oy;
                        }
                        point(x, y);
                        path += "l\n";
                        break;
                    case 'C':
                    case 'c':
                        cubic(values[0] + ox, values[1] + oy, values[2] + ox, values[3] + oy, values[4] + ox, values[5] + oy);
                        break;
                    case 'S':
                    case 's': {
                        const bool smooth = std::string_view("CcSs").contains(last);
                        cubic(smooth ? 2.0 * x - pullx : x, smooth ? 2.0 * y - pully : y, values[0] + ox, values[1] + oy, values[2] + ox,
                              values[3] + oy);
                        break;
                    }
                    case 'Q':
                    case 'q':
                    case 'T':
                    case 't': {
                        const bool smooth = (command == 'T' || command == 't') && std::string_view("QqTt").contains(last);
                        const double qx = command == 'Q' || command == 'q' ? values[0] + ox : smooth ? 2.0 * x - pullx : x;
                        const double qy = command == 'Q' || command == 'q' ? values[1] + oy : smooth ? 2.0 * y - pully : y;
                        const double ex = (command == 'Q' || command == 'q' ? values[2] : values[0]) + ox;
                        const double ey = (command == 'Q' || command == 'q' ? values[3] : values[1]) + oy;
                        const double sx = x;
                        const double sy = y;
                        cubic(sx + 2.0 / 3.0 * (qx - sx), sy + 2.0 / 3.0 * (qy - sy), ex + 2.0 / 3.0 * (qx - ex), ey + 2.0 / 3.0 * (qy - ey),
                              ex, ey);
                        pullx = qx;
                        pully = qy;
                        break;
                    }
                    case 'A':
                    case 'a': {
                        // The endpoints' arc turned to its centre's, as the SVG
                        // specification's appendix works it out.
                        double rx = std::abs(values[0]);
                        double ry = std::abs(values[1]);
                        const double phi = values[2] * std::numbers::pi / 180.0;
                        const bool large = values[3] != 0.0;
                        const bool sweep = values[4] != 0.0;
                        const double ex = values[5] + ox;
                        const double ey = values[6] + oy;
                        if (rx < 1e-9 || ry < 1e-9 || (ex == x && ey == y)) {
                            point(ex, ey);
                            path += "l\n";
                            x = ex;
                            y = ey;
                            break;
                        }
                        const double c = std::cos(phi);
                        const double s = std::sin(phi);
                        const double dx = (x - ex) / 2.0;
                        const double dy = (y - ey) / 2.0;
                        const double px = c * dx + s * dy;
                        const double py = -s * dx + c * dy;
                        const double grow = px * px / (rx * rx) + py * py / (ry * ry);
                        if (grow > 1.0) {
                            rx *= std::sqrt(grow);
                            ry *= std::sqrt(grow);
                        }
                        const double top = rx * rx * ry * ry - rx * rx * py * py - ry * ry * px * px;
                        const double bottom = rx * rx * py * py + ry * ry * px * px;
                        double root = bottom > 0.0 ? std::sqrt(std::max(0.0, top / bottom)) : 0.0;
                        if (large == sweep) root = -root;
                        const double cxp = root * rx * py / ry;
                        const double cyp = -root * ry * px / rx;
                        const double cx = c * cxp - s * cyp + (x + ex) / 2.0;
                        const double cy = s * cxp + c * cyp + (y + ey) / 2.0;
                        const auto angle = [](const double ux, const double uy, const double vx, const double vy) {
                            return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
                        };
                        const double from = angle(1.0, 0.0, (px - cxp) / rx, (py - cyp) / ry);
                        double turn = angle((px - cxp) / rx, (py - cyp) / ry, (-px - cxp) / rx, (-py - cyp) / ry);
                        if (!sweep && turn > 0.0) turn -= 2.0 * std::numbers::pi;
                        if (sweep && turn < 0.0) turn += 2.0 * std::numbers::pi;
                        const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(turn) / (std::numbers::pi / 2.0) - 1e-9)));
                        const double step = turn / pieces;
                        const double pull = 4.0 / 3.0 * std::tan(step / 4.0);
                        const auto on = [&](const double t) {
                            return std::pair{cx + c * rx * std::cos(t) - s * ry * std::sin(t), cy + s * rx * std::cos(t) + c * ry * std::sin(t)};
                        };
                        const auto along = [&](const double t) {
                            return std::pair{-c * rx * std::sin(t) - s * ry * std::cos(t), -s * rx * std::sin(t) + c * ry * std::cos(t)};
                        };
                        for (int piece = 0; piece < pieces; ++piece) {
                            const double a = from + step * piece;
                            const double b = a + step;
                            const auto [ax, ay] = on(a);
                            const auto [bx, by] = on(b);
                            const auto [ta, ua] = along(a);
                            const auto [tb, ub] = along(b);
                            cubic(ax + pull * ta, ay + pull * ua, bx - pull * tb, by - pull * ub, piece + 1 == pieces ? ex : bx,
                                  piece + 1 == pieces ? ey : by);
                        }
                        break;
                    }
                    default:
                        break;
                }
                last = command;
            }
            return path;
        };

        // A path filled and stroked as its style says, at its opacities.
        const auto paint = [&](const std::string& path, const Style& style, const std::array<double, 6>& m) {
            if (path.empty() || style.hidden) return;
            const bool filled = style.fill.has_value();
            const bool stroked = style.stroke.has_value() && style.width > 0.0;
            if (!filled && !stroked) return;
            out += "q\n";
            const double fills = style.opacity * style.filling;
            const double strokes = style.opacity * style.stroking;
            if (fills < 0.999 || strokes < 0.999) {
                const std::pair<double, double> alpha{fills, strokes};
                auto found = std::ranges::find(alphas, alpha);
                if (found == alphas.end()) {
                    alphas.push_back(alpha);
                    found = alphas.end() - 1;
                }
                out += "/G" + std::to_string(found - alphas.begin()) + " gs\n";
            }
            if (filled) {
                for (const double channel : *style.fill) put(out, channel);
                out += "rg\n";
            }
            if (stroked) {
                const double scale = std::sqrt(std::abs(m[0] * m[3] - m[1] * m[2]));
                for (const double channel : *style.stroke) put(out, channel);
                out += "RG\n";
                put(out, style.width * scale);
                out += "w " + std::to_string(style.cap) + " J " + std::to_string(style.join) + " j [";
                for (const double dash : style.dashes) put(out, dash * scale);
                out += "] ";
                put(out, style.offset * scale);
                out += "d\n";
            }
            out += path;
            out += filled && stroked ? (style.odd ? "B*" : "B") : filled ? (style.odd ? "f*" : "f") : "S";
            out += "\nQ\n";
        };

        // An element and everything in it, under a matrix and a style.
        const auto draw = [&](this const auto& self, const Element& element, std::array<double, 6> m, const Style& inherited,
                              const int depth) -> void {
            if (depth > 64) return;
            const Style style = styled(element, inherited);
            if (style.hidden || element.name == "defs" || element.name == "clipPath" || element.name == "mask" ||
                element.name == "symbol" || element.name == "style" || element.name == "title" || element.name == "desc" ||
                element.name == "metadata" || element.name.ends_with("Gradient") || element.name == "pattern" ||
                element.name == "marker" || element.name == "filter") {
                return;
            }
            if (const auto moved = attribute(element, "transform")) m = transform(*moved, m);
            const auto value = [&](const std::string_view key, const double whole = 0.0) {
                return length(attribute(element, key).value_or("0"), whole * 0.75).value_or(0.0) / 0.75;
            };

            // A clip, `clip-path="url(#id)"`: the paths its clip path holds,
            // in this element's own space.
            bool clipped = false;
            if (const auto clip = attribute(element, "clip-path"); clip && clip->starts_with("url(")) {
                std::string_view target = clip->substr(4, clip->find(')') - 4);
                if (target.starts_with('#')) target.remove_prefix(1);
                if (const auto found = named.find(target); found != named.end()) {
                    std::string region;
                    for (const Element& part : found->second->children) {
                        const std::array<double, 6> inner = attribute(part, "transform") ? transform(*attribute(part, "transform"), m) : m;
                        const auto measure = [&](const std::string_view key) {
                            return length(attribute(part, key).value_or("0")).value_or(0.0) / 0.75;
                        };
                        if (part.name == "rect") {
                            const double x = measure("x"), y = measure("y"), w = measure("width"), h = measure("height");
                            region += trace(std::format("M{} {}H{}V{}H{}Z", x, y, x + w, y + h, x), inner);
                        } else if (part.name == "path") {
                            region += trace(attribute(part, "d").value_or(""), inner);
                        } else if (part.name == "circle") {
                            const double cx = measure("cx"), cy = measure("cy"), r = measure("r");
                            region += trace(std::format("M{} {}A{} {} 0 1 0 {} {}A{} {} 0 1 0 {} {}Z", cx - r, cy, r, r, cx + r, cy, r, r,
                                                        cx - r, cy), inner);
                        }
                    }
                    if (!region.empty()) {
                        out += "q\n" + region + "W n\n";
                        clipped = true;
                    }
                }
            }

            const double fullx = frame[2];
            const double fully = frame[3];
            if (element.name == "svg" || element.name == "g" || element.name == "a" || element.name == "switch") {
                if (element.name == "svg" && &element != picture) {
                    // A picture inside the picture: its place and its own view.
                    m = compose({1.0, 0.0, 0.0, 1.0, value("x"), value("y")}, m);
                    const std::vector<double> inner = numbers(attribute(element, "viewBox").value_or(""));
                    if (inner.size() == 4 && inner[2] > 0.0 && inner[3] > 0.0) {
                        const double w = value("width", fullx);
                        const double h = value("height", fully);
                        const double scale = std::min(w > 0.0 ? w / inner[2] : 1.0, h > 0.0 ? h / inner[3] : 1.0);
                        m = compose({scale, 0.0, 0.0, scale, -inner[0] * scale, -inner[1] * scale}, m);
                    }
                }
                for (const Element& child : element.children) self(child, m, style, depth + 1);
            } else if (element.name == "use") {
                std::string_view target = attribute(element, "href").value_or("");
                if (target.starts_with('#')) target.remove_prefix(1);
                if (const auto found = named.find(target); found != named.end()) {
                    const std::array<double, 6> placed = compose({1.0, 0.0, 0.0, 1.0, value("x"), value("y")}, m);
                    if (found->second->name == "symbol") {
                        for (const Element& child : found->second->children) self(child, placed, style, depth + 1);
                    } else {
                        self(*found->second, placed, style, depth + 1);
                    }
                }
            } else if (element.name == "path") {
                paint(trace(attribute(element, "d").value_or(""), m), style, m);
            } else if (element.name == "rect") {
                const double x = value("x", fullx), y = value("y", fully), w = value("width", fullx), h = value("height", fully);
                double rx = value("rx", fullx);
                double ry = value("ry", fully);
                if (attribute(element, "rx") && !attribute(element, "ry")) ry = rx;
                if (attribute(element, "ry") && !attribute(element, "rx")) rx = ry;
                rx = std::min(rx, w / 2.0);
                ry = std::min(ry, h / 2.0);
                if (w > 0.0 && h > 0.0) {
                    const std::string data = rx > 0.0 && ry > 0.0
                        ? std::format("M{} {}H{}A{} {} 0 0 1 {} {}V{}A{} {} 0 0 1 {} {}H{}A{} {} 0 0 1 {} {}V{}A{} {} 0 0 1 {} {}Z",
                                      x + rx, y, x + w - rx, rx, ry, x + w, y + ry, y + h - ry, rx, ry, x + w - rx, y + h, x + rx,
                                      rx, ry, x, y + h - ry, y + ry, rx, ry, x + rx, y)
                        : std::format("M{} {}H{}V{}H{}Z", x, y, x + w, y + h, x);
                    paint(trace(data, m), style, m);
                }
            } else if (element.name == "circle" || element.name == "ellipse") {
                const double cx = value("cx", fullx);
                const double cy = value("cy", fully);
                const double rx = element.name == "circle" ? value("r", std::hypot(fullx, fully) / std::numbers::sqrt2) : value("rx", fullx);
                const double ry = element.name == "circle" ? rx : value("ry", fully);
                if (rx > 0.0 && ry > 0.0) {
                    paint(trace(std::format("M{} {}A{} {} 0 1 0 {} {}A{} {} 0 1 0 {} {}Z", cx - rx, cy, rx, ry, cx + rx, cy, rx, ry,
                                            cx - rx, cy), m),
                          style, m);
                }
            } else if (element.name == "line") {
                Style lined = style;
                lined.fill.reset();
                paint(trace(std::format("M{} {}L{} {}", value("x1", fullx), value("y1", fully), value("x2", fullx), value("y2", fully)), m),
                      lined, m);
            } else if (element.name == "polyline" || element.name == "polygon") {
                const std::vector<double> points = numbers(attribute(element, "points").value_or(""));
                std::string data;
                for (std::size_t at = 0; at + 1 < points.size(); at += 2) data += std::format("{}{} {}", at == 0 ? "M" : "L", points[at], points[at + 1]);
                if (element.name == "polygon" && !data.empty()) data += "Z";
                paint(trace(data, m), style, m);
            } else if (element.name == "text") {
                // Its words and its spans', each from where the last ended or
                // where it says, in the standard face its family is nearest,
                // from its anchor.
                double x = value("x", fullx);
                double y = value("y", fully);
                // Blanks collapse to one space across the runs, none before
                // the first word: a space waits for the word after it. The
                // runs are measured first, and set from where the text's
                // anchor puts the whole of them.
                bool started = false;
                bool waiting = false;
                bool measuring = true;
                double measured = 0.0;
                const auto set = [&](const std::string_view words, const Style& look) {
                    std::string letters;
                    for (const char letter : words) {
                        if (blank(letter)) {
                            waiting = started;
                            continue;
                        }
                        if (waiting) letters += ' ';
                        waiting = false;
                        started = true;
                        letters += letter;
                    }
                    if (letters.empty() || !look.fill || look.hidden) return;
                    const bool serif = look.family.contains("serif") && !look.family.contains("sans");
                    const bool fixed = look.family.contains("mono") || look.family.contains("Courier") || look.family.contains("courier");
                    const bool times = serif || look.family.contains("Times") || look.family.contains("times") ||
                                       look.family.contains("Georgia");
                    const std::string face = fixed ? (look.bold && look.italic ? "Courier-BoldOblique" : look.bold ? "Courier-Bold"
                                                      : look.italic ? "Courier-Oblique" : "Courier")
                                           : times ? (look.bold && look.italic ? "Times-BoldItalic" : look.bold ? "Times-Bold"
                                                      : look.italic ? "Times-Italic" : "Times-Roman")
                                                   : (look.bold && look.italic ? "Helvetica-BoldOblique" : look.bold ? "Helvetica-Bold"
                                                      : look.italic ? "Helvetica-Oblique" : "Helvetica");
                    auto found = std::ranges::find(fonts, face);
                    if (found == fonts.end()) {
                        fonts.push_back(face);
                        found = fonts.end() - 1;
                    }
                    const double extent = breadth(face, letters) / 1000.0 * look.size;
                    const double start = x;
                    x = start + extent;
                    if (measuring) {
                        measured += extent;
                        return;
                    }
                    out += "q\n";
                    for (const double channel : *look.fill) put(out, channel);
                    out += "rg\nBT /F" + std::to_string(found - fonts.begin()) + " 1 Tf ";
                    for (const double part : {m[0] * look.size, m[1] * look.size, -m[2] * look.size, -m[3] * look.size,
                                              m[0] * start + m[2] * y + m[4], m[1] * start + m[3] * y + m[5]}) {
                        put(out, part);
                    }
                    out += "Tm (";
                    for (const char letter : letters) {
                        const auto code = static_cast<unsigned char>(letter);
                        if (letter == '(' || letter == ')' || letter == '\\') {
                            out += '\\';
                            out += letter;
                        } else if (code < 32 || code >= 127) {
                            out += std::format("\\{:03o}", code);
                        } else {
                            out += letter;
                        }
                    }
                    out += ") Tj ET\nQ\n";
                };
                // Its runs and its spans in their order, a span's own place
                // and style for what is in it.
                const auto walk = [&](this const auto& again, const Element& holder, const Style& look) -> void {
                    for (const Element& part : holder.children) {
                        if (part.name == "#text") {
                            set(part.text, look);
                        } else if (part.name == "tspan") {
                            if (const auto across = attribute(part, "x")) x = length(*across).value_or(x * 0.75) / 0.75;
                            if (const auto upward = attribute(part, "y")) y = length(*upward).value_or(y * 0.75) / 0.75;
                            x += length(attribute(part, "dx").value_or("0")).value_or(0.0) / 0.75;
                            y += length(attribute(part, "dy").value_or("0")).value_or(0.0) / 0.75;
                            again(part, styled(part, look));
                        }
                    }
                };
                const double fromx = x;
                const double fromy = y;
                walk(element, style);
                x = fromx - (style.anchor == "middle" ? measured / 2.0 : style.anchor == "end" ? measured : 0.0);
                y = fromy;
                started = waiting = measuring = false;
                walk(element, style);
            }
            if (clipped) out += "Q\n";
        };
        draw(*picture, base, Style{}, 0);

        // The faces its text is set in, and its opacities.
        drawing.resources = faces(fonts);
        if (!alphas.empty()) {
            using Type = Value::Type;
            if (drawing.resources.type != Type::Table) drawing.resources = Value{.type = Type::Table};
            Value states{.type = Type::Table};
            for (std::size_t slot = 0; slot < alphas.size(); ++slot) {
                Value entry{.type = Type::Table};
                std::string fill;
                std::string stroke;
                put(fill, alphas[slot].first);
                put(stroke, alphas[slot].second);
                entry.items = {{.type = Type::Name, .text = "/ca"}, {.type = Type::Plain, .text = fill},
                               {.type = Type::Name, .text = "/CA"}, {.type = Type::Plain, .text = stroke}};
                states.items.push_back({.type = Type::Name, .text = "/G" + std::to_string(slot)});
                states.items.push_back(std::move(entry));
            }
            drawing.resources.items.push_back({.type = Type::Name, .text = "/ExtGState"});
            drawing.resources.items.push_back(std::move(states));
        }
        return drawing;
    }

}
