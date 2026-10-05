#pragma once

#include "latex.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#if defined(__EMSCRIPTEN__)
    #include <emscripten/val.h>
#endif

namespace render {

    /// @brief The engine compiled to WebAssembly, for a web page, a worker or
    ///        Node: latex::Session, bound for JavaScript under its own names.
    ///
    /// Built by the `wasm` target when CMake runs under Emscripten; see the
    /// top-level CMakeLists.txt. The build leaves `latex.js`, `latex.wasm`
    /// and `latex.data` side by side -- the last holding the fonts, packed
    /// into the module's own file system at #root -- and `latex.js` is a
    /// factory that loads the other two:
    ///
    /// @code
    /// const engine = await Latex();                    // latex.js's factory
    /// const session = new engine.Session();
    ///
    /// session.set("customer", "Acme Ltd.");
    /// session.define("balance", 1, (account) => ledger[account].toFixed(2));
    /// session.provide("chart.png", new Uint8Array(await chart.arrayBuffer()));
    ///
    /// if (!session.typeset(source)) console.error(session.error());
    /// const pdf = session.pdf();                         // a Uint8Array of its own
    /// session.delete();                                  // as every bound object is
    /// @endcode
    ///
    /// Every operation is latex::Session's, under the same name, and does
    /// the same thing: `set`/`unset`, `define`/`forget`, `provide`/`withdraw`,
    /// `typeset`, `pdf` and `error`. Only what JavaScript hands over differs:
    /// a command is a JavaScript function, called with the arguments' text
    /// and turned back into text with `String()`; a file is a Uint8Array or a
    /// string; and the PDF comes back as a Uint8Array that owns its bytes,
    /// so it outlives the next document.
    ///
    /// @par Limits
    /// A document runs on the thread that asked for it, and the engine never
    /// starts one of its own here, so the module needs no shared memory and
    /// runs anywhere WebAssembly does. `\\includegraphics` of a URL is not
    /// fetched from inside the module; the page fetches it and hands the
    /// bytes in with `provide`, under the name the document uses.
    class Wasm : public ::latex::Session {
    public:
        /// Where the fonts are in the module's own file system: packed there
        /// when the module was built, and read from there like any disk.
        static constexpr std::string_view root = "/assets";

        /// @brief A session over the fonts packed into the module.
        Wasm();

    #if defined(__EMSCRIPTEN__)
        /// @brief The last PDF made, copied into a Uint8Array of its own;
        ///        empty when none could be made.
        [[nodiscard]] emscripten::val bytes() const;

        /// @brief Makes a JavaScript function a command the document can call.
        /// @param name     The command's name, with or without its backslash.
        /// @param arity    How many arguments it reads, from 0 to 9.
        /// @param function Called with each argument's text; what it returns
        ///                 is read in the command's place, through `String()`.
        void attach(const std::string& name, std::size_t arity, emscripten::val function);
    #endif
    };

}
