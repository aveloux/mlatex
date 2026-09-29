#pragma once

#include <optional>
#include <string_view>

namespace syntax::modules {

    /// @brief Finds an embedded prelude file's text by name.
    ///
    /// Every `.mtex` file under `src/modules` is compiled into the binary with
    /// `#embed`, and found by name through a hash table the compiler builds
    /// (memory::Catalog). The table itself -- and the bytes of
    /// every file it holds -- lives in exactly one translation unit
    /// (`src/syntax/modules.cpp`); this is the declaration the rest of the
    /// engine calls it through, so that including it costs a lookup and not a
    /// copy of the whole embedded prelude into every file that asks for one.
    ///
    /// @param name The file's path below `src/modules`, with forward
    ///             slashes: `main.mtex`, `core/titling.mtex`,
    ///             `amsthm/main.mtex`.
    /// @return Its text, or std::nullopt when no such file is embedded.
    /// @complexity O(1) average.
    [[nodiscard]] std::optional<std::string_view> find(std::string_view name) noexcept;

}
