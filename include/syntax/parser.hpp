#pragma once

#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/traceback.hpp"
#include "memory/arena.hpp"
#include "memory/slice.hpp"

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <string_view>
#include <vector>

namespace syntax {

    class Parser {
    public:
        using Handler = std::function<Node*(Parser&)>;

        Parser(Mouth& mouth, memory::Arena& arena);

        [[nodiscard]] Token step() const;
        [[nodiscard]] memory::Slice<Node*> parse();
        [[nodiscard]] memory::Slice<Node*> parse(char closing);

        void bind(std::string_view name, Handler handler);
        void bind(Symbol symbol, Handler handler);

        [[nodiscard]] Mouth& mouth() const noexcept { return mouth_; }
        [[nodiscard]] memory::Arena& arena() const noexcept { return arena_; }
        [[nodiscard]] const std::vector<Traceback>& tracebacks() const noexcept { return tracebacks_; }

        void report(std::ostream& stream) const;

        [[nodiscard]] bool failed() const noexcept;

        static constexpr std::size_t tolerance = 100;   ///< errors before giving up, as in TeX

    private:
        Mouth& mouth_;
        memory::Arena& arena_;
        std::vector<Handler> handlers{};
        std::vector<Traceback> tracebacks_{};
        Symbol paragraph = kInvalidSymbol;
        std::size_t nesting = 0;
    };

}