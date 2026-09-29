/// @file
/// @brief The one translation unit that embeds the prelude's bytes.
///
/// modules.inc is written by CMakeLists.txt from every file under src/modules:
/// one `#embed` per file, and a memory::Catalog over their names. It is
/// included here and nowhere else -- anything else that needs find() takes
/// syntax/modules.hpp instead, which declares it alone and costs nothing to
/// include.
#include "syntax/modules.hpp"
#include "memory/catalog.hpp"

namespace syntax::modules {

#include "modules.inc"

    std::optional<std::string_view> get(const std::string_view name) noexcept {
        return catalog.get(name);
    }

}
