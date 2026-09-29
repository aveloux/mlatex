/// @file
/// @brief Canvas implementation: strokes collected, then laid into one box.
#include "render/graphics/canvas.hpp"

#include <algorithm>

namespace render::graphics {

    void Canvas::line(
        const Point2 from, const Point2 to,
        const Color color, const float width, const bool dashed
    ) {
        segments.push_back({from, to, color, width, dashed});
    }

    void Canvas::line(
        const Point3& from, const Point3& to, const Projection& view,
        const Color color, const float width, const bool dashed
    ) {
        segments.push_back({view.project(from), view.project(to), color, width, dashed});
    }

    void Canvas::place(const Point2 at, layout::Node* box, const int across, const int up, const float gap) {
        if (!box) return;
        const layout::Node::Box& shape = box->box();
        const float width = shape.width;
        const float tall = shape.height + shape.depth;

        // The corner that lands on the point, and the gap kept from it.
        const float left = across < 0 ? at.x + gap : across > 0 ? at.x - gap - width : at.x - width * 0.5f;
        const float bottom = up < 0 ? at.y + gap : up > 0 ? at.y - gap - tall : at.y - tall * 0.5f;
        labels.push_back(Label{.corner = {left, bottom}, .box = box});
    }

    void Canvas::fill(const std::span<const Point2> corners, const Color color) {
        if (corners.size() < 3) return;
        regions.push_back(Region{.corners = std::vector<Point2>(corners.begin(), corners.end()), .color = color});
    }

    layout::Node* Canvas::compose(memory::Arena& arena, const Room room) const {
        // The area the box takes, in the canvas's own coordinates, y up: the
        // declared one, or none at all when the drawing is to be its own size.
        float left = 0.0f;
        float right = room == Room::Drawn ? 0.0f : width_;
        float bottom = 0.0f;
        float top = room == Room::Drawn ? 0.0f : height_;

        // Everything drawn, taken in unless the canvas is pinned where it was
        // declared: a projected drawing lands wherever its projection puts it,
        // and a picture that did not make room for it would lay it over the
        // text around it.
        if (room != Room::Declared && (!segments.empty() || !labels.empty() || !regions.empty())) {
            bool first = room == Room::Drawn;
            const auto take = [&](const Point2 point) {
                if (first) {
                    left = right = point.x;
                    bottom = top = point.y;
                    first = false;
                    return;
                }
                left = std::min(left, point.x);
                right = std::max(right, point.x);
                bottom = std::min(bottom, point.y);
                top = std::max(top, point.y);
            };
            for (const Segment& segment : segments) {
                take(segment.from);
                take(segment.to);
            }
            for (const Region& region : regions) {
                for (const Point2 corner : region.corners) take(corner);
            }
            for (const Label& label : labels) {
                const layout::Node::Box& shape = label.box->box();
                take(label.corner);
                take({label.corner.x + shape.width, label.corner.y + shape.height + shape.depth});
            }
        }

        // The regions first, so every line is drawn over every fill.
        const memory::Slice<layout::Node*> strokes =
            arena.allocate<layout::Node*>(regions.size() + segments.size() + labels.size());
        std::size_t filled = 0;
        for (const Region& region : regions) {
            const memory::Slice<layout::Node::Point> area = arena.allocate<layout::Node::Point>(region.corners.size());
            for (std::size_t index = 0; index < region.corners.size(); ++index) {
                area[index] = {region.corners[index].x - left, top - region.corners[index].y};
            }
            auto* shape = arena.compose<layout::Node>();
            shape->path({
                .color = {region.color.r, region.color.g, region.color.b, region.color.alpha},
                .area = area,
            });
            strokes[filled++] = shape;
        }
        for (std::size_t index = 0; index < segments.size(); ++index) {
            const Segment& segment = segments[index];
            auto* stroke = arena.compose<layout::Node>();

            // A canvas's own y runs up the way graph paper does; the box it
            // sits in hangs from its top edge the way any other block here
            // does, so a point's offset from that edge is the distance down
            // from the area's top, and across from its left.
            stroke->path({
                .start = {segment.from.x - left, top - segment.from.y},
                .end = {segment.to.x - left, top - segment.to.y},
                .width = segment.width,
                .dashed = segment.dashed,
                .color = {segment.color.r, segment.color.g, segment.color.b, segment.color.alpha},
            });
            strokes[filled + index] = stroke;
        }

        // Each placed box moved to its corner: across from the area's left,
        // and its baseline down from the area's top -- the box's reference
        // point is its baseline, which stands its depth above its bottom.
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const Label& label = labels[index];
            layout::Node::Box shape = label.box->box();
            shape.offset = label.corner.x - left;
            shape.shift = top - (label.corner.y + shape.depth);
            label.box->box(shape);
            strokes[filled + segments.size() + index] = label.box;
        }

        auto* canvas = arena.compose<layout::Node>();
        canvas->box({
            .width = right - left,
            .height = 0.0f,
            .depth = top - bottom,
            .alignment = layout::Node::Alignment::Horizontal,
            .list = strokes,
            .absolute = true,
        });
        return canvas;
    }

}
