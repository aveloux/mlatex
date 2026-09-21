/// @file
/// @brief Parser implementation: token stream to a flat list of AST nodes.
///
/// Error handling follows TeX: report and carry on. An undefined control
/// sequence costs that one token, not the rest of the document, and errors are
/// collected rather than printed, so nested parses cannot report them twice.
#include "syntax/parser.hpp"
#include "logger.hpp"

#include <algorithm>
#include <ostream>
#include <string>
#include <vector>

namespace syntax {

    Parser::Parser(Mouth& mouth, memory::Arena& arena)
        : mouth_(mouth), arena_(arena) {
        paragraph = mouth_.lexicon().intern("\\par");
        Logger::log(Logger::Type::Parser, Logger::Level::Informative, "Parser subsystem initialized");
    }

    Token Parser::step() const {
        return mouth_.expand();
    }

    memory::Slice<Node*> Parser::parse() {
        return parse(0);
    }

    memory::Slice<Node*> Parser::parse(const char closing) {
        Logger::fmt(Logger::Type::Parser, Logger::Level::Informative,
                    "Starting AST syntax parsing pass (depth {})...", nesting);

        nesting++;
        struct Guard {
            std::size_t& nesting;
            ~Guard() { --nesting; }
        } guard{nesting};

        std::vector<Node*> nodes;
        nodes.reserve(nesting == 1 ? 1024 : 16);

        std::string buffer;
        buffer.reserve(256);

        memory::Location position{};
        bool terminated = closing == 0;

        auto flush = [&] {
            if (!buffer.empty()) {
                nodes.push_back(arena_.compose<Node>(
                    Node::Type::Text,
                    arena_.copy(buffer),
                    position,
                    memory::Slice<Node*>{}
                ));
                buffer.clear();
            }
        };

        while (true) {
            const Token token = mouth_.expand();

            if (mouth_.failed()) {
                flush();
                for (auto&& trace : mouth_.drain()) {
                    tracebacks_.push_back(std::move(trace));
                }
                Logger::log(Logger::Type::Parser, Logger::Level::Error,
                            "Parsing abandoned: expansion aborted");
                break;
            }

            if (closing != 0 && token.is(CatCodes::Category::Group, closing)) {
                flush();
                terminated = true;
                break;
            }

            if (token.empty()) {
                flush();
                Logger::log(Logger::Type::Parser, Logger::Level::Debug,
                            "Parser reached end of expansion stream");
                break;
            }

            if (token.symbol < handlers.size() && handlers[token.symbol]) {
                flush();
                Logger::fmt(Logger::Type::Parser, Logger::Level::Debug,
                            "Dispatching custom node handler for '{}'", token.values);
                if (Node* node = handlers[token.symbol](*this)) {
                    nodes.push_back(node);
                }
                continue;
            }

            if (token.symbol == paragraph) {
                flush();
                Logger::fmt(Logger::Type::Parser, Logger::Level::Debug,
                            "Constructed Paragraph node at line {} column {}",
                            token.location.line, token.location.column);
                nodes.push_back(arena_.compose<Node>(
                    Node::Type::Paragraph, std::string_view{}, token.location, memory::Slice<Node*>{}));
                continue;
            }

            if (token.category == CatCodes::Category::Escape) {
                flush();

                if (tracebacks_.size() < tolerance) {
                    const std::string message =
                        "Undefined macro or unhandled command primitive: " + std::string(token.values);

                    Logger::fmt(Logger::Type::Parser, Logger::Level::Error,
                                "Parsing error at line {} column {}: {}",
                                token.location.line, token.location.column, message);

                    tracebacks_.emplace_back(Traceback::Type::Macro, token.location, message);
                }

                if (tracebacks_.size() >= tolerance) {
                    Logger::log(Logger::Type::Parser, Logger::Level::Error,
                                "Too many errors; abandoning this parse pass");
                    break;
                }

                continue;
            }

            if (buffer.empty()) {
                position = token.location;
            }
            buffer += token.values;
        }

        if (!terminated) {
            const std::string message =
                std::string("Unclosed group: expected '") + closing + "' before end of input";
            Logger::log(Logger::Type::Parser, Logger::Level::Error, message);
            tracebacks_.emplace_back(Traceback::Type::Group, position, message);
        }

        if (nesting == 1) {
            for (auto&& trace : mouth_.drain()) {
                tracebacks_.push_back(std::move(trace));
            }
        }

        memory::Slice<Node*> slice = arena_.allocate<Node*>(nodes.size());
        if (!nodes.empty()) {
            std::ranges::copy(nodes, slice.begin());
        }

        Logger::fmt(Logger::Type::Parser, Logger::Level::Informative,
                    "Allocated AST slice containing {} node pointers in arena", slice.size());

        return slice;
    }

    void Parser::report(std::ostream& stream) const {
        for (const auto& traceback : tracebacks_) {
            stream << traceback.format() << '\n';
        }
        if (tracebacks_.size() >= tolerance) {
            stream << "(further errors suppressed)\n";
        }
    }

    bool Parser::failed() const noexcept {
        return !tracebacks_.empty() || mouth_.failed();
    }

    void Parser::bind(const std::string_view name, Handler handler) {
        this->bind(mouth_.lexicon().intern(name), std::move(handler));
    }

    void Parser::bind(const Symbol symbol, Handler handler) {
        const auto needed = static_cast<std::size_t>(symbol) + 1;
        if (needed > handlers.size()) {
            handlers.resize(std::max<std::size_t>(needed, handlers.size() * 2));
        }
        Logger::fmt(Logger::Type::Parser, Logger::Level::Debug,
                    "Bound custom AST node handler for symbol {}", symbol);
        handlers[symbol] = std::move(handler);
    }

}