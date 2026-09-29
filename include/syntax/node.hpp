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
        std::string_view value{};                      ///< Its literal text, if any.
        memory::Location location{};                   ///< Where it came from.
        memory::Slice<Node*> nodes{};                  ///< Child nodes, if any.
        const expression::Node* expression{nullptr};   ///< Set for Type::Expression.
        bool display = false;                          ///< `\\[...\\]` rather than `\\(...\\)`.
        void* directive{nullptr};                      ///< Set for Type::Directive; opaque here.

        /// @brief The face this text is to be set in, or null for whichever is
        ///        in use when it is composed.
        ///
        /// Opaque, and deliberately so: the syntax layer has no business
        /// knowing what a face is. It is stamped as the text is read --
        /// Parser::stamp -- because a style is chosen while the document is
        /// being read and the text is not shaped until long afterwards, by
        /// which time the choice would otherwise be gone.
        const void* face{nullptr};
        /// @brief The color this text is to be set in, or null for the
        ///        default; opaque here for the same reason #face is.
        const void* tint{nullptr};

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