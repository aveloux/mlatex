#pragma once

#include "render/graphics/color.hpp"
#include "render/graphics/point.hpp"
#include "render/graphics/projection.hpp"
#include "memory/arena.hpp"
#include "render/layout/node.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace render::graphics {

    /// @brief A sheet a document draws straight lines onto.
    ///
    /// A canvas is a coordinate system, not a widget: it keeps every line
    /// given to it until compose() turns them into the one box that holds
    /// them all, and nothing here is shared between two canvases. Coordinates are
    /// the canvas's own -- x right, y up, origin at its own bottom left
    /// corner -- read the way graph paper is, not the way the page itself
    /// runs.
    ///
    /// @par Language
    /// @code
    /// \begin{picture}{120pt}{80pt}
    ///     \linecolor{#FF0000}
    ///     \line{0}{0}{120}{80}
    ///     \linestyle{dashed}
    ///     \line{0}{80}{120}{0}
    /// \end{picture}
    /// @endcode
    class Canvas {
    public:
        /// @param width  The canvas's own width, in points.
        /// @param height The canvas's own height, in points.
        Canvas(const float width, const float height) noexcept : width_(width), height_(height) {}

        /// @brief Draws a straight line between two points already in 2D.
        /// @param from, to Endpoints, in the canvas's own coordinates.
        /// @param color    Stroke color.
        /// @param width    Stroke width, in points.
        /// @param dashed   False for a solid stroke, true for a dashed one.
        /// @complexity O(1) amortized.
        void line(Point2 from, Point2 to, Color color = black, float width = 1.0f, bool dashed = false);

        /// @brief Draws a straight line between two points in space,
        ///        flattened by the projection given.
        /// @param from, to Endpoints, in the projection's own space.
        /// @param view     How to flatten them onto the canvas.
        /// @param color    Stroke color.
        /// @param width    Stroke width, in points.
        /// @param dashed   False for a solid stroke, true for a dashed one.
        /// @complexity O(1) amortized.
        void line(
            const Point3& from,
            const Point3& to,
            const Projection& view,
            Color color = black,
            float width = 1.0f,
            bool dashed = false
        );

        /// @brief Places a box of set material -- a TikZ node's text -- on
        ///        the canvas.
        ///
        /// The box keeps its own shape; where it goes is given by the corner
        /// of it that lands on the point: its left edge's middle at the point
        /// for `anchor=west`, TikZ's `right`, its centre for no anchor at all.
        ///
        /// @param at     The point, in the canvas's own coordinates.
        /// @param box    The material, a horizontal box.
        /// @param across Which of its edges lands on the point across: -1 its
        ///               left, 0 its middle, 1 its right.
        /// @param up     And up and down: -1 its foot, 0 its middle, 1 its top.
        /// @param gap    Space kept between the point and the edge that faces it.
        /// @complexity O(1) amortized.
        void place(Point2 at, layout::Node* box, int across, int up, float gap);

        /// @brief Fills a region: the outline through its corners, closed.
        ///
        /// Every region is set behind every line, as TikZ sets a path's fill
        /// under its stroke.
        ///
        /// @param corners The outline, in the canvas's own coordinates.
        /// @param color   The fill.
        /// @complexity O(n) in the corners.
        void fill(std::span<const Point2> corners, Color color);

        /// @brief How much room a canvas takes where it is set.
        enum class Room : std::uint8_t {
            Declared,   ///< Its declared size, whatever is drawn past it: a page overlay, pinned in place.
            Grown,      ///< Its declared size, grown to take in anything drawn past it: a picture in the text.
            Drawn       ///< Exactly what is drawn: a TikZ picture, which declares no size of its own.
        };

        /// @brief Composes the box everything drawn so far sits inside.
        ///
        /// A drawing that stays inside its declared size is laid out exactly
        /// as declared. One that reaches past it -- a projection is the usual
        /// cause, since where a point in space lands is the projection's to
        /// say -- makes its box that much larger and is moved in by as much,
        /// so it pushes the text around it away instead of drawing over it.
        ///
        /// A picture set in the text stands on the baseline as a TikZ or a
        /// LaTeX picture does: its bottom on it, or the height a `baseline`
        /// option names.
        ///
        /// @param arena    Allocator for the box and every stroke inside it.
        /// @param room     How much room the box takes.
        /// @param baseline The height in the canvas's own coordinates that
        ///                 stands on the baseline; its bottom when none.
        /// @return The box; never null even when nothing was drawn.
        /// @complexity O(n) in the lines drawn.
        [[nodiscard]] layout::Node* compose(memory::Arena& arena, Room room,
                                            std::optional<float> baseline = std::nullopt) const;

    private:
        /// @brief One line, already in the canvas's own 2D space.
        struct Segment {
            Point2 from;     ///< One end.
            Point2 to;       ///< The other.
            Color color;     ///< Stroke color.
            float width;     ///< Stroke width, in points.
            bool dashed;     ///< Dashed rather than solid.
        };

        /// @brief One box placed on the canvas: where its bottom left corner
        ///        lands, and the box.
        struct Label {
            Point2 corner;         ///< Bottom left of the box's ink area, in the canvas's coordinates.
            layout::Node* box;     ///< What is set there.
        };

        /// @brief One region filled.
        struct Region {
            std::vector<Point2> corners;   ///< Its outline, in order.
            Color color;                   ///< Its fill.
        };

        float width_;
        float height_;
        std::vector<Segment> segments{};
        std::vector<Label> labels{};    ///< Every box placed, in order.
        std::vector<Region> regions{};  ///< Every region filled, in order.
    };

}
