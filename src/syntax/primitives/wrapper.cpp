/// @file
/// @brief Installs the core primitive set and gathers what it reported.
///
/// The bind() calls are independent -- a later module may rebind a name an
/// earlier one took, and the last one installed wins.
#include "syntax/primitives/wrapper.hpp"
#include "logger.hpp"

#include <cstddef>

namespace syntax::primitives {

    Wrapper::Wrapper(Lexicon& names) noexcept
        : relay(names), blocks(names), macros(names), values(names),
          compute(names), loops(names) {}

    void Wrapper::operator()(Mouth& mouth, Context& context) const {
        // One fold over every module. Adding a primitive means adding a member
        // and a name here; nothing else changes.
        bind(mouth, context, relay, blocks, macros, values, compute, loops);

        Logger::log(Logger::Type::Semantics, Logger::Level::Informative,
                    "Core primitives installed");
    }

    std::vector<Traceback> Wrapper::tracebacks() const {
        // Every module, conditionals included: relay keeps its own list like
        // the rest, so leaving it out here lost runaway and unclosed-\if
        // reports even though the module had recorded them.
        const std::vector<Traceback>* lists[] = {
            &relay.tracebacks(), &blocks.tracebacks(), &macros.tracebacks(),
            &values.tracebacks(), &compute.tracebacks(), &loops.tracebacks(),
        };

        std::size_t total = 0uz;
        for (const auto* list : lists) total += list->size();

        std::vector<Traceback> gathered;
        gathered.reserve(total);
        for (const auto* list : lists) {
            gathered.insert(gathered.end(), list->begin(), list->end());
        }
        return gathered;
    }

    bool Wrapper::failed() const noexcept {
        return !relay.tracebacks().empty() || !blocks.tracebacks().empty() ||
               !macros.tracebacks().empty() || !values.tracebacks().empty() ||
               !compute.tracebacks().empty() || !loops.tracebacks().empty();
    }

}