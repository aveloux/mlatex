#pragma once

#include "syntax/expression/unicodes.hpp"
#include "syntax/parser.hpp"

namespace render::primitives::expression {
    void ingest(syntax::Parser& parser, const syntax::expression::Unicodes& unicodes);
}