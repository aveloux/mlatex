#pragma once

#include "syntax/catcodes.hpp"
#include "syntax/semantics/registers.hpp"

#include <vector>

namespace syntax::semantics {

    /// @brief Every piece of state a group saves and restores.
    ///
    /// Opening a group opens it on the category table, on the registers and
    /// on the current fonts together, and closing it closes all three, so no
    /// module has to restore anything by hand.
    ///
    /// @par Use
    /// @code
    /// state.push();
    /// state.fonts.text = bold;         // local to the group
    /// state.pop();                     // the previous face is back
    /// @endcode
    class Union {
    public:
        /// @brief The faces text and formulas are set in.
        ///
        /// Opaque here: the syntax layer only carries them, and the render
        /// layer, which owns fonts, is the only one that looks inside.
        struct Fonts {
            const void* text{nullptr};      ///< Face for running text.
            const void* formula{nullptr};   ///< Face for formulas.
        };

        /// @brief Opens a group.
        void push() {
            catcodes.push();
            registers.push();
            saved.push_back(fonts);
        }

        /// @brief Closes a group.
        void pop() {
            catcodes.pop();
            registers.pop();
            if (saved.empty()) return;
            fonts = saved.back();
            saved.pop_back();
        }

        Catcodes catcodes{};             ///< Category table.
        Registers registers{};           ///< Register banks.
        Fonts fonts{};                   ///< Current faces.

    private:
        std::vector<Fonts> saved{};      ///< Faces to restore, one per open group.
    };

}
