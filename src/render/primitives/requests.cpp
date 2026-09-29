/// @file
/// @brief Requests implementation: `\\httpget`, `\\httppost`.
#include "render/primitives/requests.hpp"
#include "network/http.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <string>

namespace render::primitives {

    Requests::Requests(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\httpget");
        lexicon.intern("\\httppost");
    }

    void Requests::operator()(syntax::Parser& parser, Context&) const {
        parser.bind("\\httpget", [this](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            const std::string url = syntax::Argument::text(mouth);
            const auto response = network::compose("GET", url);
            if (!response) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                         "\\httpget could not reach '" + url + "'");
                return directive(arena, nullptr, origin);
            }

            const std::string text(response->body.begin(), response->body.end());
            return arena.compose<syntax::Node>(syntax::Node::Type::Text, arena.copy(text), origin);
        });

        parser.bind("\\httppost", [this](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
            memory::Arena& arena = parser.arena();
            const memory::Location origin = mouth.lookahead().location;

            const std::string url = syntax::Argument::text(mouth);
            const std::string body = syntax::Argument::text(mouth);

            const auto response = network::compose("POST", url, body);
            if (!response) {
                tracebacks_.emplace_back(syntax::Traceback::Type::Primitive, origin,
                                         "\\httppost could not reach '" + url + "'");
                return directive(arena, nullptr, origin);
            }

            const std::string text(response->body.begin(), response->body.end());
            return arena.compose<syntax::Node>(syntax::Node::Type::Text, arena.copy(text), origin);
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound request primitives");
    }

}
