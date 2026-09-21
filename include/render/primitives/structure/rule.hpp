#pragma once

#include "syntax/parser.hpp"

namespace render::primitives::structure {
    void rules(syntax::Parser& parser, syntax::semantics::Registers& registers);
}