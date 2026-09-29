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
                Reference    ///< An indirect object: #number, one of objects().
            };

            Type type{Type::Null};         ///< What it is.
            std::string text{};            ///< For Plain, Name and Text: as written.
            std::vector<Value> items{};    ///< For List and Table.
            std::uint32_t number{0};       ///< For Reference: its index in objects().
        };

        /// @brief One object the page reaches.
        struct Object {
            Value value{};            ///< Its value; a stream's dictionary, its length left out.
            std::string stream{};     ///< A stream's bytes, as the file stored them.
            bool streamed{false};     ///< Whether it is a stream.
        };

        /// @brief Reads a page of a PDF.
        /// @param bytes The whole file.
        /// @param page  Which page, from 1.
        /// @return The page, or std::nullopt when the bytes are no PDF this
        ///         can read, or it has no such page.
        /// @complexity O(n) in the objects the page reaches, and linear in
        ///             the file's length when its cross-reference is broken.
        [[nodiscard]] static std::optional<Drawing> decode(std::span<const std::byte> bytes, int page = 1);

        /// @brief The page's box, in its own points: left, bottom, right, top.
        [[nodiscard]] const std::array<float, 4>& box() const noexcept { return box_; }

        /// @brief How wide the page is, in points.
        [[nodiscard]] float width() const noexcept { return box_[2] - box_[0]; }

        /// @brief How tall the page is, in points.
        [[nodiscard]] float height() const noexcept { return box_[3] - box_[1]; }

        /// @brief What the page draws: its contents, decompressed and joined.
        [[nodiscard]] const std::string& content() const noexcept { return content_; }

        /// @brief The page's resources: a Table, or a Reference to one, its
        ///        references to objects().
        [[nodiscard]] const Value& resources() const noexcept { return resources_; }

        /// @brief The page's transparency group, or a Null value for none.
        [[nodiscard]] const Value& group() const noexcept { return group_; }

        /// @brief Every object the resources and the group reach.
        [[nodiscard]] const std::vector<Object>& objects() const noexcept { return objects_; }

    private:
        std::array<float, 4> box_{0.0f, 0.0f, 0.0f, 0.0f};   ///< Left, bottom, right, top.
        std::string content_{};                                  ///< The contents, decompressed.
        Value resources_{};                                      ///< The resources, renumbered.
        Value group_{};                                          ///< The group, renumbered.
        std::vector<Object> objects_{};                          ///< What they reach.
    };

}
