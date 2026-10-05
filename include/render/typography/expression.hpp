#pragma once

#include "typography/font.hpp"

#include <cstdint>
#include <vector>

namespace render::typography {

    /// @brief The OpenType MATH table, read in points.
    ///
    /// A math font carries the numbers a formula has to be built from: where
    /// the axis sits, how far a numerator rises, how thick a fraction bar is.
    /// Guessing them produces formulas that are legible and wrong; reading
    /// them is what makes a fraction line up with the minus sign beside it.
    ///
    /// A font without a MATH table reports zeroes, and the typesetter falls
    /// back on its own proportions. That is the case for an ordinary text
    /// face, which is why a document wanting real formulas needs a math one.
    ///
    /// @par Use
    /// @code
    /// const typography::Expression math(font);
    /// const auto metrics = math.metrics();
    ///
    /// // Centre a minus sign on the same axis the fraction bar sits on:
    /// const float middle = metrics.axis;
    /// @endcode
    class Expression {
    public:
        /// @brief The constants a formula is laid out against, in points.
        struct Metric {
            float axis{0.0f};          ///< Height of the maths axis above the baseline.
            float numerator{0.0f};     ///< How far a display numerator rises.
            float denominator{0.0f};   ///< How far a display denominator drops.
            float ascent{0.0f};        ///< How far a numerator rises in a line.
            float descent{0.0f};       ///< How far a denominator drops in a line.
            float upper{0.0f};         ///< How far a binomial's top rises in a line.
            float separation{0.0f};    ///< Least room between a fraction's bar and its parts, in a line.
            float distance{0.0f};      ///< The same, in a displayed formula.
            float spacing{0.0f};       ///< Least room between a binomial's two parts, in a line.
            float spread{0.0f};        ///< The same, in a displayed formula.
            float thickness{0.0f};     ///< Fraction bar thickness.
            float gap{0.0f};           ///< Clearance above a radical's contents, in a line.
            float clearance{0.0f};     ///< The same, in a displayed formula.
            float rule{0.0f};          ///< Thickness of a radical's bar.
            float ascender{0.0f};      ///< Room left above a radical's bar.
            float before{0.0f};        ///< Space before a radical's degree.
            float after{0.0f};         ///< Space after it: negative, tucking the sign under it.
            float raise{0.0f};         ///< Height of the degree's baseline, as a fraction of the sign's.
            float subscript{0.0f};     ///< How far a subscript drops.
            float superscript{0.0f};   ///< How far a superscript rises.
            float script{0.0f};        ///< Script size, as a fraction of the base.
            float scriptscript{0.0f};  ///< Doubly-scripted size, likewise.
            float display{0.0f};       ///< Least height of a large operator in a displayed formula.
            float drop{0.0f};          ///< Most a superscript may sit below the top of a tall base.
            float sink{0.0f};          ///< Least a subscript sits below the bottom of a deep base.
            float limit{0.0f};         ///< Clearance between a large operator and its limits.
        };

        /// @brief A stretched delimiter: which glyph to draw, and how wide it got.
        struct Variant {
            std::uint32_t glyph{0};   ///< Glyph index to draw.
            float advance{0.0f};      ///< Its extent along the stretch axis, in points.
        };

        /// @brief One piece of a glyph built from parts.
        struct Piece {
            std::uint32_t glyph{0};   ///< Glyph index to draw.
            float lift{0.0f};         ///< How far its bottom stands above the whole glyph's, in points.
        };

        /// @brief A glyph built from parts, for a height no single size reaches.
        struct Assembly {
            std::vector<Piece> pieces{};   ///< Bottom first, extenders repeated as often as needed.
            float span{0.0f};              ///< Height of the whole, bottom to top, in points.
        };

        /// @brief Reads a font's math table.
        /// @param font Font to read; must outlive this object.
        explicit Expression(const Font& font) noexcept;

        /// @brief Does this font carry a MATH table at all?
        /// @complexity O(1).
        [[nodiscard]] bool present() const noexcept;

        /// @brief Every constant, in points.
        ///
        /// Read once and remembered, because a formula asks for these on every
        /// node and the table lookup behind each one is not free.
        ///
        /// @complexity O(1) after the first call.
        [[nodiscard]] Metric metrics() const noexcept;

        /// @brief The italic correction a glyph asks for, in points.
        ///
        /// The slope of a maths italic leaves its last stem leaning over the
        /// advance. Anything set tight against it -- a superscript above all --
        /// has to be pushed out by this much or it collides.
        ///
        /// @param glyph Glyph index.
        /// @complexity O(1).
        [[nodiscard]] float correction(std::uint32_t glyph) const noexcept;

        /// @brief The smallest vertical variant of a glyph that reaches a height.
        ///
        /// This is how a parenthesis grows around a fraction: the font ships
        /// the sizes, and the engine picks the first one tall enough.
        ///
        /// @param glyph  Glyph index of the delimiter at its base size.
        /// @param height Height to reach, in points.
        /// @return The variant to draw; the base glyph when none is tall enough.
        /// @complexity O(1) -- the candidate list is read once and is short.
        [[nodiscard]] Variant stretch(std::uint32_t glyph, float height) const noexcept;

        /// @brief The smallest horizontal variant of a glyph that reaches a width.
        ///
        /// This is how a wide hat or tilde spans what it covers: the font
        /// ships each mark in a run of widths, and the engine picks the first
        /// one wide enough.
        ///
        /// @param glyph Glyph index of the mark at its base size.
        /// @param width Width to reach, in points.
        /// @return The variant to draw, its advance its width; the widest when
        ///         none reaches, and the base glyph when the font has none.
        /// @complexity O(1) -- the candidate list is read once and is short.
        [[nodiscard]] Variant widen(std::uint32_t glyph, float width) const noexcept;

        /// @brief A glyph built from the font's parts to reach any height.
        ///
        /// What a radical or a parenthesis becomes once the tallest size the
        /// font draws whole is still too short: a bottom piece, a top piece,
        /// and an extender between them repeated until the whole reaches.
        /// The pieces overlap at their joints by at least the font's least
        /// overlap, and by more -- up to the shortest connector at any joint
        /// -- when that brings the whole nearer the height asked for, so it
        /// comes out as close to that height as the parts allow.
        ///
        /// @param glyph  Glyph index of the mark at its base size.
        /// @param height Height to reach, in points.
        /// @return The pieces, bottom first; none when the font builds no
        ///         such glyph from parts.
        /// @complexity O(n) in the pieces placed.
        [[nodiscard]] Assembly assemble(std::uint32_t glyph, float height) const;

    private:
        const Font& parent;          ///< The font this reads.
        mutable Metric cache{};      ///< Constants, filled on first request.
        mutable bool ready{false};   ///< False until #cache has been filled.
    };

}
