#include "render/graphics/drawing.hpp"

#include <cassert>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// An SVG file's picture: its page from its size and view box, its shapes and
// paths through their groups' transformations in the style each inherits and
// gives itself -- attributes, `<style>` rules, `style` -- written as a PDF
// page's content, y turned upward; text in a standard face, in its order,
// from its anchor; `<use>`, a gradient's colour, opacity, and a clip.

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
    // --- The page and its shapes ------------------------------------------------------------
    {
        const auto drawn = read(R"svg(<?xml version="1.0"?>
<!-- shapes -->
<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 200 100">
  <rect x="10" y="10" width="40" height="20" fill="#ff0000"/>
  <circle cx="100" cy="50" r="10" style="fill:none; stroke:blue; stroke-width:2"/>
  <g transform="translate(150,0)"><line x1="0" y1="0" x2="0" y2="100" stroke="green"/></g>
</svg>)svg");
        assert((drawn && drawn->width() == 150.0f && drawn->height() == 75.0f) && "a page of its pixels, three quarters of a point each");
        const std::string& content = drawn->content;
        assert((content.contains("1 0 0 rg") && content.contains("7.5 67.5 m\n") && content.contains("h\nf\n")) &&
               "a rectangle filled, its corner turned upward into points");
        assert((count(content, " c\n") >= 4 && content.contains("0 0 1 RG") && content.contains("1.5 w")) &&
               "a circle in curves, stroked as its style says and not filled");
        assert((content.contains("112.5 75 m\n") && content.contains("112.5 0 l\n") && content.contains("0 0.502 0 RG")) &&
               "a line moved by its group, in green");
    }
    {
        // A path's commands, relative and absolute, smooth curves and arcs.
        const auto drawn = read(R"svg(<svg width="100" height="100"><path d="M10,10 l20,0 h10 v10 C40,30 50,30 50,20 s10,-10 20,0
            q5,5 10,0 t10,0 a5,5 0 0,1 10,0 Z" fill="none" stroke="black"/></svg>)svg");
        assert((drawn && drawn->content.contains("7.5 67.5 m\n") && drawn->content.contains("22.5 67.5 l\n") &&
                drawn->content.contains("30 67.5 l\n") && drawn->content.contains("30 60 l\n")) &&
               "lines, relative and across and down");
        assert((count(drawn->content, " c\n") >= 5 && drawn->content.contains("h\nS\n")) && "curves, smooth ones and an arc, closed");
    }

    // --- Style --------------------------------------------------------------------------------
    {
        const auto drawn = read(R"svg(<svg width="100" height="100">
  <defs><style>.warm { fill: orange } #cold { fill: blue }</style>
    <linearGradient id="sky"><stop offset="0" stop-color="#00ff00"/></linearGradient>
    <rect id="unit" width="5" height="5"/></defs>
  <rect class="spare warm" width="10" height="10"/>
  <rect id="cold" x="20" width="10" height="10" fill="red"/>
  <rect x="40" width="10" height="10" fill="url(#sky)" opacity="0.5"/>
  <use href="#unit" x="60" y="60"/>
</svg>)svg");
        assert((drawn && drawn->content.contains("1 0.647 0 rg") && drawn->content.contains("0 0 1 rg")) &&
               "a class's rule and an id's, the rule over the attribute");
        assert((drawn->content.contains("0 1 0 rg") && drawn->content.contains("/G0 gs")) && "a gradient's colour, half seen");
        assert((drawn->content.contains("45 30 m\n")) && "a shape drawn again where `<use>` puts it");
        const graphics::Drawing::Value& resources = drawn->resources;
        assert((resources.items.size() == 2 && resources.items[0].text == "/ExtGState" &&
                resources.items[1].items[1].items[1].text == "0.5 ") &&
               "its opacity a graphics state of the page's");
    }

    // --- Text ---------------------------------------------------------------------------------
    {
        const auto drawn = read(R"svg(<svg width="200" height="50"><text x="100" y="40" font-size="20" font-family="serif"
            text-anchor="middle">A <tspan font-weight="bold">bold</tspan> word &amp; more</text></svg>)svg");
        assert((drawn && drawn->content.contains("(A) Tj") && drawn->content.contains("( bold) Tj") &&
                drawn->content.contains("( word & more) Tj")) &&
               "its runs in their order, one space between each, its entity read");
        assert((drawn->content.find("(A) Tj") < drawn->content.find("( bold) Tj")) && "the span after the words before it");
        const graphics::Drawing::Value& fonts = drawn->resources.items.at(1);
        assert((fonts.items[1].items[5].text == "/Times-Roman" && fonts.items[3].items[5].text == "/Times-Bold") &&
               "set in the standard faces nearest its family and weight");
    }

    // --- What is refused --------------------------------------------------------------------
    assert((!read("<html><body>no picture</body></html>")) && "a file with no <svg>");
    return 0;
}
