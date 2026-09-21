/*#include "render/primitives/wrapper.hpp"

namespace render::primitives {

    void Wrapper::ingest(
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
    ) {
        configuration::document(parser.mouth(), document, registers);
        configuration::font(parser.mouth(), registers, registry, configuration, arena, selection);
        structure::boxes(parser, registers, shaper, typesetter, selection);
        structure::alignment(parser, shaper, typesetter, selection);
        sections.ingest(parser, shaper, selection, references);
        lists.ingest(parser, environments, shaper, typesetter, selection);
        structure::glue(parser, registers);
        structure::rules(parser, registers);
        structure::penalties(parser, registers);
        expression::ingest(parser, unicodes);
    }

}*/