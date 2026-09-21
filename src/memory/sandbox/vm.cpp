/// @file
/// @brief One self-contained run of the language.
#include "memory/sandbox/vm.hpp"
#include "logger.hpp"

#include <format>
#include <fstream>
#include <string>

namespace sandbox {

    VM::VM(const Policy& rules, const std::size_t limit)
        : policy(rules),
          memory(limit),
          arena(limit),
          state{},
          lexicon(arena),
          wrapper(lexicon),
          // Context holds references, so both targets must already exist. The
          // register bank comes from `state`: Union owns the one the scoping
          // machinery unwinds, and a second bank would silently not be scoped.
          context{policy, state.registers(), wrapper.conditionals()},
          mouth(syntax::Cursor{}, state, lexicon, arena) {
        wrapper(mouth, context);
    }

    bool VM::eval(const std::string_view code) {
        faults.clear();
        mouth.ingest(code);

        // Drive the expander to exhaustion. Everything the document produces
        // is handled by a primitive or passes through as text; this loop is
        // what makes those primitives actually run.
        while (true) {
            const syntax::Token token = mouth.expand();

            if (mouth.failed()) {
                break;
            }
            if (token.empty()) {
                break;
            }

            if (++served > policy.tokens) {
                faults.emplace_back(syntax::Traceback::Type::Memory, token.location,
                                    std::format("run consumed more than {} tokens", policy.tokens));
                break;
            }

            if (state.scope().depth() > policy.depth) {
                faults.emplace_back(syntax::Traceback::Type::Scope, token.location,
                                    std::format("scope nested deeper than {}", policy.depth));
                break;
            }
        }

        return !failed();
    }

    bool VM::run(const std::string_view path) {
        if (!policy.read) {
            faults.emplace_back(syntax::Traceback::Type::Primitive, memory::Location{},
                                "policy denies reading files");
            Logger::log(Logger::Type::Semantics, Logger::Level::Error,
                        "policy denies reading files");
            return false;
        }

        std::ifstream file{std::string(path), std::ios::binary | std::ios::ate};
        if (!file) {
            faults.emplace_back(syntax::Traceback::Type::Primitive, memory::Location{},
                                std::format("cannot open {}", path));
            return false;
        }

        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string content(static_cast<std::size_t>(size), '\0');
        if (size > 0 && !file.read(content.data(), size)) {
            faults.emplace_back(syntax::Traceback::Type::Primitive, memory::Location{},
                                std::format("cannot read {}", path));
            return false;
        }

        return eval(content);
    }

    std::vector<syntax::Traceback> VM::tracebacks() const {
        std::vector<syntax::Traceback> gathered = wrapper.tracebacks();

        const auto& expansion = mouth.history();
        gathered.insert(gathered.end(), expansion.begin(), expansion.end());
        gathered.insert(gathered.end(), faults.begin(), faults.end());

        return gathered;
    }

    bool VM::failed() const noexcept {
        return wrapper.failed() || mouth.failed() || !mouth.history().empty() || !faults.empty();
    }

}