#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace syntax::semantics {

    class Scope {
    public:
        // Syntax types are static AST nodes; Scope types are runtime stack frames.
        // They isolate local macro, register, and catcode changes inside boundaries.
        // Exiting unwinds the undo log to automatically restore engine state.
        enum class Type : std::uint8_t {
            Group,       ///< Scope group block ({...} or \\begingroup...\\endgroup)
            Environment, ///< Formal environment structure (\\begin...\\end)
            Equations,   ///< Mathematical inline/display formulas ($...$, $$...$$, \\(...\\), \\[...\\])
            Box,         ///< Layout framing box container (\\hbox, \\vbox, \\vtop)
            Conditional, ///< Conditional branch stack (\\if, \\ifx, \\else, \\fi)
            Alignment    ///< Grid/Table cell alignment scope (\\halign, \\valign, tabular)
        };

        struct Layer {
            Type type = Type::Group;
            std::size_t level = 0;
        };

        void push(Type type = Type::Group);
        void pop();
        [[nodiscard]] std::size_t depth() const noexcept;
        [[nodiscard]] Type type() const noexcept;
        void reset() noexcept;

        [[nodiscard]] std::optional<Layer> top() const noexcept;

        [[nodiscard]] static std::string_view name(Type type) noexcept;

    private:
        std::vector<Layer> stack;
    };

}