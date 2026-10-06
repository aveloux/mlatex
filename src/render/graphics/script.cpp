/// @file
/// @brief Drawing's PostScript: an EPS file's program run into the content
///        of a page as large as its picture.
///
/// script() is a small PostScript interpreter: the program's objects read as
/// PostScript reads them, its stack, its dictionaries and its procedures, and
/// the operators a figure's program uses -- arithmetic, control, dictionaries,
/// the path and its painting, colour, transformations, text and images. What
/// it paints is written as PDF's operators in the page's own points: every
/// path is kept already through the transformation it was built under, as
/// PostScript keeps it, so no transformation is written and none is needed.
#include "render/graphics/drawing.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace render::graphics {

    std::optional<Drawing> Drawing::script(const std::string_view file) {
        // --- The picture's box ---------------------------------------------------------------

        // `%%HiResBoundingBox:` if it gives one, `%%BoundingBox:` otherwise;
        // one that says `(atend)` is looked for again further on.
        std::array<float, 4> box{};
        bool boxed = false;
        for (const std::string_view key : {std::string_view{"%%HiResBoundingBox:"}, std::string_view{"%%BoundingBox:"}}) {
            for (std::size_t at = file.find(key); at != std::string_view::npos && !boxed; at = file.find(key, at + key.size())) {
                const char* cursor = file.data() + at + key.size();
                const char* const end = file.data() + file.size();
                std::size_t found = 0;
                for (float& corner : box) {
                    while (cursor < end && (*cursor == ' ' || *cursor == '\t')) ++cursor;
                    const auto [stop, failure] = std::from_chars(cursor, end, corner);
                    if (failure != std::errc{}) break;
                    cursor = stop;
                    ++found;
                }
                boxed = found == 4 && box[2] > box[0] && box[3] > box[1];
            }
            if (boxed) break;
        }
        if (!boxed) return std::nullopt;

        Drawing drawing;
        drawing.box = box;
        std::string& out = drawing.content;

        // --- Its objects ---------------------------------------------------------------------

        // What the stack and the dictionaries hold. A name is run when it is
        // met; a literal, `/name`, is pushed. A procedure met in a procedure
        // is pushed, and run when something runs it.
        struct Object {
            enum class Kind : std::uint8_t { Null, Number, Boolean, Name, Literal, Text, Procedure, Array, Mark, Table, Font, Source };
            Kind kind{Kind::Null};
            double number{0.0};                                       // a number's value, a boolean's 1 or 0, a font's size
            std::string text{};                                       // a name's letters, a string's bytes, a font's name
            std::shared_ptr<std::vector<Object>> items{};             // a procedure's or an array's
            std::shared_ptr<std::map<std::string, Object>> table{};   // a dictionary's
        };
        using Kind = Object::Kind;
        using Table = std::map<std::string, Object>;

        const auto delimiter = [](const char letter) {
            return letter == ' ' || letter == '\n' || letter == '\r' || letter == '\t' || letter == '\f' || letter == '\0' ||
                   std::string_view("()<>[]{}/%").contains(letter);
        };
        // Past blanks and comments.
        const auto blank = [](const std::string_view text, std::size_t& at) {
            while (at < text.size()) {
                const char letter = text[at];
                if (letter == '%') {
                    while (at < text.size() && text[at] != '\n' && text[at] != '\r') ++at;
                } else if (letter == ' ' || letter == '\n' || letter == '\r' || letter == '\t' || letter == '\f' || letter == '\0') {
                    ++at;
                } else {
                    break;
                }
            }
        };
        // The next object written from a place, a procedure read whole; none
        // at the end.
        const auto read = [&](this const auto& self, const std::string_view text, std::size_t& at) -> std::optional<Object> {
            blank(text, at);
            if (at >= text.size()) return std::nullopt;
            const char head = text[at];
            if (head == '(') {
                // A string: parentheses nest, and a backslash escapes a
                // letter, a line's end, or three octal digits.
                std::string letters;
                int depth = 0;
                ++at;
                while (at < text.size()) {
                    const char letter = text[at++];
                    if (letter == '\\' && at < text.size()) {
                        const char escaped = text[at++];
                        if (escaped >= '0' && escaped <= '7') {
                            int code = escaped - '0';
                            for (int digit = 0; digit < 2 && at < text.size() && text[at] >= '0' && text[at] <= '7'; ++digit) {
                                code = code * 8 + (text[at++] - '0');
                            }
                            letters += static_cast<char>(code);
                        } else if (escaped == '\r' || escaped == '\n') {
                            if (escaped == '\r' && at < text.size() && text[at] == '\n') ++at;
                        } else {
                            letters += escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped == 't' ? '\t' : escaped == 'b' ? '\b'
                                     : escaped == 'f' ? '\f' : escaped;
                        }
                    } else if (letter == '(') {
                        ++depth;
                        letters += letter;
                    } else if (letter == ')') {
                        if (depth == 0) break;
                        --depth;
                        letters += letter;
                    } else {
                        letters += letter;
                    }
                }
                return Object{.kind = Kind::Text, .text = std::move(letters)};
            }
            if (text.substr(at).starts_with("<<") || text.substr(at).starts_with(">>")) {
                at += 2;
                return Object{.kind = Kind::Name, .text = std::string(text.substr(at - 2, 2))};
            }
            if (head == '<') {
                // In hexadecimal; in ASCII85, `<~ ~>`, its bytes let go.
                ++at;
                if (at < text.size() && text[at] == '~') {
                    const std::size_t end = text.find("~>", at);
                    at = end == std::string_view::npos ? text.size() : end + 2;
                    return Object{.kind = Kind::Text};
                }
                std::string letters;
                int pending = -1;
                for (; at < text.size() && text[at] != '>'; ++at) {
                    const char letter = text[at];
                    const int value = letter >= '0' && letter <= '9' ? letter - '0'
                                    : letter >= 'a' && letter <= 'f' ? letter - 'a' + 10
                                    : letter >= 'A' && letter <= 'F' ? letter - 'A' + 10 : -1;
                    if (value < 0) continue;
                    if (pending < 0) {
                        pending = value;
                    } else {
                        letters += static_cast<char>(pending * 16 + value);
                        pending = -1;
                    }
                }
                if (pending >= 0) letters += static_cast<char>(pending * 16);
                ++at;
                return Object{.kind = Kind::Text, .text = std::move(letters)};
            }
            if (head == '{') {
                ++at;
                auto items = std::make_shared<std::vector<Object>>();
                while (true) {
                    blank(text, at);
                    if (at >= text.size()) break;
                    if (text[at] == '}') {
                        ++at;
                        break;
                    }
                    std::optional<Object> item = self(text, at);
                    if (!item) break;
                    items->push_back(std::move(*item));
                }
                return Object{.kind = Kind::Procedure, .items = std::move(items)};
            }
            if (head == '}' || head == ')' || head == '>') {
                ++at;
                return Object{};
            }
            if (head == '[' || head == ']') {
                ++at;
                return Object{.kind = Kind::Name, .text = std::string(1, head)};
            }
            const bool literal = head == '/';
            if (literal) {
                ++at;
                if (at < text.size() && text[at] == '/') ++at;   // `//name`, read as the name
            }
            const std::size_t start = at;
            while (at < text.size() && !delimiter(text[at])) ++at;
            const std::string_view word = text.substr(start, at - start);
            if (literal) return Object{.kind = Kind::Literal, .text = std::string(word)};
            double value = 0.0;
            if (const auto [stop, failure] = std::from_chars(word.data(), word.data() + word.size(), value);
                failure == std::errc{} && stop == word.data() + word.size()) {
                return Object{.kind = Kind::Number, .number = value};
            }
            // A number in a radix, `16#FF`.
            if (const std::size_t hash = word.find('#'); hash != std::string_view::npos && hash > 0) {
                int radix = 10;
                long long whole = 0;
                std::from_chars(word.data(), word.data() + hash, radix);
                if (radix >= 2 && radix <= 36 &&
                    std::from_chars(word.data() + hash + 1, word.data() + word.size(), whole, radix).ec == std::errc{}) {
                    return Object{.kind = Kind::Number, .number = static_cast<double>(whole)};
                }
            }
            if (word.empty()) {
                ++at;
                return Object{};
            }
            return Object{.kind = Kind::Name, .text = std::string(word)};
        };

        // --- The graphics state --------------------------------------------------------------

        // The transformation, a colour for everything painted, the line's
        // width, ends, joins and dashes, the font, and the path and its
        // current point -- all in the page's points once through the
        // transformation, as PostScript keeps them.
        struct State {
            std::array<double, 6> matrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            std::array<double, 3> color{0.0, 0.0, 0.0};
            std::size_t components{1};        // how many numbers `setcolor` takes
            double width{1.0};
            int cap{0};
            int join{0};
            std::vector<double> dashes{};
            double phase{0.0};
            std::string face{"Helvetica"};
            double size{10.0};
            std::string path{};
            bool placed{false};
            double x{0.0};
            double y{0.0};
            double startx{0.0};
            double starty{0.0};
        };
        State state;
        std::vector<State> saved;
        std::vector<std::string> fonts;

        // A number as the content writes it: three places, no trailing zeros.
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
        // A point of user space on the page, and a distance in it.
        const auto device = [&state](const double x, const double y) {
            const std::array<double, 6>& m = state.matrix;
            return std::pair{m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]};
        };
        const auto distance = [&state](const double x, const double y) {
            const std::array<double, 6>& m = state.matrix;
            return std::pair{m[0] * x + m[2] * y, m[1] * x + m[3] * y};
        };
        // The matrix that does `first` and then `then`, and one undone.
        const auto compose = [](const std::array<double, 6>& first, const std::array<double, 6>& then) {
            return std::array<double, 6>{then[0] * first[0] + then[2] * first[1], then[1] * first[0] + then[3] * first[1],
                                         then[0] * first[2] + then[2] * first[3], then[1] * first[2] + then[3] * first[3],
                                         then[0] * first[4] + then[2] * first[5] + then[4],
                                         then[1] * first[4] + then[3] * first[5] + then[5]};
        };
        const auto invert = [](const std::array<double, 6>& m) {
            const double determinant = m[0] * m[3] - m[1] * m[2];
            if (std::abs(determinant) < 1e-12) return std::array<double, 6>{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            return std::array<double, 6>{m[3] / determinant, -m[1] / determinant, -m[2] / determinant, m[0] / determinant,
                                         (m[2] * m[5] - m[3] * m[4]) / determinant, (m[1] * m[4] - m[0] * m[5]) / determinant};
        };
        // How much the transformation scales a length, on average.
        const auto scaling = [&state] {
            const std::array<double, 6>& m = state.matrix;
            return std::sqrt(std::abs(m[0] * m[3] - m[1] * m[2]));
        };

        const auto move = [&](const double x, const double y) {
            const auto [px, py] = device(x, y);
            put(state.path, px);
            put(state.path, py);
            state.path += "m\n";
            state.placed = true;
            state.x = state.startx = px;
            state.y = state.starty = py;
        };
        const auto line = [&](const double x, const double y) {
            const auto [px, py] = device(x, y);
            if (!state.placed) {
                move(x, y);
                return;
            }
            put(state.path, px);
            put(state.path, py);
            state.path += "l\n";
            state.x = px;
            state.y = py;
        };
        const auto curve = [&](const std::array<double, 6>& points) {
            for (std::size_t index = 0; index < 6; index += 2) {
                const auto [px, py] = device(points[index], points[index + 1]);
                put(state.path, px);
                put(state.path, py);
                if (index == 4) {
                    state.x = px;
                    state.y = py;
                }
            }
            state.path += "c\n";
        };
        // The current point back in user space.
        const auto current = [&] {
            const std::array<double, 6> back = invert(state.matrix);
            return std::pair{back[0] * state.x + back[2] * state.y + back[4], back[1] * state.x + back[3] * state.y + back[5]};
        };
        // An arc round a centre, against the clock or with it, in curves of
        // a quarter turn at most, from a line to its start.
        const auto arc = [&](const double cx, const double cy, const double radius, double from, double to, const bool clockwise) {
            if (clockwise) {
                while (to > from) to -= 360.0;
            } else {
                while (to < from) to += 360.0;
            }
            const double turn = std::numbers::pi / 180.0;
            const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(to - from) / 90.0 - 1e-9)));
            const double step = (to - from) / pieces * turn;
            double angle = from * turn;
            line(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
            for (int piece = 0; piece < pieces; ++piece) {
                const double next = angle + step;
                const double pull = 4.0 / 3.0 * std::tan(step / 4.0) * radius;
                curve({cx + radius * std::cos(angle) - pull * std::sin(angle), cy + radius * std::sin(angle) + pull * std::cos(angle),
                       cx + radius * std::cos(next) + pull * std::sin(next), cy + radius * std::sin(next) - pull * std::cos(next),
                       cx + radius * std::cos(next), cy + radius * std::sin(next)});
                angle = next;
            }
        };
        const auto paint = [&](std::string_view operation, const bool stroked) {
            if (state.path.empty()) return;
            for (const double channel : state.color) put(out, channel);
            out += stroked ? "RG\n" : "rg\n";
            if (stroked) {
                put(out, state.width * scaling());
                out += "w ";
                out += std::to_string(state.cap) + " J " + std::to_string(state.join) + " j [";
                for (const double dash : state.dashes) put(out, dash * scaling());
                out += "] ";
                put(out, state.phase * scaling());
                out += "d\n";
            }
            out += state.path;
            out += operation;
            out += '\n';
        };
        // The standard face a font's name is nearest.
        const auto standard = [](const std::string_view name) {
            const bool bold = name.contains("Bold") || name.contains("Black") || name.contains("Heavy") || name.contains("Semibold");
            const bool slanted = name.contains("Italic") || name.contains("Oblique") || name.contains("Slanted");
            if (name.contains("Symbol")) return std::string("Symbol");
            if (name.contains("Dingbats")) return std::string("ZapfDingbats");
            if (name.contains("Courier") || name.contains("Mono") || name.contains("Typewriter") || name.starts_with("CMTT") ||
                name.starts_with("cmtt")) {
                return std::string(bold && slanted ? "Courier-BoldOblique" : bold ? "Courier-Bold" : slanted ? "Courier-Oblique" : "Courier");
            }
            if (name.contains("Times") || name.contains("Roman") || (name.contains("Serif") && !name.contains("Sans")) ||
                name.contains("Palatino") || name.contains("Bookman") || name.contains("Century") || name.contains("Garamond") ||
                name.starts_with("CMR") || name.starts_with("cmr")) {
                return std::string(bold && slanted ? "Times-BoldItalic" : bold ? "Times-Bold" : slanted ? "Times-Italic" : "Times-Roman");
            }
            return std::string(bold && slanted ? "Helvetica-BoldOblique" : bold ? "Helvetica-Bold" : slanted ? "Helvetica-Oblique" : "Helvetica");
        };
        // Letters set at the current point in the current font, which moves
        // on past them.
        const auto show = [&](const std::string& letters) {
            if (!state.placed || letters.empty()) return;
            auto found = std::ranges::find(fonts, state.face);
            if (found == fonts.end()) {
                fonts.push_back(state.face);
                found = fonts.end() - 1;
            }
            for (const double channel : state.color) put(out, channel);
            out += "rg\nBT /F" + std::to_string(found - fonts.begin()) + " 1 Tf ";
            const std::array<double, 6>& m = state.matrix;
            for (const double value : {m[0] * state.size, m[1] * state.size, m[2] * state.size, m[3] * state.size, state.x, state.y}) {
                put(out, value);
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
            out += ") Tj ET\n";
            const auto [dx, dy] = distance(breadth(state.face, letters) / 1000.0 * state.size, 0.0);
            state.x += dx;
            state.y += dy;
        };

        // --- The program ---------------------------------------------------------------------

        std::vector<Object> stack;
        std::vector<std::shared_ptr<Table>> dictionaries{std::make_shared<Table>()};
        std::size_t cursor = 0;      // where the program is read from the file, for `currentfile`
        std::size_t resumed = 0;     // where the last object the program read ended
        std::size_t steps = 0;       // how many objects have run
        std::size_t depth = 0;       // how many procedures deep it runs
        bool leaving = false;        // an `exit` on its way out of the loop it is in
        bool stopping = false;       // a `stop` on its way out to the `stopped` around it
        bool halted = false;         // the program quit, or ran too long

        const auto pop = [&stack]() -> Object {
            if (stack.empty()) return {};
            Object top = std::move(stack.back());
            stack.pop_back();
            return top;
        };
        const auto number = [&pop]() {
            const Object top = pop();
            return top.kind == Kind::Number || top.kind == Kind::Boolean ? top.number : 0.0;
        };
        const auto push = [&stack](const double value) { stack.push_back(Object{.kind = Kind::Number, .number = value}); };
        const auto truth = [&stack](const bool value) {
            stack.push_back(Object{.kind = Kind::Boolean, .number = value ? 1.0 : 0.0});
        };
        const auto lookup = [&dictionaries](const std::string& name) -> const Object* {
            for (auto dictionary = dictionaries.rbegin(); dictionary != dictionaries.rend(); ++dictionary) {
                if (const auto found = (*dictionary)->find(name); found != (*dictionary)->end()) return &found->second;
            }
            return nullptr;
        };
        // A matrix from an array of six numbers.
        const auto matrix = [](const Object& array) {
            std::array<double, 6> m{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
            if (array.items && array.items->size() == 6) {
                for (std::size_t index = 0; index < 6; ++index) m[index] = (*array.items)[index].number;
            }
            return m;
        };
        const auto numbers = [](const std::array<double, 6>& m) {
            auto items = std::make_shared<std::vector<Object>>();
            for (const double value : m) items->push_back(Object{.kind = Kind::Number, .number = value});
            return Object{.kind = Kind::Array, .items = std::move(items)};
        };
        // Bytes read from the file where the program stands: in hexadecimal,
        // as they are, or to a line's end.
        const auto take = [&](const std::size_t length, const bool hexadecimal) {
            std::string bytes;
            if (hexadecimal) {
                int pending = -1;
                while (cursor < file.size() && bytes.size() < length) {
                    const char letter = file[cursor++];
                    const int value = letter >= '0' && letter <= '9' ? letter - '0'
                                    : letter >= 'a' && letter <= 'f' ? letter - 'a' + 10
                                    : letter >= 'A' && letter <= 'F' ? letter - 'A' + 10 : -1;
                    if (value < 0) continue;
                    if (pending < 0) {
                        pending = value;
                    } else {
                        bytes += static_cast<char>(pending * 16 + value);
                        pending = -1;
                    }
                }
            } else {
                // The one blank that ended the operator before them.
                if (cursor == resumed && cursor < file.size() &&
                    (file[cursor] == ' ' || file[cursor] == '\n' || file[cursor] == '\r')) {
                    ++cursor;
                }
                bytes = std::string(file.substr(cursor, length));
                cursor += bytes.size();
            }
            return bytes;
        };

        const auto run = [&](this const auto& self, const Object& object, const bool invoked) -> void {
            if (halted || leaving || stopping) return;
            if (++steps > 4000000) {
                halted = true;
                return;
            }
            if (invoked && object.kind == Kind::Procedure) {
                ++depth;
                for (const Object& item : *object.items) {
                    self(item, false);
                    if (halted || leaving || stopping) break;
                }
                --depth;
                return;
            }
            if (object.kind != Kind::Name) {
                stack.push_back(object);
                return;
            }

            // A name: what a dictionary holds for it -- a procedure is run, a
            // name standing for an operator is that operator -- or else an
            // operator of its own. Deep enough that a procedure must be
            // calling itself by the name of an operator, `/moveto {moveto}
            // def`, the operator.
            const std::string& name = object.text;
            if (const Object* defined = depth < 200 ? lookup(name) : nullptr) {
                const Object held = *defined;
                if (held.kind == Kind::Procedure) {
                    self(held, true);
                } else if (held.kind == Kind::Name && held.text != name) {
                    self(held, false);
                } else {
                    stack.push_back(held);
                }
                return;
            }

            // The stack.
            if (name == "pop") {
                pop();
            } else if (name == "exch") {
                if (stack.size() >= 2) std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
            } else if (name == "dup") {
                if (!stack.empty()) stack.push_back(stack.back());
            } else if (name == "copy") {
                if (!stack.empty() && stack.back().kind == Kind::Number) {
                    const auto count = static_cast<std::size_t>(std::max(0.0, number()));
                    if (count <= stack.size()) {
                        const std::size_t begin = stack.size() - count;
                        for (std::size_t index = 0; index < count; ++index) stack.push_back(stack[begin + index]);
                    }
                } else if (stack.size() >= 2) {
                    // One array or string into another: the second, filled.
                    Object target = pop();
                    const Object source = pop();
                    if (source.items && target.items) {
                        for (std::size_t index = 0; index < std::min(source.items->size(), target.items->size()); ++index) {
                            (*target.items)[index] = (*source.items)[index];
                        }
                    } else if (source.table && target.table) {
                        for (const auto& [key, value] : *source.table) (*target.table)[key] = value;
                    } else if (target.kind == Kind::Text) {
                        target.text = source.text;
                    }
                    stack.push_back(target);
                }
            } else if (name == "index") {
                const auto below = static_cast<std::size_t>(std::max(0.0, number()));
                stack.push_back(below < stack.size() ? stack[stack.size() - 1 - below] : Object{});
            } else if (name == "roll") {
                const auto shift = static_cast<long long>(number());
                const auto count = static_cast<long long>(number());
                if (count > 0 && static_cast<std::size_t>(count) <= stack.size()) {
                    const auto begin = stack.end() - count;
                    const long long amount = ((shift % count) + count) % count;
                    std::rotate(begin, stack.end() - amount, stack.end());
                }
            } else if (name == "clear") {
                stack.clear();
            } else if (name == "count") {
                push(static_cast<double>(stack.size()));
            } else if (name == "mark" || name == "[" || name == "<<") {
                stack.push_back(Object{.kind = Kind::Mark});
            } else if (name == "cleartomark") {
                while (!stack.empty() && stack.back().kind != Kind::Mark) stack.pop_back();
                pop();
            } else if (name == "counttomark") {
                std::size_t count = 0;
                while (count < stack.size() && stack[stack.size() - 1 - count].kind != Kind::Mark) ++count;
                push(static_cast<double>(count));
            } else if (name == "]") {
                auto items = std::make_shared<std::vector<Object>>();
                while (!stack.empty() && stack.back().kind != Kind::Mark) items->insert(items->begin(), pop());
                pop();
                stack.push_back(Object{.kind = Kind::Array, .items = std::move(items)});
            } else if (name == ">>") {
                auto table = std::make_shared<Table>();
                std::vector<Object> pairs;
                while (!stack.empty() && stack.back().kind != Kind::Mark) pairs.insert(pairs.begin(), pop());
                pop();
                for (std::size_t index = 0; index + 1 < pairs.size(); index += 2) (*table)[pairs[index].text] = pairs[index + 1];
                stack.push_back(Object{.kind = Kind::Table, .table = std::move(table)});

            // Arithmetic.
            } else if (name == "add" || name == "sub" || name == "mul" || name == "div" || name == "idiv" || name == "mod" ||
                       name == "atan" || name == "exp" || name == "max" || name == "min") {
                const double right = number();
                const double left = number();
                double result = 0.0;
                if (name == "add") result = left + right;
                if (name == "sub") result = left - right;
                if (name == "mul") result = left * right;
                if (name == "div") result = right != 0.0 ? left / right : 0.0;
                if (name == "idiv") result = right != 0.0 ? std::trunc(left / right) : 0.0;
                if (name == "mod") result = right != 0.0 ? std::fmod(std::trunc(left), std::trunc(right)) : 0.0;
                if (name == "atan") {
                    result = std::atan2(left, right) * 180.0 / std::numbers::pi;
                    if (result < 0.0) result += 360.0;
                }
                if (name == "exp") result = std::pow(left, right);
                if (name == "max") result = std::max(left, right);
                if (name == "min") result = std::min(left, right);
                push(result);
            } else if (name == "neg" || name == "abs" || name == "sqrt" || name == "sin" || name == "cos" || name == "ln" ||
                       name == "log" || name == "round" || name == "truncate" || name == "floor" || name == "ceiling" ||
                       name == "cvi" || name == "cvr") {
                const double value = number();
                const double turn = std::numbers::pi / 180.0;
                push(name == "neg" ? -value : name == "abs" ? std::abs(value) : name == "sqrt" ? std::sqrt(std::max(value, 0.0))
                     : name == "sin" ? std::sin(value * turn) : name == "cos" ? std::cos(value * turn)
                     : name == "ln" ? std::log(value) : name == "log" ? std::log10(value)
                     : name == "round" ? std::floor(value + 0.5) : name == "truncate" || name == "cvi" ? std::trunc(value)
                     : name == "floor" ? std::floor(value) : name == "ceiling" ? std::ceil(value) : value);
            } else if (name == "rand" || name == "realtime" || name == "usertime") {
                push(0.0);
            } else if (name == "srand") {
                pop();

            // Comparison and logic.
            } else if (name == "eq" || name == "ne") {
                const Object right = pop();
                const Object left = pop();
                const bool same = left.kind == Kind::Number || left.kind == Kind::Boolean
                                      ? (right.kind == Kind::Number || right.kind == Kind::Boolean) && left.number == right.number
                                      : left.text == right.text && left.kind != Kind::Null && right.kind != Kind::Null;
                truth(name == "eq" ? same : !same);
            } else if (name == "gt" || name == "ge" || name == "lt" || name == "le") {
                const double right = number();
                const double left = number();
                truth(name == "gt" ? left > right : name == "ge" ? left >= right : name == "lt" ? left < right : left <= right);
            } else if (name == "and" || name == "or" || name == "xor") {
                const Object right = pop();
                const Object left = pop();
                const auto a = static_cast<long long>(left.number);
                const auto b = static_cast<long long>(right.number);
                const long long result = name == "and" ? (a & b) : name == "or" ? (a | b) : (a ^ b);
                if (left.kind == Kind::Boolean) truth(result != 0);
                else push(static_cast<double>(result));
            } else if (name == "not") {
                const Object value = pop();
                if (value.kind == Kind::Boolean) truth(value.number == 0.0);
                else push(static_cast<double>(~static_cast<long long>(value.number)));
            } else if (name == "true" || name == "false") {
                truth(name == "true");
            } else if (name == "null") {
                stack.push_back(Object{});

            // Control.
            } else if (name == "if") {
                const Object body = pop();
                if (pop().number != 0.0) self(body, true);
            } else if (name == "ifelse") {
                const Object otherwise = pop();
                const Object body = pop();
                self(pop().number != 0.0 ? body : otherwise, true);
            } else if (name == "for") {
                const Object body = pop();
                const double limit = number();
                const double increment = number();
                double value = number();
                const auto within = [&] { return increment > 0.0 ? value <= limit + 1e-9 : value >= limit - 1e-9; };
                for (; increment != 0.0 && within() && !halted && !stopping; value += increment) {
                    push(value);
                    self(body, true);
                    if (leaving) {
                        leaving = false;
                        break;
                    }
                }
            } else if (name == "repeat") {
                const Object body = pop();
                const auto times = static_cast<long long>(number());
                for (long long time = 0; time < times && !halted && !stopping; ++time) {
                    self(body, true);
                    if (leaving) {
                        leaving = false;
                        break;
                    }
                }
            } else if (name == "loop") {
                const Object body = pop();
                while (!halted && !stopping) {
                    self(body, true);
                    if (leaving) {
                        leaving = false;
                        break;
                    }
                }
            } else if (name == "forall") {
                const Object body = pop();
                const Object over = pop();
                if (over.items) {
                    for (const Object& item : *over.items) {
                        stack.push_back(item);
                        self(body, true);
                        if (leaving || halted || stopping) break;
                    }
                } else if (over.table) {
                    for (const auto& [key, value] : *over.table) {
                        stack.push_back(Object{.kind = Kind::Literal, .text = key});
                        stack.push_back(value);
                        self(body, true);
                        if (leaving || halted || stopping) break;
                    }
                } else if (over.kind == Kind::Text) {
                    for (const char letter : over.text) {
                        push(static_cast<unsigned char>(letter));
                        self(body, true);
                        if (leaving || halted || stopping) break;
                    }
                }
                leaving = false;
            } else if (name == "exit") {
                leaving = true;
            } else if (name == "exec") {
                const Object body = pop();
                if (body.kind == Kind::Procedure) self(body, true);
                else if (body.kind == Kind::Name || body.kind == Kind::Literal) self(Object{.kind = Kind::Name, .text = body.text}, false);
                else stack.push_back(body);
            } else if (name == "stopped") {
                const Object body = pop();
                self(body, true);
                truth(stopping);
                stopping = false;
            } else if (name == "stop") {
                stopping = true;
            } else if (name == "quit") {
                halted = true;

            // Dictionaries, arrays and strings.
            } else if (name == "dict") {
                pop();
                stack.push_back(Object{.kind = Kind::Table, .table = std::make_shared<Table>()});
            } else if (name == "begin") {
                const Object dictionary = pop();
                dictionaries.push_back(dictionary.table ? dictionary.table : std::make_shared<Table>());
            } else if (name == "end") {
                if (dictionaries.size() > 1) dictionaries.pop_back();
            } else if (name == "def" || name == "store") {
                const Object value = pop();
                const Object key = pop();
                std::shared_ptr<Table> into = dictionaries.back();
                if (name == "store") {
                    for (auto dictionary = dictionaries.rbegin(); dictionary != dictionaries.rend(); ++dictionary) {
                        if ((*dictionary)->contains(key.text)) {
                            into = *dictionary;
                            break;
                        }
                    }
                }
                (*into)[key.text] = value;
            } else if (name == "load") {
                const Object key = pop();
                const Object* found = lookup(key.text);
                stack.push_back(found ? *found : Object{.kind = Kind::Name, .text = key.text});
            } else if (name == "where") {
                const Object key = pop();
                bool known = false;
                for (auto dictionary = dictionaries.rbegin(); dictionary != dictionaries.rend() && !known; ++dictionary) {
                    if ((*dictionary)->contains(key.text)) {
                        stack.push_back(Object{.kind = Kind::Table, .table = *dictionary});
                        known = true;
                    }
                }
                truth(known);
            } else if (name == "known") {
                const Object key = pop();
                const Object dictionary = pop();
                truth(dictionary.table && dictionary.table->contains(key.text));
            } else if (name == "undef") {
                const Object key = pop();
                const Object dictionary = pop();
                if (dictionary.table) dictionary.table->erase(key.text);
            } else if (name == "currentdict" || name == "userdict" || name == "globaldict" || name == "statusdict" ||
                       name == "systemdict" || name == "errordict" || name == "$error") {
                stack.push_back(Object{.kind = Kind::Table, .table = name == "currentdict" ? dictionaries.back() : dictionaries.front()});
            } else if (name == "countdictstack") {
                push(static_cast<double>(dictionaries.size()));
            } else if (name == "get") {
                const Object key = pop();
                const Object from = pop();
                if (from.table) {
                    const auto found = from.table->find(key.text);
                    stack.push_back(found != from.table->end() ? found->second : Object{});
                } else if (from.items) {
                    const auto index = static_cast<std::size_t>(std::max(0.0, key.number));
                    stack.push_back(index < from.items->size() ? (*from.items)[index] : Object{});
                } else {
                    const auto index = static_cast<std::size_t>(std::max(0.0, key.number));
                    push(index < from.text.size() ? static_cast<unsigned char>(from.text[index]) : 0.0);
                }
            } else if (name == "put") {
                const Object value = pop();
                const Object key = pop();
                const Object into = pop();
                if (into.table) (*into.table)[key.text] = value;
                if (into.items) {
                    const auto index = static_cast<std::size_t>(std::max(0.0, key.number));
                    if (index < into.items->size()) (*into.items)[index] = value;
                }
            } else if (name == "length") {
                const Object of = pop();
                push(static_cast<double>(of.items ? of.items->size() : of.table ? of.table->size() : of.text.size()));
            } else if (name == "maxlength") {
                pop();
                push(1000.0);
            } else if (name == "array") {
                const auto size = static_cast<std::size_t>(std::clamp(number(), 0.0, 100000.0));
                stack.push_back(Object{.kind = Kind::Array, .items = std::make_shared<std::vector<Object>>(size)});
            } else if (name == "string") {
                const auto size = static_cast<std::size_t>(std::clamp(number(), 0.0, 1000000.0));
                stack.push_back(Object{.kind = Kind::Text, .text = std::string(size, '\0')});
            } else if (name == "aload") {
                const Object array = pop();
                if (array.items) {
                    for (const Object& item : *array.items) stack.push_back(item);
                }
                stack.push_back(array);
            } else if (name == "astore") {
                Object array = pop();
                if (array.items) {
                    for (auto item = array.items->rbegin(); item != array.items->rend(); ++item) *item = pop();
                }
                stack.push_back(array);
            } else if (name == "getinterval") {
                const auto count = static_cast<std::size_t>(std::max(0.0, number()));
                const auto start = static_cast<std::size_t>(std::max(0.0, number()));
                Object of = pop();
                if (of.items) {
                    auto items = std::make_shared<std::vector<Object>>();
                    for (std::size_t index = start; index < std::min(start + count, of.items->size()); ++index) {
                        items->push_back((*of.items)[index]);
                    }
                    of.items = std::move(items);
                } else {
                    of.text = start < of.text.size() ? of.text.substr(start, count) : std::string{};
                }
                stack.push_back(of);
            } else if (name == "cvx") {
                Object value = pop();
                if (value.kind == Kind::Array) value.kind = Kind::Procedure;
                if (value.kind == Kind::Literal) value.kind = Kind::Name;
                stack.push_back(value);
            } else if (name == "cvlit") {
                Object value = pop();
                if (value.kind == Kind::Procedure) value.kind = Kind::Array;
                if (value.kind == Kind::Name) value.kind = Kind::Literal;
                stack.push_back(value);
            } else if (name == "xcheck") {
                const Object value = pop();
                truth(value.kind == Kind::Procedure || value.kind == Kind::Name);
            } else if (name == "cvn") {
                const Object value = pop();
                stack.push_back(Object{.kind = Kind::Literal, .text = value.text});
            } else if (name == "cvs" || name == "cvrs") {
                if (name == "cvrs") pop();
                pop();
                const Object value = pop();
                std::string written = value.text;
                if (value.kind == Kind::Number) {
                    written = std::format("{}", value.number);
                } else if (value.kind == Kind::Boolean) {
                    written = value.number != 0.0 ? "true" : "false";
                }
                stack.push_back(Object{.kind = Kind::Text, .text = std::move(written)});
            } else if (name == "type") {
                const Object value = pop();
                static constexpr std::array<std::string_view, 12> types{
                    "nulltype", "realtype", "booleantype", "nametype", "nametype", "stringtype",
                    "arraytype", "arraytype", "marktype", "dicttype", "fonttype", "filetype",
                };
                stack.push_back(Object{.kind = Kind::Name, .text = std::string(types[static_cast<std::size_t>(value.kind)])});
            } else if (name == "bind" || name == "readonly" || name == "executeonly" || name == "noaccess") {
                // Each leaves what it is given as it is.

            // The graphics state.
            } else if (name == "gsave" || name == "save") {
                saved.push_back(state);
                out += "q\n";
                if (name == "save") stack.push_back(Object{.kind = Kind::Table, .table = std::make_shared<Table>()});
            } else if (name == "grestore" || name == "restore") {
                if (name == "restore") pop();
                if (!saved.empty()) {
                    state = std::move(saved.back());
                    saved.pop_back();
                    out += "Q\n";
                }
            } else if (name == "grestoreall" || name == "initgraphics") {
                while (!saved.empty()) {
                    state = std::move(saved.back());
                    saved.pop_back();
                    out += "Q\n";
                }
            } else if (name == "setlinewidth") {
                state.width = number();
            } else if (name == "currentlinewidth") {
                push(state.width);
            } else if (name == "setlinecap") {
                state.cap = std::clamp(static_cast<int>(number()), 0, 2);
            } else if (name == "setlinejoin") {
                state.join = std::clamp(static_cast<int>(number()), 0, 2);
            } else if (name == "setdash") {
                state.phase = number();
                const Object pattern = pop();
                state.dashes.clear();
                if (pattern.items) {
                    for (const Object& item : *pattern.items) state.dashes.push_back(item.number);
                }
            } else if (name == "setmiterlimit" || name == "setflat" || name == "setstrokeadjust" || name == "setoverprint" ||
                       name == "setsmoothness" || name == "setpagedevice" || name == "setglobal" || name == "setobjectformat" ||
                       name == "setpacking") {
                pop();
            } else if (name == "currentglobal" || name == "currentpacking") {
                truth(false);
            } else if (name == "languagelevel") {
                push(2.0);
            } else if (name == "setrgbcolor") {
                const double blue = number();
                const double green = number();
                state.color = {number(), green, blue};
            } else if (name == "setgray") {
                const double gray = number();
                state.color = {gray, gray, gray};
            } else if (name == "setcmykcolor") {
                const double black = number();
                const double yellow = number();
                const double magenta = number();
                const double cyan = number();
                state.color = {(1.0 - cyan) * (1.0 - black), (1.0 - magenta) * (1.0 - black), (1.0 - yellow) * (1.0 - black)};
            } else if (name == "sethsbcolor") {
                const double value = number();
                const double saturation = number();
                const double hue = number() * 6.0;
                const double sector = std::floor(hue);
                const double part = hue - sector;
                const double p = value * (1.0 - saturation);
                const double q = value * (1.0 - saturation * part);
                const double t = value * (1.0 - saturation * (1.0 - part));
                const std::array<std::array<double, 3>, 6> colors{{{value, t, p}, {q, value, p}, {p, value, t},
                                                                    {p, q, value}, {t, p, value}, {value, p, q}}};
                state.color = colors[static_cast<std::size_t>(std::clamp(sector, 0.0, 5.0))];
            } else if (name == "setcolorspace") {
                const Object space = pop();
                const std::string named = space.items && !space.items->empty() ? space.items->front().text : space.text;
                state.components = named == "DeviceRGB" || named == "CalRGB" ? 3 : named == "DeviceCMYK" ? 4 : 1;
                state.color = {0.0, 0.0, 0.0};
            } else if (name == "setcolor") {
                std::array<double, 4> values{};
                for (std::size_t index = state.components; index-- > 0;) values[index] = number();
                if (state.components == 3) state.color = {values[0], values[1], values[2]};
                else if (state.components == 4) {
                    state.color = {(1.0 - values[0]) * (1.0 - values[3]), (1.0 - values[1]) * (1.0 - values[3]),
                                   (1.0 - values[2]) * (1.0 - values[3])};
                } else {
                    state.color = {values[0], values[0], values[0]};
                }
            } else if (name == "currentrgbcolor") {
                for (const double channel : state.color) push(channel);
            } else if (name == "currentgray") {
                push((state.color[0] + state.color[1] + state.color[2]) / 3.0);

            // The transformation.
            } else if (name == "translate" || name == "scale" || name == "rotate" || name == "concat") {
                // With a matrix given last, that matrix is changed instead.
                std::optional<Object> given;
                if (!stack.empty() && stack.back().kind == Kind::Array && name != "concat") given = pop();
                std::array<double, 6> step{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                if (name == "translate") {
                    const double y = number();
                    step[4] = number();
                    step[5] = y;
                } else if (name == "scale") {
                    const double y = number();
                    step[0] = number();
                    step[3] = y;
                } else if (name == "rotate") {
                    const double angle = number() * std::numbers::pi / 180.0;
                    step = {std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), 0.0, 0.0};
                } else {
                    step = matrix(pop());
                }
                if (given) {
                    stack.push_back(numbers(compose(step, matrix(*given))));
                } else {
                    state.matrix = compose(step, state.matrix);
                }
            } else if (name == "matrix" || name == "identmatrix") {
                if (name == "identmatrix") pop();
                stack.push_back(numbers({1.0, 0.0, 0.0, 1.0, 0.0, 0.0}));
            } else if (name == "currentmatrix") {
                pop();
                stack.push_back(numbers(state.matrix));
            } else if (name == "defaultmatrix") {
                pop();
                stack.push_back(numbers({1.0, 0.0, 0.0, 1.0, 0.0, 0.0}));
            } else if (name == "setmatrix") {
                state.matrix = matrix(pop());
            } else if (name == "invertmatrix") {
                pop();
                stack.push_back(numbers(invert(matrix(pop()))));
            } else if (name == "concatmatrix") {
                pop();
                const std::array<double, 6> second = matrix(pop());
                stack.push_back(numbers(compose(matrix(pop()), second)));
            } else if (name == "transform" || name == "dtransform" || name == "itransform" || name == "idtransform") {
                std::array<double, 6> m = state.matrix;
                if (!stack.empty() && stack.back().kind == Kind::Array) m = matrix(pop());
                if (name.starts_with('i')) m = invert(m);
                const double y = number();
                const double x = number();
                const bool shifted = name == "transform" || name == "itransform";
                push(m[0] * x + m[2] * y + (shifted ? m[4] : 0.0));
                push(m[1] * x + m[3] * y + (shifted ? m[5] : 0.0));

            // The path.
            } else if (name == "newpath") {
                state.path.clear();
                state.placed = false;
            } else if (name == "moveto" || name == "rmoveto" || name == "lineto" || name == "rlineto") {
                double y = number();
                double x = number();
                if (name.starts_with('r')) {
                    const auto [ux, uy] = current();
                    x += ux;
                    y += uy;
                }
                if (name.ends_with("moveto")) move(x, y);
                else line(x, y);
            } else if (name == "curveto" || name == "rcurveto") {
                std::array<double, 6> points{};
                for (std::size_t index = 6; index-- > 0;) points[index] = number();
                if (name == "rcurveto") {
                    const auto [ux, uy] = current();
                    for (std::size_t index = 0; index < 6; index += 2) {
                        points[index] += ux;
                        points[index + 1] += uy;
                    }
                }
                if (!state.placed) move(points[0], points[1]);
                curve(points);
            } else if (name == "closepath") {
                if (state.placed) {
                    state.path += "h\n";
                    state.x = state.startx;
                    state.y = state.starty;
                }
            } else if (name == "arc" || name == "arcn") {
                const double to = number();
                const double from = number();
                const double radius = number();
                const double cy = number();
                const double cx = number();
                arc(cx, cy, radius, from, to, name == "arcn");
            } else if (name == "arct" || name == "arcto") {
                // A corner rounded off, drawn as the corner itself: its
                // radius and the point beyond it let go.
                for (int operand = 0; operand < 3; ++operand) pop();
                const double y = number();
                const double x = number();
                line(x, y);
                if (name == "arcto") {
                    for (const double value : {x, y, x, y}) push(value);
                }
            } else if (name == "currentpoint") {
                const auto [ux, uy] = current();
                push(ux);
                push(uy);
            } else if (name == "fill" || name == "eofill" || name == "stroke") {
                paint(name == "fill" ? "f" : name == "eofill" ? "f*" : "S", name == "stroke");
                state.path.clear();
                state.placed = false;
            } else if (name == "clip" || name == "eoclip") {
                if (!state.path.empty()) out += state.path + (name == "clip" ? "W n\n" : "W* n\n");
            } else if (name == "clippath") {
                state.path.clear();
                state.placed = false;
                const std::array<double, 6> kept = state.matrix;
                state.matrix = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                move(box[0], box[1]);
                line(box[2], box[1]);
                line(box[2], box[3]);
                line(box[0], box[3]);
                state.path += "h\n";
                state.matrix = kept;
            } else if (name == "rectfill" || name == "rectstroke" || name == "rectclip") {
                const double height = number();
                const double width = number();
                const double y = number();
                const double x = number();
                const std::string kept = std::exchange(state.path, std::string{});
                const bool placed = state.placed;
                move(x, y);
                line(x + width, y);
                line(x + width, y + height);
                line(x, y + height);
                state.path += "h\n";
                if (name == "rectclip") out += state.path + "W n\n";
                else paint(name == "rectfill" ? "f" : "S", name == "rectstroke");
                state.path = kept;
                state.placed = placed;
            } else if (name == "flattenpath" || name == "reversepath" || name == "strokepath" || name == "showpage" ||
                       name == "copypage" || name == "erasepage" || name == "initclip") {
                // Nothing to draw, or the page as it stands.

            // Text.
            } else if (name == "findfont") {
                const Object key = pop();
                stack.push_back(Object{.kind = Kind::Font, .number = 1.0, .text = standard(key.text)});
            } else if (name == "scalefont") {
                const double factor = number();
                Object font = pop();
                font.number *= factor;
                stack.push_back(font);
            } else if (name == "makefont") {
                const std::array<double, 6> m = matrix(pop());
                Object font = pop();
                font.number *= std::sqrt(std::abs(m[0] * m[3] - m[1] * m[2]));
                stack.push_back(font);
            } else if (name == "setfont") {
                const Object font = pop();
                state.face = font.kind == Kind::Font ? font.text : standard(font.text);
                state.size = font.kind == Kind::Font ? font.number : state.size;
            } else if (name == "selectfont") {
                const Object size = pop();
                const Object key = pop();
                state.face = standard(key.text);
                state.size = size.kind == Kind::Number ? size.number : 10.0;
            } else if (name == "currentfont") {
                stack.push_back(Object{.kind = Kind::Font, .number = state.size, .text = state.face});
            } else if (name == "definefont") {
                const Object font = pop();
                const Object key = pop();
                stack.push_back(Object{.kind = Kind::Font, .number = 1.0, .text = standard(key.text)});
                static_cast<void>(font);
            } else if (name == "show" || name == "ashow" || name == "widthshow" || name == "awidthshow" || name == "kshow" ||
                       name == "xshow" || name == "xyshow" || name == "glyphshow") {
                Object letters = pop();
                if (name == "kshow" || name == "xshow" || name == "xyshow") {
                    // The procedure or the widths before it, let go: the letters are the other.
                    if (letters.kind != Kind::Text) std::swap(letters, stack.empty() ? letters : stack.back());
                    pop();
                }
                const std::size_t operands = name == "ashow" ? 2 : name == "widthshow" ? 3 : name == "awidthshow" ? 5 : 0;
                for (std::size_t index = 0; index < operands; ++index) pop();
                if (letters.kind == Kind::Text) show(letters.text);
            } else if (name == "stringwidth") {
                const Object letters = pop();
                push(breadth(state.face, letters.text) / 1000.0 * state.size);
                push(0.0);
            } else if (name == "charpath") {
                pop();
                pop();

            // The file the program is read from: its bytes for an image,
            // and an encrypted font's skipped.
            } else if (name == "currentfile") {
                stack.push_back(Object{.kind = Kind::Source});
            } else if (name == "readhexstring" || name == "readstring") {
                const Object buffer = pop();
                pop();
                stack.push_back(Object{.kind = Kind::Text, .text = take(buffer.text.size(), name == "readhexstring")});
                truth(true);
            } else if (name == "readline") {
                pop();
                pop();
                const std::size_t end = file.find('\n', cursor);
                const std::size_t stop = end == std::string_view::npos ? file.size() : end;
                stack.push_back(Object{.kind = Kind::Text, .text = std::string(file.substr(cursor, stop - cursor))});
                cursor = std::min(stop + 1, file.size());
                truth(true);
            } else if (name == "eexec") {
                // A font's encrypted part, skipped to past the `cleartomark`
                // it ends with -- and the dictionary it would have closed,
                // closed.
                pop();
                const std::size_t end = file.find("cleartomark", cursor);
                cursor = end == std::string_view::npos ? file.size() : end + 11;
                if (dictionaries.size() > 1) dictionaries.pop_back();
            } else if (name == "closefile" || name == "flushfile" || name == "print" || name == "=" || name == "==") {
                pop();

            // Images, whose samples come from a procedure or a string: drawn
            // inline, in hexadecimal, through the transformation that puts
            // the image's square on the page.
            } else if (name == "image" || name == "colorimage" || name == "imagemask") {
                std::size_t channels = 1;
                bool multiple = false;
                if (name == "colorimage") {
                    channels = static_cast<std::size_t>(std::clamp(number(), 1.0, 4.0));
                    multiple = pop().number != 0.0;
                }
                std::vector<Object> sources;
                for (std::size_t index = 0; index < (multiple ? channels : 1); ++index) sources.insert(sources.begin(), pop());
                const std::array<double, 6> placing = matrix(pop());
                const double bits = name == "imagemask" ? 1.0 : number();
                const bool polarity = name == "imagemask" && pop().number != 0.0;
                const auto rows = static_cast<std::size_t>(std::clamp(number(), 0.0, 20000.0));
                const auto columns = static_cast<std::size_t>(std::clamp(number(), 0.0, 20000.0));
                const auto precision = static_cast<std::size_t>(bits);
                const std::size_t needed = (columns * precision * channels + 7) / 8 * rows;
                std::string samples;
                for (int guard = 0; samples.size() < needed && guard < 1000000 && !halted && !stopping; ++guard) {
                    const Object& source = sources.front();
                    if (source.kind == Kind::Text) {
                        samples += source.text;
                        if (source.text.empty()) break;
                        continue;
                    }
                    const std::size_t before = stack.size();
                    self(source, true);
                    if (stack.size() <= before) break;
                    const Object chunk = pop();
                    if (chunk.text.empty()) break;
                    samples += chunk.text;
                }
                samples.resize(needed, '\0');
                if (!multiple && rows > 0 && columns > 0 && (precision == 1 || precision == 2 || precision == 4 || precision == 8)) {
                    const std::array<double, 6> square{static_cast<double>(columns), 0.0, 0.0, -static_cast<double>(rows), 0.0,
                                                       static_cast<double>(rows)};
                    const std::array<double, 6> whole = compose(compose(square, invert(placing)), state.matrix);
                    if (name == "imagemask") {
                        for (const double channel : state.color) put(out, channel);
                        out += "rg\n";
                    }
                    out += "q ";
                    for (const double value : whole) put(out, value);
                    out += "cm\nBI /W " + std::to_string(columns) + " /H " + std::to_string(rows);
                    if (name == "imagemask") {
                        out += std::string(" /IM true /D [") + (polarity ? "1 0" : "0 1") + "]";
                    } else {
                        out += " /BPC " + std::to_string(precision) + (channels == 3 ? " /CS /RGB" : channels == 4 ? " /CS /CMYK" : " /CS /G");
                    }
                    out += " /F /AHx ID\n";
                    for (const char byte : samples) out += std::format("{:02X}", static_cast<unsigned char>(byte));
                    out += ">\nEI Q\n";
                }

            // Anything else: its operands left, as nothing draws with them.
            } else if (name == "version" || name == "product") {
                stack.push_back(Object{.kind = Kind::Text, .text = "3010"});
            }
        };

        while (!halted && !stopping) {
            std::optional<Object> object = read(file, cursor);
            if (!object) break;
            resumed = cursor;
            run(*object, false);
            leaving = false;
        }
        while (!saved.empty()) {
            saved.pop_back();
            out += "Q\n";
        }

        drawing.resources = faces(fonts);
        return drawing;
    }

}
