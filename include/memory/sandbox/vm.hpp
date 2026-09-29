#pragma once

#include "memory/arena.hpp"
#include "memory/sandbox/allocator.hpp"
#include "memory/sandbox/policy.hpp"
#include "syntax/cursor.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/wrapper.hpp"
#include "syntax/semantics/union.hpp"
#include "syntax/traceback.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sandbox {

    /// @brief One self-contained run of the language.
    ///
    /// Owns every piece of engine state, installs the wrapper primitives, and
    /// exposes two ways in: evaluate() for a string, run() for a file.
    ///
    /// @par Order
    /// The member order below is load-bearing and must not be rearranged.
    /// `lexicon` needs `arena`; `wrapper` needs `lexicon`; `context` holds
    /// references into `state` and `wrapper`; `mouth` needs `state`, `lexicon`
    /// and `arena`. Declaring them in this order is what makes the
    /// constructor's initialiser list valid.
    ///
    /// @par Use
    /// @code
    /// sandbox::VM machine;
    /// if (!machine.evaluate("\\set\\integer0 = \\evaluate{6*7}")) {
    ///     for (const auto& fault : machine.tracebacks()) {
    ///         std::cerr << fault.format() << '\n';
    ///     }
    /// }
    /// @endcode
    class VM {
    public:
        static constexpr std::size_t ceiling = 64 * 1024 * 1024;   ///< Default byte budget.

        /// @brief Builds a machine and installs the wrapper primitives.
        /// @param rules How much the document is allowed to do.
        /// @param limit Byte budget for the allocator.
        explicit VM(const Policy& rules = {}, std::size_t limit = ceiling);
        ~VM() = default;

        VM(const VM&) = delete("a machine owns its expander, its parser and the arena both allocate from");
        VM& operator=(const VM&) = delete("a machine owns its expander, its parser and the arena both allocate from");
        VM(VM&&) noexcept = delete("a machine owns its expander, its parser and the arena both allocate from");
        VM& operator=(VM&&) noexcept = delete("a machine owns its expander, its parser and the arena both allocate from");

        /// @brief Runs a document held in memory.
        /// @param code Source text.
        /// @return True when the run produced no errors.
        [[nodiscard]] bool evaluate(std::string_view code);

        /// @brief Runs a document from a file.
        /// @param path File to read.
        /// @return True when the run produced no errors. Refused, with an
        ///         error recorded, when the policy denies reading.
        [[nodiscard]] bool run(std::string_view path);

        /// @brief The policy this machine was built with.
        [[nodiscard]] const Policy& rules() const noexcept { return policy; }

        /// @brief Every error from the last run: the expander's and the wrapper's.
        /// @complexity O(n) in the number of errors.
        [[nodiscard]] std::vector<syntax::Traceback> tracebacks() const;

        /// @brief Did the last run report anything?
        [[nodiscard]] bool error() const noexcept;

        /// @brief Tokens consumed so far, against Policy::tokens.
        [[nodiscard]] std::uint64_t consumed() const noexcept { return served; }

        /// @brief The expander, for callers that want to drive it directly.
        [[nodiscard]] syntax::Mouth& expander() noexcept { return mouth; }

    private:
        Policy policy;
        Allocator allocator;
        memory::Arena arena;
        syntax::semantics::Union state;
        syntax::Lexicon lexicon;
        syntax::primitives::Wrapper wrapper;
        syntax::primitives::Context context;
        syntax::Mouth mouth;
        std::uint64_t served{0};
        std::vector<syntax::Traceback> tracebacks_{};   ///< Errors this machine found.
    };

}