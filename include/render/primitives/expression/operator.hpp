#pragma once

#include "syntax/expression/parser.hpp"
#include "syntax/lexicon.hpp"

namespace render::primitives::expression {
    void rules(syntax::expression::Parser& parser, syntax::Lexicon& lexicon);
}