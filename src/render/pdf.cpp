/// @file
/// @brief Pdf implementation: laid-out pages into a PDF file.
///
/// The file is built in memory and written once, because the cross-reference
/// table at the end of it is a list of byte offsets into everything before it
/// -- so nothing can be written until all of it exists.
///
/// Object numbers are handed out before the objects that fill them, because a
/// page has to name its contents and its fonts before either has been written.
#include "render/pdf.hpp"
#include "logger.hpp"

#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ot.h>
#include <harfbuzz/hb-subset.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace render {

    /// @brief Appends a whole number.
    /// @param text  Text to append to.
    /// @param value Number to write.
    /// @complexity O(1).
    static void number(std::string& text, const std::size_t value) {
        char buffer[24];
        if (const auto [stop, failure] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            failure == std::errc{}) {
            text.append(buffer, stop);
        }
    }

    /// @brief Appends a signed number.
    /// @param text  Text to append to.
    /// @param value Number to write.
    /// @complexity O(1).
    static void number(std::string& text, const int value) {
        char buffer[24];
        if (const auto [stop, failure] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            failure == std::errc{}) {
            text.append(buffer, stop);
        }
    }

    /// @brief Appends a number rounded to whole points.
    /// @param text  Text to append to.
    /// @param value Number to write.
    /// @complexity O(1).
    static void round(std::string& text, const float value) {
        number(text, static_cast<int>(value < 0.0f ? value - 0.5f : value + 0.5f));
    }

    /// @brief Appends a fractional number, such as an alpha channel.
    /// @param text  Text to append to.
    /// @param value Number to write; three decimals, trailing zeros dropped.
    /// @complexity O(1).
    static void fraction(std::string& text, const float value) {
        char buffer[32];
        const auto [stop, failure] =
            std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 3);
        if (failure != std::errc{}) {
            text += '0';
            return;
        }

        std::string_view digits(buffer, static_cast<std::size_t>(stop - buffer));
        if (digits.find('.') != std::string_view::npos) {
            while (digits.ends_with('0')) digits.remove_suffix(1);
            if (digits.ends_with('.')) digits.remove_suffix(1);
        }
        text += digits.empty() ? std::string_view("0") : digits;
    }

    /// @brief Appends a number in four hexadecimal digits.
    /// @param text  Text to append to.
    /// @param value Number to write; only its low sixteen bits are written.
    /// @complexity O(1).
    static void hexadecimal(std::string& text, const std::uint32_t value) {
        static constexpr char digits[] = "0123456789ABCDEF";
        text += digits[(value >> 12) & 0xF];
        text += digits[(value >> 8) & 0xF];
        text += digits[(value >> 4) & 0xF];
        text += digits[value & 0xF];
    }

    std::size_t Pdf::File::reserve() {
        offsets.push_back(0);
        return offsets.size();
    }

    void Pdf::File::open(const std::size_t number) {
        offsets[number - 1] = bytes.size();
        render::number(bytes, number);
        bytes += " 0 obj\n";
    }

    void Pdf::File::close() {
        bytes += "\nendobj\n";
    }

    void Pdf::File::stream(
        const std::size_t number,
        const std::string_view extra,
        const std::string_view payload
    ) {
        open(number);
        bytes += "<<";
        bytes += extra;
        bytes += "/Length ";
        render::number(bytes, payload.size());
        bytes += ">>\nstream\n";
        bytes += payload;
        bytes += "\nendstream";
        close();
    }


    std::string Pdf::render(Composer& composer, const memory::Slice<layout::Pager::Page> pages) {
        if (pages.empty()) return {};

        const layout::Document::Configuration& page = composer.document.configuration;
        if (page.width <= 0.0f || page.height <= 0.0f) return {};

        File file;
        file.bytes.reserve(256 * 1024);

        // The comment on the second line is four bytes above 127, which is how
        // a reader is told the file is binary and must not be line-ending
        // converted on its way anywhere.
        file.bytes += "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";

        // The root and the page tree are named by everything under them, so
        // their numbers are taken first and their contents written last.
        const std::size_t catalogue = file.reserve();
        const std::size_t tree = file.reserve();

        // Three rounds, in this order for one reason each. The page
        // descriptions come first, because drawing them is what discovers
        // which faces the document uses. The faces come second, because a page
        // has to name them. The pages come last, once there is something to
        // name.
        std::vector<std::size_t> bodies;
        bodies.reserve(pages.count);

        for (const layout::Pager::Page& sheet : pages) {
            const std::string_view content = composer.draw(sheet.nodes, sheet.notes, page.left, page.top);
            const std::size_t body = file.reserve();
            file.stream(body, "", content);
            bodies.push_back(body);
        }

        // Cut in parallel, written in order; see the class notes. A face goes
        // to whichever thread claims it first, and the writing thread claims
        // them too: while the face it has to write next is still being cut
        // elsewhere, it cuts a later one rather than wait for it.
        const std::vector<Composer::Entry>& faces = composer.faces;

        /// @brief One face's cut, and whether it has been made yet.
        struct Cut {
            hb_blob_t* program{nullptr};      ///< What cut() made of the face.
            std::atomic<bool> ready{false};   ///< Set once the program is.
        };

        std::vector<Cut> cuts(faces.size());
        std::atomic<std::size_t> claimed{0};

        const auto take = [&faces, &cuts, &claimed]() -> bool {
            const std::size_t slot = claimed.fetch_add(1, std::memory_order_relaxed);
            if (slot >= faces.size()) return false;
            // The face cut down to the glyphs the document drew from it: the
            // expensive half of embedding it, and the half that touches
            // nothing but the face itself -- which is why it runs here, on as
            // many threads as there are faces and cores. The whole file when
            // it would not cut; nothing when there is nothing usable.
            cuts[slot].program = [&entry = faces[slot]]() -> hb_blob_t* {
                if (!entry.font || !entry.font->face() || entry.glyphs.empty()) return nullptr;

                hb_face_t* face = entry.font->face()->handle();
                if (!face || entry.font->size() <= 0.0f) return nullptr;

                // Cut down to what the document drew. Glyph indices are kept as they
                // were, so the page descriptions written earlier still name the right
                // glyphs -- which is the whole reason this can happen after the fact.
                hb_subset_input_t* input = hb_subset_input_create_or_fail();
                if (!input) return nullptr;

                hb_set_t* wanted = hb_subset_input_glyph_set(input);
                hb_map_t* numbering = hb_subset_input_old_to_new_glyph_mapping(input);

                // The pages already name each glyph by the number it will be embedded
                // under, so the cut is told to use exactly those numbers. Letting it
                // choose its own would be smaller still by nothing and would mean
                // laying the document out twice to find out what it chose.
                hb_set_add(wanted, 0);           // the missing-glyph slot, which a font must keep
                hb_map_set(numbering, 0, 0);

                for (std::size_t index = 0; index < entry.glyphs.size(); ++index) {
                    hb_set_add(wanted, entry.glyphs[index]);
                    hb_map_set(numbering, entry.glyphs[index], static_cast<hb_codepoint_t>(index + 1));
                }

                hb_set_t* dropped = hb_subset_input_set(input, HB_SUBSET_SETS_DROP_TABLE_TAG);
                for (const hb_tag_t tag : {HB_TAG('G','S','U','B'), HB_TAG('G','P','O','S'), HB_TAG('G','D','E','F'),
                                           HB_TAG('M','A','T','H'), HB_TAG('B','A','S','E'), HB_TAG('J','S','T','F'),
                                           HB_TAG('k','e','r','n'), HB_TAG('D','S','I','G')}) hb_set_add(dropped, tag);
                hb_face_t* subset = hb_subset_or_fail(face, input);
                hb_subset_input_destroy(input);

                // A font that will not cut is embedded whole. A larger file is worse
                // than a smaller one; a file with no font in it is worse than both.
                // The cut face's blob is compiled from it and owns its own bytes, so
                // the face can go at once.
                hb_blob_t* blob = hb_face_reference_blob(subset ? subset : face);
                if (subset) hb_face_destroy(subset);

                unsigned int length = 0;
                if (!hb_blob_get_data(blob, &length) || length == 0) {
                    hb_blob_destroy(blob);
                    return nullptr;
                }
                return blob;
            }();
            cuts[slot].ready.store(true, std::memory_order_release);
            cuts[slot].ready.notify_all();
            return true;
        };

        std::vector<std::size_t> fonts;
        fonts.reserve(faces.size());

        {
            // One helper fewer than the faces, because the writing thread is
            // one of them; none at all for a document in a single face.
            //
            // A WebAssembly module built without threads has none to start,
            // and starting one there throws; the writing thread cuts every
            // face itself, which is the same work in the same order.
        #if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
            const std::size_t cores = 1;
        #else
            const std::size_t cores = std::max(1u, std::thread::hardware_concurrency());
        #endif
            const std::size_t helpers = faces.empty() ? 0 : std::min(faces.size(), cores) - 1;

        #if defined(_WIN32)
            // The process's own thread pool, rather than threads of this
            // writer's making. Its workers are usually alive already -- the
            // loader started them to map the program's libraries in parallel
            // -- and handing one work costs microseconds where starting a
            // thread costs a tenth of a millisecond, every one of it paid on
            // the writing thread before it can write anything.
            const PTP_WORK work = helpers > 0
                ? CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
                      const auto& claim = *static_cast<const decltype(take)*>(context);
                      while (claim()) {}
                  }, const_cast<void*>(static_cast<const void*>(&take)), nullptr)
                : nullptr;
            for (std::size_t index = 0; work && index < helpers; ++index) SubmitThreadpoolWork(work);
        #else
            std::vector<std::jthread> threads;
            threads.reserve(helpers);
            for (std::size_t index = 0; index < helpers; ++index) {
                threads.emplace_back([&take] {
                    while (take()) {}
                });
            }
        #endif

            for (std::size_t slot = 0; slot < faces.size(); ++slot) {
                while (!cuts[slot].ready.load(std::memory_order_acquire)) {
                    if (!take()) cuts[slot].ready.wait(false, std::memory_order_acquire);
                }

                // The face embedded: its program, its character map and its
                // font dictionaries, the object number of the font returned,
                // or 0 when it could not be cut.
                const std::size_t font = [&file, &entry = faces[slot], blob = cuts[slot].program]() -> std::size_t {
                    if (!blob) return 0;

                    hb_face_t* face = entry.font->face()->handle();

                    unsigned int length = 0;
                    const char* data = hb_blob_get_data(blob, &length);

                    // Everything the font's own tables say about it, read once.
                    const unsigned int units = hb_face_get_upem(face);
                    const float scale = units > 0 ? 1000.0f / static_cast<float>(units) : 1.0f;

                    hb_font_t* measure = hb_font_create(face);
                    hb_font_set_scale(measure, static_cast<int>(units), static_cast<int>(units));
                    hb_ot_font_set_funcs(measure);

                    hb_font_extents_t extents{};
                    hb_font_get_h_extents(measure, &extents);

                    // Outlines drawn as TrueType draws them, in a glyf table --
                    // a system's face, Times New Roman for Arabic -- rather than
                    // as the CFF the carried faces use: embedded as a TrueType
                    // program, under a font that says it is one.
                    hb_blob_t* outlines = hb_face_reference_table(face, HB_TAG('g', 'l', 'y', 'f'));
                    const bool truetype = hb_blob_get_length(outlines) > 0;
                    hb_blob_destroy(outlines);

                    // Its name, as a PDF names a subset: six capitals saying
                    // which cut of the face this is -- the glyphs it holds,
                    // hashed, so the same document names it the same every
                    // time -- then the face's own PostScript name, from its
                    // name table. A viewer shows it; nothing else reads it.
                    std::string name(6, 'A');
                    std::uint64_t digest = 14695981039346656037ull;
                    for (const auto glyph : entry.glyphs) digest = (digest ^ glyph) * 1099511628211ull;
                    for (char& letter : name) {
                        letter = static_cast<char>('A' + digest % 26);
                        digest /= 26;
                    }
                    name += '+';
                    char spelled[128];
                    unsigned int size = sizeof spelled;
                    hb_ot_name_get_utf8(face, HB_OT_NAME_ID_POSTSCRIPT_NAME, HB_LANGUAGE_INVALID, &size, spelled);
                    for (const char letter : std::string_view(spelled, std::min<std::size_t>(size, sizeof spelled - 1))) {
                        if (std::isalnum(static_cast<unsigned char>(letter)) || letter == '-' || letter == '_') name += letter;
                    }
                    if (name.size() == 7) name += "Embedded";

                    const std::size_t program = file.reserve();
                    const std::size_t map = file.reserve();
                    const std::size_t descriptor = file.reserve();
                    const std::size_t descendant = file.reserve();
                    const std::size_t font = file.reserve();

                    file.stream(program, truetype ? "/Length1 " + std::to_string(length) : std::string("/Subtype /OpenType"),
                                std::string_view(data, length));

                    // The character map: which character each glyph stands for. Without
                    // it the text on the page cannot be selected, searched or copied --
                    // the glyphs are there, but nothing says what they mean.
                    std::string cmap;
                    cmap.reserve(entry.glyphs.size() * 16 + 512);
                    cmap += "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                            "/CIDSystemInfo <</Registry (Adobe) /Ordering (UCS) /Supplement 0>> def\n"
                            "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
                            "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";

                    // A hundred pairs at a time, which is the most the format allows in
                    // one block.
                    for (std::size_t start = 0; start < entry.glyphs.size(); start += 100) {
                        const std::size_t stop = std::min(start + 100, entry.glyphs.size());
                        number(cmap, static_cast<std::size_t>(stop - start));
                        cmap += " beginbfchar\n";
                        for (std::size_t index = start; index < stop; ++index) {
                            cmap += '<';
                            hexadecimal(cmap, static_cast<std::uint32_t>(index + 1));
                            cmap += "> <";

                            // Above the basic plane a character is written as the pair of
                            // halves that stand for it. A ligature stands for all of its
                            // letters, one after another.
                            const auto unit = [&cmap](const std::uint32_t point) {
                                if (point > 0xFFFF) {
                                    const std::uint32_t offset = point - 0x10000;
                                    hexadecimal(cmap, 0xD800 + (offset >> 10));
                                    hexadecimal(cmap, 0xDC00 + (offset & 0x3FF));
                                } else {
                                    hexadecimal(cmap, point);
                                }
                            };
                            if (const std::string_view letters = entry.letters[index]; !letters.empty()) {
                                // Read out of the UTF-8 they are kept in, a stray byte as
                                // itself so the reading always moves on.
                                const auto byte = [&letters](const std::size_t at) {
                                    return at < letters.size() ? static_cast<std::uint32_t>(static_cast<unsigned char>(letters[at]))
                                                               : 0u;
                                };
                                for (std::size_t at = 0; at < letters.size();) {
                                    const std::uint32_t lead = byte(at);
                                    if (lead >= 0xF0 && lead < 0xF8) {
                                        unit(((lead & 0x07) << 18) | ((byte(at + 1) & 0x3F) << 12) | ((byte(at + 2) & 0x3F) << 6) |
                                             (byte(at + 3) & 0x3F));
                                        at += 4;
                                    } else if (lead >= 0xE0 && lead < 0xF0) {
                                        unit(((lead & 0x0F) << 12) | ((byte(at + 1) & 0x3F) << 6) | (byte(at + 2) & 0x3F));
                                        at += 3;
                                    } else if (lead >= 0xC0 && lead < 0xE0) {
                                        unit(((lead & 0x1F) << 6) | (byte(at + 1) & 0x3F));
                                        at += 2;
                                    } else {
                                        unit(lead);
                                        at += 1;
                                    }
                                }
                            } else {
                                unit(entry.points[index]);
                            }
                            cmap += ">\n";
                        }
                        cmap += "endbfchar\n";
                    }
                    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend";

                    file.stream(map, "", cmap);

                    file.open(descriptor);
                    file.bytes += "<</Type /FontDescriptor\n/FontName /" + name + "\n/Flags 4\n/FontBBox [0 ";
                    round(file.bytes, static_cast<float>(extents.descender) * scale);
                    file.bytes += " 1000 ";
                    round(file.bytes, static_cast<float>(extents.ascender) * scale);
                    file.bytes += "]\n/ItalicAngle 0\n/Ascent ";
                    round(file.bytes, static_cast<float>(extents.ascender) * scale);
                    file.bytes += "\n/Descent ";
                    round(file.bytes, static_cast<float>(extents.descender) * scale);
                    file.bytes += "\n/CapHeight ";
                    round(file.bytes, static_cast<float>(extents.ascender) * scale);
                    file.bytes += truetype ? "\n/StemV 80\n/FontFile2 " : "\n/StemV 80\n/FontFile3 ";
                    number(file.bytes, program);
                    file.bytes += " 0 R>>";
                    file.close();

                    // Widths, in thousandths of the em. The numbers run from one without
                    // gaps, so the whole face is a single range.
                    std::string widths;
                    widths.reserve(entry.glyphs.size() * 6);
                    widths += "1 [";

                    // Taken by the composer when each glyph was first drawn, rounded the
                    // way it drew them, so the table and the pages agree to the unit.
                    for (std::size_t index = 0; index < entry.glyphs.size(); ++index) {
                        if (index > 0) widths += ' ';
                        number(widths, static_cast<int>(entry.widths[index]));
                    }
                    widths += ']';

                    file.open(descendant);
                    file.bytes += std::string("<</Type /Font\n/Subtype /") + (truetype ? "CIDFontType2" : "CIDFontType0") +
                                  "\n/BaseFont /" + name +
                                  "\n/CIDSystemInfo <</Registry (Adobe) /Ordering (Identity) /Supplement 0>>\n" +
                                  (truetype ? "/CIDToGIDMap /Identity\n" : "") + "/FontDescriptor ";
                    number(file.bytes, descriptor);
                    file.bytes += " 0 R\n/DW 1000\n/W [";
                    file.bytes += widths;
                    file.bytes += "]>>";
                    file.close();

                    file.open(font);
                    file.bytes += "<</Type /Font\n/Subtype /Type0\n/BaseFont /" + name +
                                  "\n/Encoding /Identity-H\n/DescendantFonts [";
                    number(file.bytes, descendant);
                    file.bytes += " 0 R]\n/ToUnicode ";
                    number(file.bytes, map);
                    file.bytes += " 0 R>>";
                    file.close();

                    hb_font_destroy(measure);
                    hb_blob_destroy(blob);

                    Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                                "Embedded {} glyphs in {} bytes", entry.glyphs.size(), length);
                    return font;
                }();
                if (font == 0) {
                    Logger::log(Logger::Type::Layout, Logger::Level::Error,
                                "A face could not be embedded; its text will not show");
                }
                fonts.push_back(font);
            }

        #if defined(_WIN32)
            // Every face is cut by now, but a worker may still be on its way
            // out of take(), which reads what is on this thread's stack; it
            // has to be gone before any of that is.
            if (work) {
                WaitForThreadpoolWorkCallbacks(work, FALSE);
                CloseThreadpoolWork(work);
            }
        #endif
        }

        // One ExtGState per opacity a color actually used, named the way the
        // page descriptions call them: entry `n` is `/GSn`. Both alphas are
        // set to the one value, since nothing here fills and strokes in the
        // same operator, and a document that never wrote anything but opaque
        // colors reserves none of this at all.
        std::vector<std::size_t> states;
        states.reserve(composer.opacities.size());

        for (const float value : composer.opacities) {
            const std::size_t state = file.reserve();
            file.open(state);
            file.bytes += "<</Type /ExtGState\n/ca ";
            fraction(file.bytes, value);
            file.bytes += "\n/CA ";
            fraction(file.bytes, value);
            file.bytes += ">>";
            file.close();
            states.push_back(state);
        }

        // One Image XObject per distinct image an \includegraphics drew,
        // named the way the page descriptions call them: entry `n` is `/Imn`.
        std::vector<std::size_t> pictures;
        pictures.reserve(composer.pictures.size());

        for (const graphics::Image* source : composer.pictures) {
            // The image, already decoded to plain pixels, as an XObject; 0
            // when it could not be compressed into the file.
            const std::size_t object = !source ? 0 : [&file, &picture = *source]() -> std::size_t {
                // A JPEG kept as it came: its own bytes, which the reader
                // decodes -- DCTDecode is the compression it already has.
                if (const std::vector<std::uint8_t>& encoded = picture.encoded; !encoded.empty()) {
                    const std::size_t object = file.reserve();
                    std::string extra = "/Type /XObject\n/Subtype /Image\n/Width ";
                    number(extra, picture.width);
                    extra += "\n/Height ";
                    number(extra, picture.height);
                    extra += picture.components == 1 ? "\n/ColorSpace /DeviceGray" : "\n/ColorSpace /DeviceRGB";
                    extra += "\n/BitsPerComponent 8\n/Filter /DCTDecode\n";
                    file.stream(object, extra,
                                std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
                    Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                                "Embedded a {}x{} JPEG as it came, in {} bytes", picture.width, picture.height,
                                encoded.size());
                    return object;
                }

                const std::vector<std::uint8_t>& pixels = picture.pixels;
                if (pixels.empty()) return 0;

                // The same compression a PNG's own IDAT already used, asked for
                // fresh: what decode() handed back is now plain RGB, not the bytes
                // a PNG or a JPEG arrived as, so there is nothing left to embed
                // as-is even when the source already was compressed.
                uLongf bound = compressBound(static_cast<uLong>(pixels.size()));
                std::string compressed(bound, '\0');

                if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &bound,
                              reinterpret_cast<const Bytef*>(pixels.data()), static_cast<uLong>(pixels.size()),
                              Z_BEST_SPEED) != Z_OK) {
                    return 0;
                }
                compressed.resize(bound);

                const std::size_t object = file.reserve();

                std::string extra = "/Type /XObject\n/Subtype /Image\n/Width ";
                number(extra, picture.width);
                extra += "\n/Height ";
                number(extra, picture.height);
                extra += "\n/ColorSpace /DeviceRGB\n/BitsPerComponent 8\n/Filter /FlateDecode\n";

                file.stream(object, extra, compressed);

                Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                            "Embedded a {}x{} image in {} bytes", picture.width, picture.height, compressed.size());
                return object;
            }();
            if (object == 0) {
                Logger::log(Logger::Type::Layout, Logger::Level::Error,
                            "An image could not be embedded; it will not show");
            }
            pictures.push_back(object);
        }

        // Each page of another PDF drawn, as a Form XObject named the way
        // the page descriptions call them -- entry `n` is `/Fmn` -- the
        // objects it reaches copied in first, each under a number of this
        // file's, and its references renumbered to them.
        std::vector<std::size_t> forms;
        forms.reserve(composer.drawings.size());
        for (const graphics::Drawing* drawn : composer.drawings) {
            using Value = graphics::Drawing::Value;
            const std::vector<graphics::Drawing::Object>& objects = drawn->objects;
            std::vector<std::size_t> numbers(objects.size());
            for (std::size_t& slot : numbers) slot = file.reserve();

            // A value as the file writes it; a dictionary's entries alone,
            // when a stream's dictionary is written around them.
            const auto write = [&numbers](this const auto& self, std::string& out, const Value& value,
                                          const bool inner = false) -> void {
                switch (value.type) {
                    case Value::Type::Null:
                        out += "null";
                        break;
                    case Value::Type::Plain:
                    case Value::Type::Name:
                    case Value::Type::Text:
                        out += value.text;
                        break;
                    case Value::Type::Reference:
                        if (value.number < numbers.size()) {
                            number(out, numbers[value.number]);
                            out += " 0 R";
                        } else {
                            out += "null";
                        }
                        break;
                    case Value::Type::List:
                        out += '[';
                        for (std::size_t index = 0; index < value.items.size(); ++index) {
                            if (index > 0) out += ' ';
                            self(out, value.items[index]);
                        }
                        out += ']';
                        break;
                    case Value::Type::Table:
                        if (!inner) out += "<<";
                        for (const Value& item : value.items) {
                            self(out, item);
                            out += ' ';
                        }
                        if (!inner) out += ">>";
                        break;
                }
            };

            for (std::size_t index = 0; index < objects.size(); ++index) {
                const graphics::Drawing::Object& object = objects[index];
                if (object.streamed) {
                    std::string extra;
                    write(extra, object.value, true);
                    file.stream(numbers[index], extra, object.stream);
                } else {
                    file.open(numbers[index]);
                    write(file.bytes, object.value);
                    file.close();
                }
            }

            const std::string& content = drawn->content;
            uLongf bound = compressBound(static_cast<uLong>(content.size()));
            std::string compressed(bound, '\0');
            const bool packed = compress2(reinterpret_cast<Bytef*>(compressed.data()), &bound,
                                          reinterpret_cast<const Bytef*>(content.data()),
                                          static_cast<uLong>(content.size()), Z_BEST_SPEED) == Z_OK;
            compressed.resize(packed ? bound : 0);

            const std::size_t form = file.reserve();
            std::string extra = "/Type /XObject /Subtype /Form /BBox [";
            for (std::size_t corner = 0; corner < 4; ++corner) {
                if (corner > 0) extra += ' ';
                fraction(extra, drawn->box[corner]);
            }
            extra += "] /Resources ";
            if (drawn->resources.type == Value::Type::Null) {
                extra += "<<>>";
            } else {
                write(extra, drawn->resources);
            }
            if (drawn->group.type != Value::Type::Null) {
                extra += " /Group ";
                write(extra, drawn->group);
            }
            extra += packed ? " /Filter /FlateDecode " : " ";
            file.stream(form, extra, packed ? std::string_view(compressed) : std::string_view(content));
            forms.push_back(form);
            Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                        "Embedded a {} by {} point page of another PDF, with {} objects", drawn->width(),
                        drawn->height(), objects.size());
        }

        // Every page's number first, since a link on one may go to another.
        std::vector<std::size_t> sheets;
        sheets.reserve(bodies.size());
        for (std::size_t index = 0; index < bodies.size(); ++index) sheets.push_back(file.reserve());

        // A string as a PDF writes one: plain in parentheses, its parentheses
        // and backslashes escaped; anything past ASCII as UTF-16 with its
        // byte order mark, in hexadecimal.
        const auto text = [](std::string& out, const std::string_view written) {
            if (std::ranges::all_of(written, [](const char letter) { return static_cast<unsigned char>(letter) < 0x80; })) {
                out += '(';
                for (const char letter : written) {
                    if (letter == '(' || letter == ')' || letter == '\\') out += '\\';
                    out += letter;
                }
                out += ')';
                return;
            }
            out += "<FEFF";
            for (std::size_t at = 0; at < written.size();) {
                const auto lead = static_cast<unsigned char>(written[at]);
                const std::size_t span = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
                std::uint32_t point = span == 1 ? lead : lead & (0xFF >> (span + 1));
                for (std::size_t more = 1; more < span && at + more < written.size(); ++more) {
                    point = (point << 6) | (static_cast<unsigned char>(written[at + more]) & 0x3F);
                }
                at += span;
                if (point >= 0x10000) {
                    point -= 0x10000;
                    hexadecimal(out, 0xD800 + (point >> 10));
                    hexadecimal(out, 0xDC00 + (point & 0x3FF));
                } else {
                    hexadecimal(out, point);
                }
            }
            out += '>';
        };

        // Where an anchor stands, as a destination a reader goes to: its
        // page, a little above it.
        const std::vector<Composer::Place>& places = composer.places;
        const auto destination = [&](std::string& out, const std::size_t anchor) {
            if (anchor >= places.size() || places[anchor].page >= sheets.size()) return false;
            out += '[';
            number(out, sheets[places[anchor].page]);
            out += " 0 R /XYZ 0 ";
            round(out, std::min(page.height, page.height - places[anchor].down + 24.0f));
            out += " null]";
            return true;
        };

        // The links of each page: an annotation each area they cover, framed
        // as hyperref frames one or not at all, going to an address or to an
        // anchor's place. Each marked to print, as PDF/A has every one.
        const std::vector<std::vector<Composer::Link>>& links = composer.links;
        std::vector<std::vector<std::size_t>> annotations(sheets.size());
        for (std::size_t sheet = 0; sheet < sheets.size() && sheet < links.size(); ++sheet) {
            for (const Composer::Link& link : links[sheet]) {
                std::string action;
                if (!link.target.empty()) {
                    action = "/A <</S /URI /URI ";
                    text(action, link.target);
                    action += ">>";
                } else {
                    action = "/Dest ";
                    if (!destination(action, link.anchor)) continue;
                }
                for (const auto& [left, top, right, bottom] : link.areas) {
                    const std::size_t made = file.reserve();
                    file.open(made);
                    file.bytes += "<</Type /Annot /Subtype /Link /F 4 /Rect [";
                    for (const float value : {left - 1.0f, page.height - bottom - 1.0f, right + 1.0f, page.height - top + 1.0f}) {
                        fraction(file.bytes, value);
                        file.bytes += ' ';
                    }
                    file.bytes += "] /Border [0 0 ";
                    file.bytes += link.border.alpha > 0.0f ? "1" : "0";
                    file.bytes += "] /C [";
                    for (const float value : {link.border.r, link.border.g, link.border.b}) {
                        fraction(file.bytes, value);
                        file.bytes += ' ';
                    }
                    file.bytes += "] ";
                    file.bytes += action;
                    file.bytes += ">>";
                    file.close();
                    annotations[sheet].push_back(made);
                }
            }
        }

        for (std::size_t sheet = 0; sheet < bodies.size(); ++sheet) {
            const std::size_t body = bodies[sheet];
            const std::size_t leaf = sheets[sheet];

            file.open(leaf);
            file.bytes += "<</Type /Page\n/Parent ";
            number(file.bytes, tree);
            file.bytes += " 0 R\n/MediaBox [0 0 ";
            round(file.bytes, page.width);
            file.bytes += ' ';
            round(file.bytes, page.height);
            file.bytes += "]\n/Contents ";
            number(file.bytes, body);
            file.bytes += " 0 R\n/Resources <</Font <<";

            // Every face, on every page. Naming a font a page does not use
            // costs one entry in a dictionary; working out which pages use
            // which would cost a pass over the marks to save it.
            for (std::size_t slot = 0; slot < fonts.size(); ++slot) {
                if (fonts[slot] == 0) continue;
                file.bytes += "/F";
                number(file.bytes, slot);
                file.bytes += ' ';
                number(file.bytes, fonts[slot]);
                file.bytes += " 0 R";
            }
            file.bytes += ">>";

            if (!states.empty()) {
                file.bytes += "/ExtGState <<";
                for (std::size_t slot = 0; slot < states.size(); ++slot) {
                    file.bytes += "/GS";
                    number(file.bytes, slot);
                    file.bytes += ' ';
                    number(file.bytes, states[slot]);
                    file.bytes += " 0 R";
                }
                file.bytes += ">>";
            }

            if (!pictures.empty() || !forms.empty()) {
                file.bytes += "/XObject <<";
                for (std::size_t slot = 0; slot < pictures.size(); ++slot) {
                    if (pictures[slot] == 0) continue;
                    file.bytes += "/Im";
                    number(file.bytes, slot);
                    file.bytes += ' ';
                    number(file.bytes, pictures[slot]);
                    file.bytes += " 0 R";
                }
                for (std::size_t slot = 0; slot < forms.size(); ++slot) {
                    file.bytes += "/Fm";
                    number(file.bytes, slot);
                    file.bytes += ' ';
                    number(file.bytes, forms[slot]);
                    file.bytes += " 0 R";
                }
                file.bytes += ">>";
            }

            file.bytes += ">>";
            if (!annotations[sheet].empty()) {
                file.bytes += "\n/Annots [";
                for (const std::size_t made : annotations[sheet]) {
                    number(file.bytes, made);
                    file.bytes += " 0 R ";
                }
                file.bytes += ']';
            }
            file.bytes += ">>";
            file.close();
        }

        file.open(tree);
        file.bytes += "<</Type /Pages\n/Count ";
        number(file.bytes, sheets.size());
        file.bytes += "\n/Kids [";
        for (std::size_t index = 0; index < sheets.size(); ++index) {
            if (index > 0) file.bytes += ' ';
            number(file.bytes, sheets[index]);
            file.bytes += " 0 R";
        }
        file.bytes += "]>>";
        file.close();

        // The outline a reader lists beside the pages: each bookmark under
        // the nearest before it that stands higher, going to its heading.
        const layout::Document::Metadata& about = composer.document.metadata;
        const std::vector<layout::Document::Bookmark>& marks = about.bookmarks;
        std::size_t outline = 0;
        if (!marks.empty()) {
            outline = file.reserve();
            std::vector<std::size_t> made(marks.size());
            for (std::size_t& slot : made) slot = file.reserve();
            // Each one's parent, and each one's children, the top's under
            // the outline itself.
            std::vector<std::size_t> parents(marks.size(), marks.size());
            std::vector<std::vector<std::size_t>> children(marks.size() + 1);
            std::vector<std::size_t> open;
            for (std::size_t index = 0; index < marks.size(); ++index) {
                while (!open.empty() && marks[open.back()].level >= marks[index].level) open.pop_back();
                parents[index] = open.empty() ? marks.size() : open.back();
                children[parents[index]].push_back(index);
                open.push_back(index);
            }
            const auto named = [&](const std::size_t index) { return index == marks.size() ? outline : made[index]; };
            const auto reference = [&](const std::string_view key, const std::size_t object) {
                file.bytes += key;
                file.bytes += ' ';
                number(file.bytes, object);
                file.bytes += " 0 R ";
            };
            for (std::size_t index = 0; index < marks.size(); ++index) {
                file.open(made[index]);
                file.bytes += "<</Title ";
                text(file.bytes, marks[index].title);
                file.bytes += ' ';
                reference("/Parent", named(parents[index]));
                const std::vector<std::size_t>& siblings = children[parents[index]];
                const auto place = std::ranges::find(siblings, index) - siblings.begin();
                if (place > 0) reference("/Prev", made[siblings[static_cast<std::size_t>(place) - 1]]);
                if (static_cast<std::size_t>(place) + 1 < siblings.size()) {
                    reference("/Next", made[siblings[static_cast<std::size_t>(place) + 1]]);
                }
                if (!children[index].empty()) {
                    reference("/First", made[children[index].front()]);
                    reference("/Last", made[children[index].back()]);
                    file.bytes += "/Count ";
                    number(file.bytes, children[index].size());
                    file.bytes += ' ';
                }
                std::string where;
                if (destination(where, marks[index].anchor)) file.bytes += "/Dest " + where;
                file.bytes += ">>";
                file.close();
            }
            file.open(outline);
            file.bytes += "<</Type /Outlines ";
            reference("/First", made[children[marks.size()].front()]);
            reference("/Last", made[children[marks.size()].back()]);
            file.bytes += "/Count ";
            number(file.bytes, children[marks.size()].size());
            file.bytes += ">>";
            file.close();
        }

        // What the file says of itself: its title and the rest as hyperref
        // gave them, and what made it.
        static constexpr std::string_view producer = "latex";
        const std::size_t information = file.reserve();
        file.open(information);
        file.bytes += "<</Producer ";
        text(file.bytes, producer);
        for (const auto& [key, value] : {std::pair{"/Title", &about.title}, std::pair{"/Author", &about.author},
                                         std::pair{"/Subject", &about.subject}, std::pair{"/Keywords", &about.keywords}}) {
            if (value->empty()) continue;
            file.bytes += ' ';
            file.bytes += key;
            file.bytes += ' ';
            text(file.bytes, *value);
        }
        file.bytes += ">>";
        file.close();

        // PDF/A-2b, the archival standard, when a document asks to keep to
        // it: the same account of itself as XMP, which an archive reads, and
        // the colors it is drawn in stated by an ICC profile of sRGB's.
        std::size_t metadata = 0;
        std::size_t intent = 0;
        if (about.archival) {
            const auto escaped = [](const std::string_view written) {
                std::string out;
                for (const char letter : written) {
                    if (letter == '&') out += "&amp;";
                    else if (letter == '<') out += "&lt;";
                    else if (letter == '>') out += "&gt;";
                    else out += letter;
                }
                return out;
            };
            std::string xmp = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                              "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
                              "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                              "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" "
                              "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">"
                              "<pdfaid:part>2</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance>"
                              "<pdf:Producer>" + std::string(producer) + "</pdf:Producer>";
            if (!about.title.empty()) {
                xmp += "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" + escaped(about.title) +
                       "</rdf:li></rdf:Alt></dc:title>";
            }
            if (!about.author.empty()) {
                xmp += "<dc:creator><rdf:Seq><rdf:li>" + escaped(about.author) + "</rdf:li></rdf:Seq></dc:creator>";
            }
            if (!about.subject.empty()) {
                xmp += "<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">" + escaped(about.subject) +
                       "</rdf:li></rdf:Alt></dc:description>";
            }
            if (!about.keywords.empty()) xmp += "<pdf:Keywords>" + escaped(about.keywords) + "</pdf:Keywords>";
            xmp += "</rdf:Description></rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>";
            metadata = file.reserve();
            file.stream(metadata, "/Type /Metadata /Subtype /XML ", xmp);

            // A display profile of sRGB, ICC version 2: its header, then its
            // white point, its three primaries adapted to D50, and a gamma of
            // 2.2 for each channel, the curve shared.
            std::string icc(128, '\0');
            const auto word = [](std::string& out, const std::uint32_t value) {
                for (const int shift : {24, 16, 8, 0}) out += static_cast<char>((value >> shift) & 0xFF);
            };
            const auto put = [](std::string& out, const std::size_t at, const std::uint32_t value) {
                for (std::size_t index = 0; index < 4; ++index) {
                    out[at + index] = static_cast<char>((value >> (24 - 8 * index)) & 0xFF);
                }
            };
            const auto fixed = [](const double value) { return static_cast<std::uint32_t>(std::lround(value * 65536.0)); };
            const auto xyz = [&word, &fixed](const double x, const double y, const double z) {
                std::string tag = "XYZ ";
                word(tag, 0);
                for (const double value : {x, y, z}) word(tag, fixed(value));
                return tag;
            };
            std::string curve = "curv";
            word(curve, 0);
            word(curve, 1);
            curve += "\x02\x33\x00\x00";   // 2.2 as u8Fixed8, then padding
            std::string description = "desc";
            word(description, 0);
            word(description, 5);
            description += std::string("sRGB\0", 5);
            description += std::string(4 + 4 + 2 + 1 + 67, '\0');
            while (description.size() % 4 != 0) description += '\0';
            std::string notice = "text";
            word(notice, 0);
            notice += std::string("No copyright\0\0\0\0", 16);
            const std::vector<std::pair<std::string_view, std::string>> tags{
                {"desc", description}, {"cprt", notice}, {"wtpt", xyz(0.9642, 1.0, 0.8249)},
                {"rXYZ", xyz(0.4361, 0.2225, 0.0139)}, {"gXYZ", xyz(0.3851, 0.7169, 0.0971)},
                {"bXYZ", xyz(0.1431, 0.0606, 0.7141)}, {"rTRC", curve},
            };
            // The table: every tag, and the other two curves pointing at the
            // red one's data.
            std::string table;
            word(table, static_cast<std::uint32_t>(tags.size() + 2));
            std::size_t offset = 128 + 4 + 12 * (tags.size() + 2);
            std::string data;
            std::size_t shared = 0;
            for (const auto& [signature, body] : tags) {
                table += signature;
                word(table, static_cast<std::uint32_t>(offset + data.size()));
                word(table, static_cast<std::uint32_t>(body.size()));
                if (signature == "rTRC") shared = offset + data.size();
                data += body;
            }
            for (const std::string_view signature : {"gTRC", "bTRC"}) {
                table += signature;
                word(table, static_cast<std::uint32_t>(shared));
                word(table, static_cast<std::uint32_t>(curve.size()));
            }
            icc += table;
            icc += data;
            put(icc, 0, static_cast<std::uint32_t>(icc.size()));
            put(icc, 8, 0x02100000);
            icc.replace(12, 4, "mntr");
            icc.replace(16, 4, "RGB ");
            icc.replace(20, 4, "XYZ ");
            put(icc, 24, (2024u << 16) | 1u);   // 2024-01-01, 00:00:00
            put(icc, 28, 1u << 16);
            icc.replace(36, 4, "acsp");
            put(icc, 68, fixed(0.9642));
            put(icc, 72, fixed(1.0));
            put(icc, 76, fixed(0.8249));

            const std::size_t profile = file.reserve();
            file.stream(profile, "/N 3 ", icc);
            intent = file.reserve();
            file.open(intent);
            file.bytes += "<</Type /OutputIntent /S /GTS_PDFA1 /OutputConditionIdentifier (sRGB IEC61966-2.1) "
                          "/Info (sRGB IEC61966-2.1) /DestOutputProfile ";
            number(file.bytes, profile);
            file.bytes += " 0 R>>";
            file.close();
        }

        file.open(catalogue);
        file.bytes += "<</Type /Catalog\n/Pages ";
        number(file.bytes, tree);
        file.bytes += " 0 R";
        if (outline != 0) {
            file.bytes += "\n/Outlines ";
            number(file.bytes, outline);
            file.bytes += " 0 R /PageMode /UseOutlines";
        }
        if (metadata != 0) {
            file.bytes += "\n/Metadata ";
            number(file.bytes, metadata);
            file.bytes += " 0 R /OutputIntents [";
            number(file.bytes, intent);
            file.bytes += " 0 R]";
        }
        file.bytes += ">>";
        file.close();

        // The cross-reference table: where every object starts, in order, each
        // entry exactly twenty bytes so a reader can seek straight to one.
        const std::size_t table = file.bytes.size();
        file.bytes += "xref\n0 ";
        number(file.bytes, file.offsets.size() + 1);
        file.bytes += "\n0000000000 65535 f \n";

        for (const std::size_t offset : file.offsets) {
            const std::string entry = std::to_string(offset);
            file.bytes.append(10 - entry.size(), '0');
            file.bytes += entry;
            file.bytes += " 00000 n \n";
        }

        // The file's identifier, which PDF/A asks for: zlib's two checksums of
        // everything written, and its length -- the same for the same
        // document every time.
        const auto* written = reinterpret_cast<const Bytef*>(file.bytes.data());
        const auto length = static_cast<uInt>(file.bytes.size());
        std::string identity;
        for (const std::uint32_t part : {static_cast<std::uint32_t>(crc32(0, written, length)),
                                         static_cast<std::uint32_t>(adler32(1, written, length)),
                                         static_cast<std::uint32_t>(file.bytes.size()), 0x4C615465u}) {
            hexadecimal(identity, part >> 16);
            hexadecimal(identity, part & 0xFFFF);
        }

        file.bytes += "trailer\n<</Size ";
        number(file.bytes, file.offsets.size() + 1);
        file.bytes += "\n/Root ";
        number(file.bytes, catalogue);
        file.bytes += " 0 R /Info ";
        number(file.bytes, information);
        file.bytes += " 0 R /ID [<" + identity + "> <" + identity + ">]>>\nstartxref\n";
        number(file.bytes, table);
        file.bytes += "\n%%EOF\n";

        return std::move(file.bytes);
    }

    bool Pdf::compose(
        Composer& composer,
        const memory::Slice<layout::Pager::Page> pages,
        const std::string_view path
    ) {
        if (path.empty()) return false;

        const std::string bytes = render(composer, pages);
        if (bytes.empty()) return false;

    #if defined(_WIN32)
        // Handed to the system whole: one create and one write, where a
        // stream would first copy a file that is already whole in memory
        // through a buffer of its own. Opened the way the stream opened it --
        // truncated, and shared, so a viewer that has the last run's output
        // open does not stop this one being written.
        const HANDLE output = CreateFileA(std::string(path).c_str(), GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) return false;

        bool written = true;
        for (std::size_t offset = 0; written && offset < bytes.size();) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 30));
            DWORD moved = 0;
            written = WriteFile(output, bytes.data() + offset, chunk, &moved, nullptr) && moved == chunk;
            offset += moved;
        }
        CloseHandle(output);
        return written;
    #else
        std::ofstream output(std::string(path), std::ios::binary | std::ios::trunc);
        if (!output) return false;

        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const bool written = output.good();
        return written;
    #endif
    }

}
