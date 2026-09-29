/// @file
/// @brief The engine bound for JavaScript: see render/wasm.hpp.
///
/// Compiled into the `wasm` target alone, under Emscripten. Embind does the
/// binding: engine::Session's operations are handed to JavaScript under their
/// own names, each through a small adapter where JavaScript's types differ
/// from C++'s -- embind has no string view, reads a Uint8Array or a string
/// into a std::string, and would read a PDF's bytes back as UTF-8 text if
/// they went out as one.
#include "render/wasm.hpp"

#include <string>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
    #include <emscripten/bind.h>
#endif

namespace render {

    Wasm::Wasm() : ::engine::Session(std::string(root)) {}

#if defined(__EMSCRIPTEN__)

    emscripten::val Wasm::bytes() const {
        const std::string& made = pdf();
        const auto* data = reinterpret_cast<const unsigned char*>(made.data());

        // A view onto the module's own memory would be invalidated by the
        // next document, or by the memory growing; slice() copies it into a
        // Uint8Array of its own.
        return emscripten::val(emscripten::typed_memory_view(made.size(), data)).call<emscripten::val>("slice");
    }

    void Wasm::attach(const std::string& name, const std::size_t arity, emscripten::val function) {
        define({
            .name = name,
            .arity = arity,
            .handler = [function = std::move(function)](const std::span<const std::string> arguments) {
                emscripten::val list = emscripten::val::array();
                for (const std::string& argument : arguments) list.call<void>("push", argument);

                const emscripten::val answer = function.call<emscripten::val>("apply", emscripten::val::null(), list);
                if (answer.isUndefined() || answer.isNull()) return std::string{};
                return emscripten::val::global("String")(answer).as<std::string>();
            },
        });
    }

    /// @brief Hands engine::Session to JavaScript under its own names.
    EMSCRIPTEN_BINDINGS(latex) {
        emscripten::class_<Wasm>("Session")
            .constructor<>()
            .function("set", emscripten::optional_override([](Wasm& self, const std::string& name,
                                                              const std::string& value) { self.set(name, value); }))
            .function("unset", emscripten::optional_override(
                                   [](Wasm& self, const std::string& name) { self.unset(name); }))
            .function("define", &Wasm::attach)
            .function("forget", emscripten::optional_override(
                                    [](Wasm& self, const std::string& name) { self.forget(name); }))
            .function("provide", emscripten::optional_override([](Wasm& self, const std::string& name,
                                                                  std::string bytes) {
                self.provide(name, std::move(bytes));
            }))
            .function("withdraw", emscripten::optional_override(
                                      [](Wasm& self, const std::string& name) { self.withdraw(name); }))
            .function("typeset", emscripten::optional_override(
                                     [](Wasm& self, const std::string& document) { return self.typeset(document); }))
            .function("pdf", &Wasm::bytes)
            .function("error", emscripten::optional_override(
                                   [](const Wasm& self) { return std::string(self.error()); }));
    }

#endif

}
