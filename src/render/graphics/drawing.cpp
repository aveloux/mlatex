/// @file
/// @brief Drawing implementation: a page of another PDF, read as a PDF
///        reader reads it.
///
/// Everything here is decode(): the file's values read as the format writes
/// them, its objects found through the cross-reference sections it ends with
/// -- tables, streams, and the object streams those point into -- the page
/// found in the page tree, and every object the page's resources reach
/// copied out, numbered in the order it was reached.
#include "render/graphics/drawing.hpp"

#include <zlib.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace render::graphics {

    std::optional<Drawing> Drawing::decode(const std::span<const std::byte> bytes, const int page) {
        using Type = Value::Type;
        std::string_view file(reinterpret_cast<const char*>(bytes.data()), bytes.size());

        // A picture in PostScript -- after the header a DOS EPS file with a
        // preview opens with, at the place and the length it names -- or in
        // SVG, whose element may follow a declaration and comments.
        if (file.starts_with("\xC5\xD0\xD3\xC6") && file.size() >= 12) {
            const auto field = [&file](const std::size_t at) {
                std::size_t value = 0;
                for (std::size_t index = 0; index < 4; ++index) {
                    value |= static_cast<std::size_t>(static_cast<unsigned char>(file[at + index])) << (8 * index);
                }
                return value;
            };
            const std::size_t start = field(4);
            const std::size_t length = field(8);
            if (start < file.size()) file = file.substr(start, std::min(length, file.size() - start));
        }
        if (file.starts_with("%!PS")) return page == 1 ? script(file) : std::nullopt;
        if (!file.starts_with("%PDF-") && file.substr(0, 8192).contains("<svg")) return page == 1 ? markup(file) : std::nullopt;

        if (!file.starts_with("%PDF-") || page < 1) return std::nullopt;
        constexpr std::size_t none = std::string_view::npos;

        // --- The file's words ---------------------------------------------------------------

        const auto blank = [](const char letter) {
            return letter == ' ' || letter == '\n' || letter == '\r' || letter == '\t' || letter == '\f' || letter == '\0';
        };
        const auto boundary = [&blank](const char letter) {
            return blank(letter) || std::string_view("()<>[]{}/%").contains(letter);
        };
        // Past blanks and comments.
        const auto skip = [&blank](const std::string_view in, std::size_t at) {
            while (at < in.size()) {
                if (blank(in[at])) {
                    ++at;
                } else if (in[at] == '%') {
                    while (at < in.size() && in[at] != '\n' && in[at] != '\r') ++at;
                } else {
                    break;
                }
            }
            return at;
        };
        // The letters from here to the next blank or delimiter.
        const auto word = [&boundary](const std::string_view in, const std::size_t at) {
            std::size_t end = at;
            while (end < in.size() && !boundary(in[end])) ++end;
            return in.substr(std::min(at, in.size()), end - std::min(at, in.size()));
        };
        const auto whole = [](const std::string_view text) {
            return !text.empty() && std::ranges::all_of(text, [](const char letter) { return letter >= '0' && letter <= '9'; });
        };
        const auto integer = [](const std::string_view text) {
            long long value = 0;
            std::from_chars(text.data(), text.data() + text.size(), value);
            return value;
        };

        // A value, read from `at`, which is left past it. `n g R` is one
        // value, a reference, whose number is the object's in the file.
        const auto read = [&](this const auto& self, const std::string_view in, std::size_t& at, const int depth) -> Value {
            Value made;
            at = skip(in, at);
            if (depth > 64) {
                at = in.size();
                return made;
            }
            if (at >= in.size()) return made;
            const char letter = in[at];

            if (letter == '<' && at + 1 < in.size() && in[at + 1] == '<') {
                at += 2;
                made.type = Type::Table;
                while (true) {
                    at = skip(in, at);
                    if (at >= in.size()) break;
                    if (in.substr(at, 2) == ">>") {
                        at += 2;
                        break;
                    }
                    Value key = self(in, at, depth + 1);
                    if (key.type != Type::Name) continue;
                    made.items.push_back(std::move(key));
                    made.items.push_back(self(in, at, depth + 1));
                }
                return made;
            }
            if (letter == '<') {
                const std::size_t end = in.find('>', at);
                const std::size_t stop = end == none ? in.size() : end + 1;
                made.type = Type::Text;
                made.text = std::string(in.substr(at, stop - at));
                at = stop;
                return made;
            }
            if (letter == '(') {
                std::size_t scan = at + 1;
                int nesting = 1;
                while (scan < in.size() && nesting > 0) {
                    if (in[scan] == '\\') {
                        scan += 2;
                        continue;
                    }
                    if (in[scan] == '(') ++nesting;
                    if (in[scan] == ')') --nesting;
                    ++scan;
                }
                scan = std::min(scan, in.size());
                made.type = Type::Text;
                made.text = std::string(in.substr(at, scan - at));
                at = scan;
                return made;
            }
            if (letter == '[') {
                ++at;
                made.type = Type::List;
                while (true) {
                    at = skip(in, at);
                    if (at >= in.size()) break;
                    if (in[at] == ']') {
                        ++at;
                        break;
                    }
                    made.items.push_back(self(in, at, depth + 1));
                }
                return made;
            }
            if (letter == '/') {
                const std::string_view name = word(in, at + 1);
                made.type = Type::Name;
                made.text = "/" + std::string(name);
                at += 1 + name.size();
                return made;
            }

            const std::string_view token = word(in, at);
            if (token.empty()) {
                // A character no value begins with: stepped over.
                ++at;
                return made;
            }
            at += token.size();
            if (whole(token)) {
                const std::size_t look = skip(in, at);
                const std::string_view generation = word(in, look);
                if (whole(generation)) {
                    const std::size_t after = skip(in, look + generation.size());
                    if (after < in.size() && in[after] == 'R' && (after + 1 >= in.size() || boundary(in[after + 1]))) {
                        made.type = Type::Reference;
                        made.number = static_cast<std::uint32_t>(integer(token));
                        at = after + 1;
                        return made;
                    }
                }
            }
            if (token == "null") return made;
            made.type = Type::Plain;
            made.text = std::string(token);
            return made;
        };

        // A dictionary's value under a key, or null.
        const auto get = [](const Value& table, const std::string_view key) -> const Value* {
            if (table.type != Type::Table) return nullptr;
            for (std::size_t index = 0; index + 1 < table.items.size(); index += 2) {
                if (table.items[index].text == key) return &table.items[index + 1];
            }
            return nullptr;
        };

        // --- Streams ----------------------------------------------------------------------

        // Flate's compressed bytes, unpacked.
        const auto unpack = [](const std::string_view data) -> std::optional<std::string> {
            z_stream stream{};
            if (inflateInit(&stream) != Z_OK) return std::nullopt;
            std::string out(std::max<std::size_t>(data.size() * 4, 1024), '\0');
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
            stream.avail_in = static_cast<uInt>(data.size());
            std::size_t written = 0;
            int status = Z_OK;
            while (status == Z_OK) {
                if (written == out.size()) out.resize(out.size() * 2);
                stream.next_out = reinterpret_cast<Bytef*>(out.data() + written);
                stream.avail_out = static_cast<uInt>(out.size() - written);
                status = inflate(&stream, Z_NO_FLUSH);
                written = out.size() - stream.avail_out;
                if (status == Z_BUF_ERROR && stream.avail_in == 0) break;
            }
            inflateEnd(&stream);
            if (status == Z_DATA_ERROR || status == Z_MEM_ERROR || status == Z_NEED_DICT) return std::nullopt;
            out.resize(written);
            return out;
        };

        // A stream's bytes, its filter undone -- Flate, and PNG's predictors
        // after it, as cross-reference streams use them -- or std::nullopt
        // for a filter this does not undo.
        const auto expand = [&](const Value& dictionary, const std::string_view data) -> std::optional<std::string> {
            const Value* filter = get(dictionary, "/Filter");
            if (filter && filter->type == Type::List) filter = filter->items.size() == 1 ? &filter->items[0] : nullptr;
            if (!get(dictionary, "/Filter")) return std::string(data);
            if (!filter || (filter->text != "/FlateDecode" && filter->text != "/Fl")) return std::nullopt;
            std::optional<std::string> out = unpack(data);
            if (!out) return std::nullopt;

            const Value* parameters = get(dictionary, "/DecodeParms");
            if (parameters && parameters->type == Type::List) parameters = parameters->items.empty() ? nullptr : &parameters->items[0];
            const auto parameter = [&](const std::string_view key, const long long fallback) {
                const Value* found = parameters ? get(*parameters, key) : nullptr;
                return found && found->type == Type::Plain ? integer(found->text) : fallback;
            };
            if (parameter("/Predictor", 1) < 10) return out;

            const long long colors = std::max(parameter("/Colors", 1), 1LL);
            const long long bits = std::max(parameter("/BitsPerComponent", 8), 1LL);
            const std::size_t pixel = static_cast<std::size_t>(std::max(colors * bits / 8, 1LL));
            const std::size_t row = static_cast<std::size_t>((parameter("/Columns", 1) * colors * bits + 7) / 8);
            std::string plain;
            std::string previous(row, '\0');
            for (std::size_t at = 0; at + row + 1 <= out->size(); at += row + 1) {
                const auto kind = static_cast<unsigned char>((*out)[at]);
                std::string line = out->substr(at + 1, row);
                for (std::size_t index = 0; index < row; ++index) {
                    const int left = index >= pixel ? static_cast<unsigned char>(line[index - pixel]) : 0;
                    const int up = static_cast<unsigned char>(previous[index]);
                    const int corner = index >= pixel ? static_cast<unsigned char>(previous[index - pixel]) : 0;
                    int add = 0;
                    if (kind == 1) add = left;
                    if (kind == 2) add = up;
                    if (kind == 3) add = (left + up) / 2;
                    if (kind == 4) {
                        const int guess = left + up - corner;
                        const int apart[3] = {std::abs(guess - left), std::abs(guess - up), std::abs(guess - corner)};
                        add = apart[0] <= apart[1] && apart[0] <= apart[2] ? left : apart[1] <= apart[2] ? up : corner;
                    }
                    line[index] = static_cast<char>(static_cast<unsigned char>(line[index]) + add);
                }
                plain += line;
                previous = std::move(line);
            }
            return plain;
        };

        // --- Where each object is ------------------------------------------------------

        /// Where one object is: nowhere yet (0), at an offset in the file
        /// (1), in an object stream at a place (2), or freed (3).
        struct Entry {
            std::uint8_t kind{0};
            std::size_t first{0};
            std::size_t second{0};
        };
        std::vector<Entry> entries;
        // Sections are read newest first, so an object already placed stays.
        const auto enter = [&entries](const long long number, const Entry entry) {
            if (number < 0 || number > 8'000'000) return;
            const auto slot = static_cast<std::size_t>(number);
            if (slot >= entries.size()) entries.resize(slot + 1);
            if (entries[slot].kind == 0) entries[slot] = entry;
        };

        // The dictionary and the bytes of a stream object at an offset whose
        // length is written out, or an empty dictionary.
        const auto direct = [&](const std::size_t offset) -> std::pair<Value, std::string_view> {
            std::size_t at = skip(file, offset);
            for (int index = 0; index < 3; ++index) at = skip(file, at + word(file, at).size());
            Value dictionary = read(file, at, 0);
            at = skip(file, at);
            if (dictionary.type != Type::Table || file.substr(at, 6) != "stream") return {};
            at += 6;
            if (at < file.size() && file[at] == '\r') ++at;
            if (at < file.size() && file[at] == '\n') ++at;
            const Value* length = get(dictionary, "/Length");
            std::size_t size = length && length->type == Type::Plain ? static_cast<std::size_t>(integer(length->text)) : none;
            if (size == none || at + size > file.size()) {
                const std::size_t end = file.find("endstream", at);
                size = (end == none ? file.size() : end) - at;
            }
            return {std::move(dictionary), file.substr(at, size)};
        };

        // A cross-reference stream: its entries placed, its dictionary the
        // trailer it stands for.
        const auto crossing = [&](const std::size_t offset) -> Value {
            auto [dictionary, data] = direct(offset);
            if (dictionary.type != Type::Table) return {};
            const std::optional<std::string> table = expand(dictionary, data);
            const Value* widths = get(dictionary, "/W");
            if (!table || !widths || widths->type != Type::List || widths->items.size() < 3) return {};
            std::size_t width[3];
            for (std::size_t index = 0; index < 3; ++index) width[index] = static_cast<std::size_t>(integer(widths->items[index].text));
            const std::size_t row = width[0] + width[1] + width[2];
            if (row == 0) return {};

            std::vector<long long> ranges;
            if (const Value* index = get(dictionary, "/Index"); index && index->type == Type::List) {
                for (const Value& item : index->items) ranges.push_back(integer(item.text));
            } else if (const Value* size = get(dictionary, "/Size")) {
                ranges = {0, integer(size->text)};
            }
            std::size_t at = 0;
            for (std::size_t range = 0; range + 1 < ranges.size(); range += 2) {
                for (long long number = ranges[range]; number < ranges[range] + ranges[range + 1]; ++number) {
                    if (at + row > table->size()) return dictionary;
                    std::size_t field[3] = {0, 0, 0};
                    for (std::size_t part = 0; part < 3; ++part) {
                        for (std::size_t byte = 0; byte < width[part]; ++byte) {
                            field[part] = (field[part] << 8) | static_cast<unsigned char>((*table)[at++]);
                        }
                    }
                    const std::size_t kind = width[0] == 0 ? 1 : field[0];
                    if (kind == 1) enter(number, {1, field[1], 0});
                    if (kind == 2) enter(number, {2, field[1], field[2]});
                    if (kind == 0) enter(number, {3, 0, 0});
                }
            }
            return dictionary;
        };

        // The sections from the newest back, each trailer's /Prev the next.
        std::uint32_t catalog = 0;
        {
            const std::size_t mark = file.rfind("startxref");
            std::size_t start = none;
            if (mark != none) {
                const std::size_t at = skip(file, mark + 9);
                start = static_cast<std::size_t>(integer(word(file, at)));
            }
            std::vector<std::size_t> seen;
            while (start < file.size() && !std::ranges::contains(seen, start) && seen.size() < 256) {
                seen.push_back(start);
                std::size_t at = skip(file, start);
                Value trailer;
                if (file.substr(at, 4) == "xref") {
                    at += 4;
                    while (true) {
                        at = skip(file, at);
                        if (file.substr(at, 7) == "trailer") {
                            at += 7;
                            trailer = read(file, at, 0);
                            break;
                        }
                        const std::string_view first = word(file, at);
                        if (!whole(first)) break;
                        at = skip(file, at + first.size());
                        const std::string_view count = word(file, at);
                        if (!whole(count)) break;
                        at += count.size();
                        for (long long index = 0; index < integer(count); ++index) {
                            at = skip(file, at);
                            const std::string_view offset = word(file, at);
                            at = skip(file, at + offset.size());
                            at = skip(file, at + word(file, at).size());
                            const char kind = at < file.size() ? file[at] : 'f';
                            ++at;
                            enter(integer(first) + index,
                                  kind == 'n' ? Entry{1, static_cast<std::size_t>(integer(offset)), 0} : Entry{3, 0, 0});
                        }
                    }
                    if (const Value* hybrid = get(trailer, "/XRefStm"); hybrid && hybrid->type == Type::Plain) {
                        crossing(static_cast<std::size_t>(integer(hybrid->text)));
                    }
                } else {
                    trailer = crossing(start);
                }
                if (trailer.type != Type::Table) break;
                if (get(trailer, "/Encrypt")) return std::nullopt;
                if (catalog == 0) {
                    if (const Value* root = get(trailer, "/Root"); root && root->type == Type::Reference) catalog = root->number;
                }
                const Value* prior = get(trailer, "/Prev");
                start = prior && prior->type == Type::Plain ? static_cast<std::size_t>(integer(prior->text)) : none;
            }
        }

        // A file whose sections are missing or wrong is read the long way:
        // every `n g obj` in it, the last of each number standing, and its
        // root named in the last trailer it wrote.
        const auto rescan = [&] {
            entries.clear();
            for (std::size_t at = file.find("obj"); at != none; at = file.find("obj", at + 3)) {
                if (at + 3 < file.size() && !boundary(file[at + 3])) continue;
                std::size_t back = at;
                while (back > 0 && blank(file[back - 1])) --back;
                std::size_t digits = back;
                while (digits > 0 && file[digits - 1] >= '0' && file[digits - 1] <= '9') --digits;
                if (digits == back) continue;
                std::size_t gap = digits;
                while (gap > 0 && blank(file[gap - 1])) --gap;
                std::size_t start = gap;
                while (start > 0 && file[start - 1] >= '0' && file[start - 1] <= '9') --start;
                if (start == gap || gap == digits) continue;
                const long long number = integer(file.substr(start, gap - start));
                if (number >= 0 && number < 8'000'000) {
                    const auto slot = static_cast<std::size_t>(number);
                    if (slot >= entries.size()) entries.resize(slot + 1);
                    entries[slot] = Entry{1, start, 0};
                }
            }
            if (const std::size_t root = file.rfind("/Root"); root != none) {
                std::size_t at = root + 5;
                const Value named = read(file, at, 0);
                if (named.type == Type::Reference) catalog = named.number;
            }
        };
        if (catalog == 0 || entries.empty()) rescan();
        if (catalog == 0) return std::nullopt;

        // --- Objects ----------------------------------------------------------------------

        /// An object as the file holds it: its value, and a stream's bytes.
        struct Loaded {
            Value value{};
            std::string_view data{};
            bool streamed{false};
        };
        /// An object stream opened: its bytes unpacked, and where each object in it starts.
        struct Opened {
            std::string content{};
            std::vector<std::size_t> places{};
        };
        std::unordered_map<std::uint32_t, Loaded> loaded;
        std::unordered_map<std::uint32_t, Opened> opened;

        // An object by its number in the file, read the first time it is
        // asked for; null when the file has none by that number.
        const auto load = [&](this const auto& self, const std::uint32_t number, const int depth) -> const Loaded* {
            if (const auto found = loaded.find(number); found != loaded.end()) return &found->second;
            if (number >= entries.size() || depth > 32) return nullptr;
            const Entry entry = entries[number];
            Loaded made;
            if (entry.kind == 1) {
                std::size_t at = skip(file, entry.first);
                for (int index = 0; index < 2; ++index) at = skip(file, at + word(file, at).size());
                if (word(file, at) != "obj") return nullptr;
                at += 3;
                made.value = read(file, at, 0);
                at = skip(file, at);
                if (file.substr(at, 6) == "stream") {
                    at += 6;
                    if (at < file.size() && file[at] == '\r') ++at;
                    if (at < file.size() && file[at] == '\n') ++at;
                    std::size_t size = none;
                    if (const Value* length = get(made.value, "/Length")) {
                        const Value* known = length;
                        if (length->type == Type::Reference) {
                            const Loaded* held = self(length->number, depth + 1);
                            known = held ? &held->value : nullptr;
                        }
                        if (known && known->type == Type::Plain) size = static_cast<std::size_t>(integer(known->text));
                    }
                    const auto ends = [&](const std::size_t end) {
                        return file.substr(skip(file, end), 9) == "endstream";
                    };
                    if (size == none || at + size > file.size() || !ends(at + size)) {
                        const std::size_t end = file.find("endstream", at);
                        size = (end == none ? file.size() : end) - at;
                        while (size > 0 && (file[at + size - 1] == '\n' || file[at + size - 1] == '\r')) --size;
                    }
                    made.data = file.substr(at, size);
                    made.streamed = true;
                }
            } else if (entry.kind == 2) {
                const auto holder = static_cast<std::uint32_t>(entry.first);
                auto box = opened.find(holder);
                if (box == opened.end()) {
                    const Loaded* stream = self(holder, depth + 1);
                    if (!stream || !stream->streamed) return nullptr;
                    std::optional<std::string> content = expand(stream->value, stream->data);
                    if (!content) return nullptr;
                    Opened fresh;
                    fresh.content = std::move(*content);
                    const Value* count = get(stream->value, "/N");
                    const Value* first = get(stream->value, "/First");
                    if (!count || !first) return nullptr;
                    std::size_t at = 0;
                    for (long long index = 0; index < integer(count->text); ++index) {
                        at = skip(fresh.content, at);
                        at = skip(fresh.content, at + word(fresh.content, at).size());
                        const std::string_view offset = word(fresh.content, at);
                        at += offset.size();
                        fresh.places.push_back(static_cast<std::size_t>(integer(first->text) + integer(offset)));
                    }
                    box = opened.emplace(holder, std::move(fresh)).first;
                }
                if (entry.second >= box->second.places.size()) return nullptr;
                std::size_t at = box->second.places[entry.second];
                made.value = read(box->second.content, at, 0);
            } else {
                return nullptr;
            }
            return &loaded.emplace(number, std::move(made)).first->second;
        };

        // A value, followed through its references to what they name.
        const auto resolve = [&](const Value* value) -> const Value* {
            for (int guard = 0; value && value->type == Type::Reference && guard < 32; ++guard) {
                const Loaded* held = load(value->number, 0);
                value = held ? &held->value : nullptr;
            }
            return value;
        };

        // --- The page -----------------------------------------------------------------------

        /// What a page takes from the nodes above it in the tree.
        struct Inherited {
            const Value* resources{nullptr};
            const Value* media{nullptr};
            const Value* crop{nullptr};
        };
        int remaining = page;
        const auto find = [&](this const auto& self, const Value* node, Inherited inherited,
                              const int depth) -> std::optional<std::pair<const Value*, Inherited>> {
            node = resolve(node);
            if (!node || node->type != Type::Table || depth > 64) return std::nullopt;
            if (const Value* found = get(*node, "/Resources")) inherited.resources = found;
            if (const Value* found = get(*node, "/MediaBox")) inherited.media = found;
            if (const Value* found = get(*node, "/CropBox")) inherited.crop = found;
            if (const Value* kids = resolve(get(*node, "/Kids")); kids && kids->type == Type::List) {
                // A subtree before the page asked for is passed whole.
                if (const Value* count = get(*node, "/Count"); count && count->type == Type::Plain && depth > 0 &&
                                                               integer(count->text) < remaining) {
                    remaining -= static_cast<int>(integer(count->text));
                    return std::nullopt;
                }
                for (const Value& kid : kids->items) {
                    if (auto found = self(&kid, inherited, depth + 1)) return found;
                    if (remaining <= 0) break;
                }
                return std::nullopt;
            }
            if (--remaining == 0) return std::pair{node, inherited};
            return std::nullopt;
        };

        const Loaded* root = load(catalog, 0);
        if (!root) {
            rescan();
            loaded.clear();
            opened.clear();
            root = catalog != 0 ? load(catalog, 0) : nullptr;
        }
        if (!root) return std::nullopt;
        const auto found = find(get(root->value, "/Pages"), {}, 0);
        if (!found) return std::nullopt;
        const auto& [leaf, inherited] = *found;

        Drawing made;

        // Its box: the crop box a page is seen through, its media box when
        // it has none.
        const Value* box = resolve(inherited.crop ? inherited.crop : inherited.media);
        if (!box || box->type != Type::List || box->items.size() < 4) return std::nullopt;
        for (std::size_t index = 0; index < 4; ++index) {
            const Value* corner = resolve(&box->items[index]);
            if (!corner || corner->type != Type::Plain) return std::nullopt;
            std::from_chars(corner->text.data(), corner->text.data() + corner->text.size(), made.box[index]);
        }
        if (made.box[0] > made.box[2]) std::swap(made.box[0], made.box[2]);
        if (made.box[1] > made.box[3]) std::swap(made.box[1], made.box[3]);
        if (made.width() <= 0.0f || made.height() <= 0.0f) return std::nullopt;

        // What it draws: each content stream unpacked, one after another.
        const Value* contents = get(*leaf, "/Contents");
        std::vector<const Value*> parts;
        if (contents && contents->type == Type::Reference) {
            const Loaded* held = load(contents->number, 0);
            if (held && held->value.type == Type::List) {
                for (const Value& item : held->value.items) parts.push_back(&item);
            } else {
                parts.push_back(contents);
            }
        } else if (contents && contents->type == Type::List) {
            for (const Value& item : contents->items) parts.push_back(&item);
        }
        for (const Value* part : parts) {
            if (part->type != Type::Reference) continue;
            const Loaded* held = load(part->number, 0);
            if (!held || !held->streamed) continue;
            const std::optional<std::string> unpacked = expand(held->value, held->data);
            if (!unpacked) return std::nullopt;
            made.content += *unpacked;
            made.content += '\n';
        }

        // Every object its resources and its group reach, copied in the order
        // reached, each reference renumbered to its copy's place. A /Parent
        // is left behind, so no page tree comes with them.
        std::unordered_map<std::uint32_t, std::uint32_t> local;
        std::vector<std::uint32_t> pending;
        const auto copy = [&](this const auto& self, const Value& from) -> Value {
            Value to;
            to.type = from.type;
            to.text = from.text;
            if (from.type == Type::Reference) {
                const auto [where, fresh] = local.try_emplace(from.number, static_cast<std::uint32_t>(local.size()));
                if (fresh) pending.push_back(from.number);
                to.number = where->second;
                return to;
            }
            to.items.reserve(from.items.size());
            for (std::size_t index = 0; index < from.items.size(); ++index) {
                if (from.type == Type::Table && index % 2 == 0 && from.items[index].text == "/Parent") {
                    ++index;
                    continue;
                }
                to.items.push_back(self(from.items[index]));
            }
            return to;
        };
        if (inherited.resources) made.resources = copy(*inherited.resources);
        if (const Value* group = get(*leaf, "/Group")) made.group = copy(*group);
        for (std::size_t index = 0; index < pending.size(); ++index) {
            Object object;
            if (const Loaded* source = load(pending[index], 0)) {
                object.value = copy(source->value);
                if (source->streamed) {
                    object.streamed = true;
                    object.stream = std::string(source->data);
                    std::vector<Value>& items = object.value.items;
                    for (std::size_t at = 0; at + 1 < items.size(); at += 2) {
                        if (items[at].text != "/Length") continue;
                        items.erase(items.begin() + static_cast<std::ptrdiff_t>(at),
                                    items.begin() + static_cast<std::ptrdiff_t>(at + 2));
                        break;
                    }
                }
            }
            made.objects.push_back(std::move(object));
        }
        return made;
    }

    double Drawing::breadth(const std::string_view face, const std::string_view letters) noexcept {
        // The widths of the printable ASCII letters, from a space to a
        // tilde, in Adobe's metrics for Helvetica and Times-Roman; Courier's
        // are all 600. A bold or slanted cut is near enough its upright one.
        static constexpr std::array<short, 95> helvetica{
            278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
            556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778,
            722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278,
            278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
            556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584,
        };
        static constexpr std::array<short, 95> times{
            250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278, 500, 500, 500, 500,
            500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444, 921, 722, 667, 667, 722, 611, 556, 722,
            722, 333, 389, 722, 611, 889, 722, 722, 556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333,
            278, 333, 469, 500, 333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500,
            500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541,
        };
        const bool fixed = face.starts_with("Courier");
        const std::array<short, 95>& table = face.starts_with("Times") ? times : helvetica;
        double total = 0.0;
        for (const char letter : letters) {
            const auto code = static_cast<unsigned char>(letter);
            total += fixed ? 600.0 : code >= 32 && code < 127 ? table[code - 32] : 500.0;
        }
        return total;
    }

    Drawing::Value Drawing::faces(const std::vector<std::string>& names) {
        using Type = Value::Type;
        if (names.empty()) return {};
        Value fonts{.type = Type::Table};
        for (std::size_t index = 0; index < names.size(); ++index) {
            Value font{.type = Type::Table,
                       .items = {{.type = Type::Name, .text = "/Type"}, {.type = Type::Name, .text = "/Font"},
                                 {.type = Type::Name, .text = "/Subtype"}, {.type = Type::Name, .text = "/Type1"},
                                 {.type = Type::Name, .text = "/BaseFont"}, {.type = Type::Name, .text = "/" + names[index]}}};
            // The symbol faces keep their own encoding.
            if (names[index] != "Symbol" && names[index] != "ZapfDingbats") {
                font.items.push_back({.type = Type::Name, .text = "/Encoding"});
                font.items.push_back({.type = Type::Name, .text = "/WinAnsiEncoding"});
            }
            fonts.items.push_back({.type = Type::Name, .text = "/F" + std::to_string(index)});
            fonts.items.push_back(std::move(font));
        }
        return {.type = Type::Table, .items = {{.type = Type::Name, .text = "/Font"}, std::move(fonts)}};
    }

}
