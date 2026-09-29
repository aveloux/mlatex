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

// #embed is C++26's, and Clang still calls it an extension of its own there.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif

#include "modules.inc"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    std::optional<std::string_view> find(const std::string_view name) noexcept {
        return catalog.find(name);
    }

}
