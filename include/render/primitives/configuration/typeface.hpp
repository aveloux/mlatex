#pragma once

#include "memory/arena.hpp"
#include "syntax/mouth.hpp"
#include "typography/fontconfig.hpp"
#include "typography/registry.hpp"

namespace render::primitives::configuration {

    class Selection {
    public:
        [[nodiscard]] const typography::Font* font() const noexcept;
        void font(const typography::Font* value) noexcept;

    private:
        const typography::Font* current{nullptr};
    };

    void font(
        syntax::Mouth& mouth,
        syntax::semantics::Registers& registers,
        typography::Registry& registry,
        typography::FontConfig& options,
        memory::Arena& arena,
        Selection& selection
    );

}