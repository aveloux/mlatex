#pragma once

#include "layout/document.hpp"
#include "syntax/mouth.hpp"

namespace render::primitives::configuration {
    void document(syntax::Mouth& mouth, layout::Document& document_, syntax::semantics::Registers& registers);
}