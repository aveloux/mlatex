#include "render/graphics/drawing.hpp"

#include <cassert>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// An EPS file's picture: its program run -- procedures, dictionaries, loops
// -- and what it paints written as a PDF page's content, every path through
// the transformation it was built under, its text in a standard face, an
// image inline; a box from its comments, a binary header passed over, and a
// program that never ends stopped.

namespace graphics = render::graphics;

/// @brief A file's picture, read as \\includegraphics reads one.
static std::optional<graphics::Drawing> read(const std::string_view bytes) {
    return graphics::Drawing::decode(std::as_bytes(std::span{bytes}));
}

/// @brief How many times a text holds another.
static std::size_t count(const std::string_view text, const std::string_view part) {
    std::size_t found = 0;
    for (std::size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size())) ++found;
    return found;
}

int main() {
    // --- Paths, painting and the state ----------------------------------------------------
    {
        const auto drawn = read("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 10 20 110 70\n"
                                "/box { 4 dict begin /h exch def /w exch def /y exch def /x exch def\n"
                                "  newpath x y moveto w 0 rlineto 0 h rlineto w neg 0 rlineto closepath end } def\n"
                                "1 0 0 setrgbcolor 10 20 30 40 box fill\n"
                                "0 1 2 { 20 mul 50 add 20 10 10 box 0 0 1 setrgbcolor fill } for\n"
                                "2 setlinewidth 0 setgray 60 45 15 0 360 arc stroke\n");
        assert((drawn && drawn->width() == 100.0f && drawn->height() == 50.0f) && "its box from its comment");
        const std::string& content = drawn->content;
        assert((count(content, "\nf\n") == 4) && "a box filled, and three more from a loop");
        assert((content.contains("1 0 0 rg") && content.contains("0 0 1 rg")) && "each in its colour");
        assert((count(content, " c\n") == 4 && content.contains("2 w") && content.contains("\nS\n")) &&
               "a circle in four curves, stroked as wide as it was set");
        assert((content.contains("10 20 m\n") && content.contains("40 20 l\n") && content.contains("h\n")) &&
               "a path's points where its procedure put them");
    }
    {
        // A path built under a transformation keeps it; the stroke after the
        // transformation is undone is as wide as it was. And gsave keeps
        // the path, grestore brings it back.
        const auto drawn = read("%!PS\n%%BoundingBox: 0 0 100 100\n"
                                "matrix currentmatrix 50 50 translate 2 1 scale 0 0 10 0 360 arc setmatrix 1 setlinewidth stroke\n"
                                "newpath 0 0 moveto 10 10 lineto gsave 1 0 0 setrgbcolor fill grestore 0 setgray stroke\n");
        assert((drawn && drawn->content.contains("70 50 m\n") && drawn->content.contains("1 w")) &&
               "an ellipse at its place, twice as wide, its line one point");
        assert((drawn->content.contains("\nq\n") && drawn->content.contains("\nQ\n") && count(drawn->content, "0 0 m\n") == 2) &&
               "a path filled in a saved state, then stroked again after it");
    }

    // --- Text ---------------------------------------------------------------------------
    {
        const auto drawn = read("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 30\n"
                                "/Times-Bold findfont 12 scalefont setfont 50 10 moveto\n"
                                "(Mid) dup stringwidth pop 2 div neg 0 rmoveto show\n"
                                "/Helvetica 8 selectfont 0 0 moveto (a\\(b\\)) show\n");
        assert((drawn && drawn->content.contains("BT /F0 1 Tf 12 0 0 12") && drawn->content.contains("(Mid) Tj")) &&
               "text in the font it set, at its size");
        assert((drawn->content.contains("BT /F1") && drawn->content.contains("(a\\(b\\)) Tj")) &&
               "a second face, its parentheses escaped");
        const graphics::Drawing::Value& fonts = drawn->resources.items.at(1);
        assert((fonts.items.size() == 4 && fonts.items[1].items[5].text == "/Times-Bold" &&
                fonts.items[3].items[5].text == "/Helvetica") &&
               "each a standard face, named in the page's resources");
    }

    // --- An image -----------------------------------------------------------------------
    {
        const auto drawn = read("%!PS\n%%BoundingBox: 0 0 20 20\n"
                                "20 20 scale 2 2 8 [2 0 0 -2 0 2] {currentfile 4 string readhexstring pop} image\n"
                                "00FF80FF\nshowpage\n");
        assert((drawn && drawn->content.contains("BI /W 2 /H 2 /BPC 8 /CS /G /F /AHx ID") &&
                drawn->content.contains("00FF80FF>")) &&
               "an image's samples, read from the file after it, drawn inline");
    }

    // --- What is passed over, stopped and refused ---------------------------------------
    {
        // A DOS EPS's header names where the PostScript is.
        std::string header{"\xC5\xD0\xD3\xC6", 4};
        const std::string program = "%!PS\n%%BoundingBox: 0 0 5 5\n0 0 5 5 rectfill\n";
        for (const std::size_t value : {std::size_t{30}, program.size()}) {
            for (int shift = 0; shift < 32; shift += 8) header += static_cast<char>((value >> shift) & 0xFF);
        }
        header.resize(30, '\0');
        const auto drawn = read(header + program);
        assert((drawn && drawn->width() == 5.0f && drawn->content.contains("\nf\n")) && "a binary header passed over");
    }
    {
        const auto drawn = read("%!PS\n%%BoundingBox: 0 0 5 5\n0 0 moveto 5 5 lineto stroke { } loop 0 0 5 5 rectfill\n");
        assert((drawn && drawn->content.contains("\nS\n") && !drawn->content.contains("\nf\n")) &&
               "a program that never ends is stopped, what it drew kept");
    }
    {
        const auto drawn = read("%!PS\n%%BoundingBox: 0 0 5 5\n{ 0 0 moveto 5 5 lineto stroke stop 1 1 2 2 rectfill } stopped pop "
                                "0 0 5 5 rectfill\n");
        assert((drawn && drawn->content.contains("\nS\n") && drawn->content.contains("\nf\n") &&
                count(drawn->content, "\nf\n") == 1) &&
               "a stop ends what `stopped` runs, and the program goes on");
    }
    assert((!read("%!PS\nnewpath 0 0 moveto\n")) && "a program with no bounding box");
    return 0;
}
