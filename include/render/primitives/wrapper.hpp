/*#pragma once

#include "render/primitives/configuration/page.hpp"
#include "render/primitives/configuration/typeface.hpp"
#include "render/primitives/expression/entry.hpp"
#include "render/primitives/structure/alignment.hpp"
#include "render/primitives/structure/box.hpp"
#include "render/primitives/structure/glue.hpp"
#include "render/primitives/structure/list.hpp"
#include "render/primitives/structure/penalty.hpp"
#include "render/primitives/structure/rule.hpp"
#include "render/primitives/structure/section.hpp"
#include "layout/document.hpp"
#include "layout/typesetter.hpp"
#include "memory/arena.hpp"
#include "syntax/expression/unicodes.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/scaffold/environment.hpp"
#include "syntax/primitives/scaffold/reference.hpp"
#include "typography/fontconfig.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

namespace render::primitives {

    class Wrapper {
    public:
        void ingest(
            syntax::Parser& parser,
            layout::Document& document,
            syntax::semantics::Registers& registers,
            typography::Registry& registry,
            typography::FontConfig& configuration,
            memory::Arena& arena,
            const typography::Shaper& shaper,
            const layout::Typesetter& typesetter,
            const syntax::expression::Unicodes& unicodes,
            configuration::Selection& selection,
            syntax::primitives::scaffold::Environments& environments,
            syntax::primitives::scaffold::References& references
        );

    private:
        structure::Numbering sections{};
        structure::Lists lists{};
    };

}*/
#pragma once
