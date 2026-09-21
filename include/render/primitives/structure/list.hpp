/*#pragma once

#include "render/primitives/configuration/typeface.hpp"
#include "render/layout/typesetter.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/scaffold/environment.hpp"
#include "typography/shaper.hpp"

#include <vector>

namespace render::primitives::structure {

    class Lists {
    public:
        void ingest(
            syntax::Parser& parser,
            syntax::primitives::scaffold::Environments& environments,
            const typography::Shaper& shaper,
            const layout::Typesetter& typesetter,
            const configuration::Selection& selection
        );

    private:
        enum class Type { Bullet, Number, Description };

        struct Level {
            Type type;
            int index;
        };

        std::vector<Level> levels{};
    };

}*/
#pragma once
