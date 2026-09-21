#pragma once

#include "memory/location.hpp"
#include "memory/slice.hpp"

#include <string_view>

namespace syntax::expression { struct Node; }

namespace syntax {

    struct Node {
        enum class Type {
            Text,        ///< Plain unformatted character stream
            Paragraph,   ///< Explicit paragraph break (\\par)
            Expression,  ///< Math block entry point (\\(...\\), \\[...\\], $...$)
            Group,       ///< Enclosed scope block ({...})
            Environment, ///< Generalized environment (\\begin{name}...\\end{name})
            Verbatim,    ///< Raw unformatted code container
            Macro,       ///< Macro expansion or directive (\\def, \\newcommand)
            Argument,    ///< Positional or optional argument payload ({...}, [...])
            Alignment,   ///< Table or grid cell/row alignment marker (& and the row break)
            Comment,     ///< Source code line comment (% ...)
            Directive    ///< Opaque engine-produced payload (boxes, glue, rules, penalties)
        };

        Type type = Type::Text;
        std::string_view value{};
        memory::Location location{};
        memory::Slice<Node*> nodes{};
        const expression::Node* expression{nullptr};
        bool display = false;   ///< \\[...\\] rather than \\(...\\)
        void* directive{nullptr};

        Node() = default;

        /// @brief Builds a node.
        /// @param kind     What the node is.
        /// @param text     Its literal text, if any.
        /// @param where    Source position.
        /// @param children Child nodes, if any.
        /// @param block    True for display math rather than inline.
        Node(const Type kind, const std::string_view text, const memory::Location where,
             const memory::Slice<Node*> children = {}, const bool block = false)
            : type(kind), value(text), location(where), nodes(children), display(block) {}
    };

}