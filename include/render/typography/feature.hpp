#pragma once

#include <harfbuzz/hb.h>

#include <cstdint>
#include <string_view>

namespace render::typography {

    /// @brief One OpenType feature, turned on or off for a whole run.
    ///
    /// A feature is a rule inside the font: ligatures, old-style figures,
    /// small capitals. Every font decides which of them are on by default, and
    /// this is how a document overrules that decision.
    ///
    /// @par Use
    /// @code
    /// const typography::Feature features[] = {
    ///     typography::Feature("liga", 0),   // no ligatures in this run
    ///     typography::Feature("onum"),      // old-style figures in it
    /// };
    /// shaper.shape(fonts, text, memory::Slice{features, 2});
    /// @endcode
    struct Feature {
        hb_feature_t feature{};   ///< The tag and value, as the shaper wants them.

        constexpr Feature() noexcept = default;

        /// @brief Names a feature and says what to do with it.
        /// @param tag   Its four-letter name; a shorter one is padded, a
        ///              longer one is cut, because the format allows four.
        /// @param value 1 to turn it on, 0 to turn it off, or an index for a
        ///              feature that offers alternates.
        explicit constexpr Feature(const std::string_view tag,
                                   const std::uint32_t value = 1) noexcept {
            char letters[4] = {' ', ' ', ' ', ' '};
            for (std::size_t index = 0; index < tag.size() && index < 4; ++index) {
                letters[index] = tag[index];
            }

            feature.tag = HB_TAG(letters[0], letters[1], letters[2], letters[3]);
            feature.value = value;

            // The whole run, which is the only scope anything here asks for:
            // a feature that applies to part of a word is the font's business
            // and not the document's.
            feature.start = HB_FEATURE_GLOBAL_START;
            feature.end = HB_FEATURE_GLOBAL_END;
        }
    };

}
