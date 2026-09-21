#pragma once

#include <cstddef>
#include <cstdint>

namespace sandbox {

    /// @brief What a document is allowed to do.
    ///
    /// Defaults deny everything interesting: a fresh Policy runs a document
    /// that can compute and define, but cannot touch the host.
    ///
    /// Kept in its own header so a driver that only needs to build a Context
    /// does not have to pull in the whole machine.
    struct Policy {
        bool shell{false};                ///< May a document run a shell command?
        bool write{false};                ///< May a document open an output stream?
        bool read{false};                 ///< May a document pull in another file?
        std::size_t depth{10'000};        ///< Deepest scope nesting allowed.
        std::uint64_t tokens{1'000'000};  ///< Most tokens one run may consume.
    };

}