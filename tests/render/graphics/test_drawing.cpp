#include "render/graphics/drawing.hpp"

#include <cassert>
#include <span>
#include <string>
#include <vector>

// A page of another PDF, read as a reader reads one: through a classic
// cross-reference table, through a cross-reference stream whose objects sit
// in an object stream, and the long way when the table is wrong -- the page
// asked for found in the tree, its box, its contents, and every object its
// resources reach copied and renumbered. Bytes that are no PDF, or an
// encrypted one, are refused.

namespace graphics = render::graphics;

/// @brief A PDF written object by object, its offsets kept for the table.
struct Writer {
    std::string bytes{"%PDF-1.5\n"};
    std::vector<std::size_t> offsets{};

    /// @brief An object, numbered from 1 in the order written.
    void object(const std::string& body) {
        offsets.push_back(bytes.size());
        bytes += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    }

    /// @brief A stream object.
    void stream(const std::string& dictionary, const std::string& data) {
        object("<<" + dictionary + "/Length " + std::to_string(data.size()) + ">>\nstream\n" + data + "\nendstream");
    }

    /// @brief The classic table, the trailer naming the root, and the end.
    std::string finish(const std::string& trailer, const std::size_t shift = 0) {
        const std::size_t start = bytes.size();
        bytes += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
        for (const std::size_t offset : offsets) {
            std::string padded = std::to_string(offset + shift);
            padded.insert(0, 10 - padded.size(), '0');
            bytes += padded + " 00000 n \n";
        }
        bytes += "trailer\n<<" + trailer + "/Size " + std::to_string(offsets.size() + 1) + ">>\nstartxref\n" +
                 std::to_string(start) + "\n%%EOF\n";
        return bytes;
    }
};

/// @brief A page drawn from bytes.
static std::optional<graphics::Drawing> read(const std::string& bytes, const int page = 1) {
    return graphics::Drawing::decode(std::as_bytes(std::span{bytes}), page);
}

int main() {
    // --- A classic table ---------------------------------------------------------------
    Writer classic;
    classic.object("<</Type /Catalog /Pages 2 0 R>>");
    classic.object("<</Type /Pages /Kids [3 0 R 6 0 R] /Count 2 /MediaBox [0 0 200 100]>>");
    classic.object("<</Type /Page /Parent 2 0 R /Resources <</Font <</F1 4 0 R>>>> /Contents 5 0 R>>");
    classic.object("<</Type /Font /Subtype /Type1 /BaseFont /Helvetica /Widths [1 2 (3)] /Parent 2 0 R>>");
    classic.stream("", "BT /F1 12 Tf 10 10 Td (Hi) Tj ET");
    classic.object("<</Type /Page /Parent 2 0 R /CropBox [10 20 110 70] /Contents 7 0 R>>");
    classic.stream("", "0 0 m 50 50 l S");
    const std::string one = classic.finish("/Root 1 0 R");

    const auto first = read(one);
    assert((first.has_value()) && "a PDF's page is read");
    assert((first && first->width() == 200.0f && first->height() == 100.0f) && "its box inherited from the tree");
    assert((first && first->content.starts_with("BT /F1 12 Tf")) && "its contents");
    assert((first && first->resources.type == graphics::Drawing::Value::Type::Table) && "its resources");
    assert((first && first->objects.size() == 1) && "every object they reach, and nothing else");
    assert((first && first->objects[0].value.items.size() == 8) && "a /Parent left behind");

    const auto second = read(one, 2);
    assert((second && second->box[0] == 10.0f && second->width() == 100.0f && second->height() == 50.0f) &&
           "another page, seen through its crop box");
    assert((second && second->content.starts_with("0 0 m")) && "with its own contents");
    assert((!read(one, 3)) && "a page the file does not have is none");

    // --- A cross-reference stream and an object stream ----------------------------------
    {
        Writer modern;
        modern.object("<</Type /Catalog /Pages 2 0 R>>");
        modern.object("<</Type /Pages /Kids [3 0 R] /Count 1>>");
        // Objects 3 and 4 live in object stream 5.
        const std::string sheet = "<</Type /Page /MediaBox [0 0 300 150] /Resources 4 0 R /Contents 6 0 R>> ";
        const std::string held = sheet + "<</ProcSet [/PDF]>>";
        const std::string header = "3 0 4 " + std::to_string(sheet.size()) + " ";
        const std::string objects = header + held;
        const std::size_t stream = modern.bytes.size();
        modern.bytes += "5 0 obj\n<</Type /ObjStm /N 2 /First " + std::to_string(header.size()) + " /Length " +
                        std::to_string(objects.size()) + ">>\nstream\n" + objects + "\nendstream\nendobj\n";
        const std::size_t content = modern.bytes.size();
        modern.bytes += "6 0 obj\n<</Length 10>>\nstream\n1 0 0 1 cm\nendstream\nendobj\n";

        // The table as a stream: a byte for the kind, two for the place,
        // one for the index, uncompressed.
        std::string table;
        const auto entry = [&table](const int kind, const std::size_t place, const int index) {
            table += static_cast<char>(kind);
            table += static_cast<char>((place >> 8) & 0xFF);
            table += static_cast<char>(place & 0xFF);
            table += static_cast<char>(index);
        };
        entry(0, 0, 0);
        entry(1, modern.offsets[0], 0);
        entry(1, modern.offsets[1], 0);
        entry(2, 5, 0);
        entry(2, 5, 1);
        entry(1, stream, 0);
        entry(1, content, 0);
        const std::size_t cross = modern.bytes.size();
        entry(1, cross, 0);
        modern.bytes += "7 0 obj\n<</Type /XRef /Size 8 /W [1 2 1] /Root 1 0 R /Length " + std::to_string(table.size()) +
                        ">>\nstream\n" + table + "\nendstream\nendobj\nstartxref\n" + std::to_string(cross) +
                        "\n%%EOF\n";

        const auto drawn = read(modern.bytes);
        assert((drawn && drawn->width() == 300.0f) && "a cross-reference stream, its page in an object stream");
        assert((drawn && drawn->resources.type == graphics::Drawing::Value::Type::Reference &&
                drawn->objects.size() == 1 && drawn->objects[0].value.items.size() == 2) &&
               "its resources, in the object stream too, copied and renumbered");
        assert((drawn && drawn->content.starts_with("1 0 0 1 cm")) && "its contents");
    }

    // --- A table that lies -------------------------------------------------------------
    {
        Writer wrong;
        wrong.object("<</Type /Catalog /Pages 2 0 R>>");
        wrong.object("<</Type /Pages /Kids [3 0 R] /Count 1>>");
        wrong.object("<</Type /Page /MediaBox [0 0 50 60] /Contents 4 0 R>>");
        wrong.stream("", "q Q");
        const std::string shifted = wrong.finish("/Root 1 0 R", 7);
        const auto drawn = read(shifted);
        assert((drawn && drawn->width() == 50.0f && drawn->height() == 60.0f) &&
               "a file whose table is wrong is read the long way, every object found where it stands");
    }

    // --- What is refused -----------------------------------------------------------------
    assert((!read("not a PDF at all")) && "bytes that are no PDF");
    assert((!read("")) && "nothing");
    {
        Writer locked;
        locked.object("<</Type /Catalog /Pages 2 0 R>>");
        locked.object("<</Type /Pages /Kids [3 0 R] /Count 1>>");
        locked.object("<</Type /Page /MediaBox [0 0 50 60]>>");
        locked.object("<</Filter /Standard /V 1>>");
        assert((!read(locked.finish("/Root 1 0 R /Encrypt 4 0 R"))) && "an encrypted file");
    }
    return 0;
}
