#pragma once

#include "syntax/catcodes.hpp"
#include "syntax/semantics/registers.hpp"
#include "syntax/semantics/scope.hpp"

namespace syntax::semantics {

    class Union {
    public:
        void push(const Scope::Type type = Scope::Type::Group) {
            scopes.push(type);
            registers_.push();
            catcodes_.push();
        }

        void pop() {
            scopes.pop();
            registers_.pop();
            catcodes_.pop();
        }

        [[nodiscard]] CatCodes& catcodes() noexcept { return catcodes_; }
        [[nodiscard]] const CatCodes& catcodes() const noexcept { return catcodes_; }

        [[nodiscard]] Registers& registers() noexcept { return registers_; }
        [[nodiscard]] const Registers& registers() const noexcept { return registers_; }

        [[nodiscard]] Scope& scope() noexcept { return scopes; }
        [[nodiscard]] const Scope& scope() const noexcept { return scopes; }

    private:
        CatCodes catcodes_{};
        Registers registers_{};
        Scope scopes{};
    };

}