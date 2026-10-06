#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace render::graphics {

    /// @brief One page of another PDF, kept to be drawn as a form: its box,
    ///        what it draws, and every object what it draws reaches.
    ///
    /// A figure made by a plotting program, a drawing program or another
    /// LaTeX run arrives as a PDF. It is drawn as pdfTeX draws one: its page
    /// set whole into this document as a Form XObject, its fonts, images and
    /// patterns carried with it, nothing rasterised and nothing redrawn.
    ///
    /// decode() reads the file the way a PDF reader does -- the
    /// cross-reference table or stream the file ends with, and each before
    /// it, compressed object streams included; the whole file scanned for
    /// its objects when that table is broken -- finds the page asked for in
    /// the page tree, takes its crop box (its media box when it has none),
    /// its contents decompressed and joined, and its resources. Every object
    /// those resources reach is copied, and numbered from 0 in the order it
    /// was reached, so the writer only renumbers them into its own file.
    ///
    /// A picture drawn as PostScript or SVG arrives the same way: an EPS
    /// file's program is run (script()) and an SVG file's elements read
    /// (markup()), each into the content of a page as large as its picture,
    /// the same operators a PDF's page would hold -- paths, fills, strokes,
    /// clips, colours -- its text set in PDF's standard faces.
    ///
    /// @par What it does not read
    /// An encrypted file, and contents compressed other than with Flate; a
    /// page's rotation is not applied.
    class Drawing {
    public:
        /// @brief A PDF value, as the file wrote it.
        struct Value {
            /// @brief What kind of value.
            enum class Type : std::uint8_t {
                Null,        ///< `null`, or what could not be read.
                Plain,       ///< A number or a boolean, #text as written.
                Name,        ///< `/Name`, #text with its slash.
                Text,        ///< A string, #text with its delimiters: `(...)` or `<...>`.
                List,        ///< An array: #items.
                Table,       ///< A dictionary: #items, keys and values in turn.
                Reference    ///< An indirect object: #number, one of Drawing::objects.
            };

            Type type{Type::Null};         ///< What it is.
            std::string text{};            ///< For Plain, Name and Text: as written.
            std::vector<Value> items{};    ///< For List and Table.
            std::uint32_t number{0};       ///< For Reference: its index in Drawing::objects.
        };

        /// @brief One object the page reaches.
        struct Object {
            Value value{};            ///< Its value; a stream's dictionary, its length left out.
            std::string stream{};     ///< A stream's bytes, as the file stored them.
            bool streamed{false};     ///< Whether it is a stream.
        };

        /// @brief Reads a page of a PDF, or an EPS or SVG file's picture.
        /// @param bytes The whole file.
        /// @param page  Which page, from 1; a picture has only the one.
        /// @return The page, or std::nullopt when the bytes are no PDF, EPS
        ///         or SVG this can read, or it has no such page.
        /// @complexity O(n) in the objects the page reaches, and linear in
        ///             the file's length when its cross-reference is broken;
        ///             a picture's, in what its program does or its elements.
        [[nodiscard]] static std::optional<Drawing> decode(std::span<const std::byte> bytes, int page = 1);

        /// @brief How wide the page is, in points.
        [[nodiscard]] float width() const noexcept { return box[2] - box[0]; }

        /// @brief How tall the page is, in points.
        [[nodiscard]] float height() const noexcept { return box[3] - box[1]; }

        std::array<float, 4> box{0.0f, 0.0f, 0.0f, 0.0f};   ///< The page's box, in its own points: left, bottom, right, top.
        std::string content{};                                ///< What the page draws: its contents, decompressed and joined.
        Value resources{};     ///< The page's resources, renumbered: a Table, or a Reference to one, into #objects.
        Value group{};         ///< The page's transparency group, renumbered, or a Null value for none.
        std::vector<Object> objects{};                        ///< Every object the resources and the group reach.

    private:
        /// @brief An Encapsulated PostScript file's picture: its program run.
        ///
        /// The box is its `%%BoundingBox` comment's. The program runs as a
        /// PostScript interpreter runs it -- its stack, its dictionaries, its
        /// procedures, loops and conditions -- and what it paints is written
        /// as PDF's operators in the page's own points: every path already
        /// through the transformation it was built under, so a `gsave scale
        /// arc grestore stroke` comes out as PostScript draws it. Text is set
        /// in the standard face its font's name is nearest; an image whose
        /// samples follow it in the file is drawn inline. A program that
        /// runs too long is stopped where it stands, and what it drew kept.
        /// @param file The file's text, from its `%!PS`.
        /// @return The picture, or std::nullopt with no bounding box.
        /// @complexity O(n) in what the program does, a few million steps at most.
        [[nodiscard]] static std::optional<Drawing> script(std::string_view file);

        /// @brief An SVG file's picture: its elements read.
        ///
        /// The page is the picture's `width` and `height` -- a pixel three
        /// quarters of a point, as Inkscape exports one -- or its `viewBox`'s.
        /// Rectangles, circles, ellipses, lines, polylines, polygons and paths
        /// -- every command of `d`, arcs and smooth curves included -- are
        /// filled and stroked as their `fill`, `stroke` and `stroke-width`
        /// say, inherited from the groups around them, through each group's
        /// and each element's `transform`; `<use>` draws what it names again,
        /// and text is set in the standard face its family is nearest.
        /// @param file The file's text.
        /// @return The picture, or std::nullopt when it holds no `<svg>`.
        /// @complexity O(n) in the file's length.
        [[nodiscard]] static std::optional<Drawing> markup(std::string_view file);

        /// @brief How wide letters stand in one of PDF's standard faces.
        /// @param face    The face: `Helvetica-Bold`, `Times-Roman`, `Courier`.
        /// @param letters The letters, one byte each.
        /// @return Their width, in thousandths of the size they are set at.
        /// @complexity O(n) in the letters.
        [[nodiscard]] static double breadth(std::string_view face, std::string_view letters) noexcept;

        /// @brief The resources a picture's text needs: each standard face it
        ///        is set in, named `/F0`, `/F1` and on in the order given.
        /// @param names The faces' names.
        /// @return A Table holding `/Font`, or a Null value when there are none.
        /// @complexity O(n) in the faces.
        [[nodiscard]] static Value faces(const std::vector<std::string>& names);
    };

}
