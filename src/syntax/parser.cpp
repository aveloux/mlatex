/// @file
/// @brief Parser implementation: token stream to a flat list of AST nodes.
///
/// Error handling follows TeX: report and carry on. An undefined control
/// sequence costs that one token, not the rest of the document, and errors are
/// collected rather than printed, so nested parses cannot report them twice.
#include "syntax/parser.hpp"
#include "logger.hpp"
#include "syntax/semantics/union.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace syntax {

    Parser::Parser(Mouth& mouth, memory::Arena& arena)
        : mouth(mouth), arena(arena) {
        paragraph = mouth.lexicon.intern("\\par");
        assign = mouth.lexicon.intern("\\@set");
        mouth.reader.means = [this](const Symbol symbol) {
            return symbol < handlers.size() && handlers[symbol] != nullptr;
        };
        // Copied before bind() may grow the table out from under it; a
        // source with no meaning here takes the name's away, as TeX's
        // `\let\name\undefined` does.
        mouth.reader.lend = [this](const Symbol name, const Symbol source) {
            Handler handler = source < handlers.size() && handlers[source] ? *handlers[source] : Handler{};
            const bool meant = static_cast<bool>(handler);
            if (meant || name < handlers.size()) bind(name, std::move(handler));
            return meant;
        };
        Logger::log(Logger::Type::Parser, Logger::Level::Informative, "Parser subsystem initialized");
    }

    Parser::~Parser() {
        mouth.reader = {};
    }

    memory::Slice<Node*> Parser::parse(const char closing) {
        Symbol matched = none;
        return parse(closing, {}, matched);
    }

    memory::Slice<Node*> Parser::parse(const char closing, const std::span<const Symbol> stops, Symbol& matched) {
        matched = none;
        Logger::log(Logger::Type::Parser, Logger::Level::Informative,
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

        // The run of text being gathered: made and stamped as its first
        // character arrives, with the face in use then, and given its text
        // once it ends.
        Node* text = nullptr;
        std::uint64_t seen = 0;
        auto flush = [&] {
            if (!buffer.empty()) {
                text->value = arena.copy(buffer);
                nodes.push_back(text);
                buffer.clear();
            }
        };

        while (true) {
            const Token token = mouth.expand();

            if (mouth.error) {
                flush();
                Logger::log(Logger::Type::Parser, Logger::Level::Error,
                            "Parsing abandoned: expansion aborted");
                break;
            }

            if (closing != 0 && token.is(Catcodes::Category::Group, closing)) {
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

            if (std::ranges::contains(stops, token.symbol)) {
                flush();
                matched = token.symbol;
                terminated = true;
                break;
            }

            // A character a document made ordinary -- `\catcode`\$=12` --
            // is text, whatever the character means otherwise.
            if (token.symbol < handlers.size() && handlers[token.symbol] &&
                token.category != Catcodes::Category::Other && token.category != Catcodes::Category::Letter) {
                flush();
                Logger::log(Logger::Type::Parser, Logger::Level::Debug,
                            "Dispatching custom node handler for '{}'", token.text);
                if (Node* node = (*handlers[token.symbol])(*this)) {
                    // A block standing on its own ends the paragraph it
                    // interrupts; text a command set starts one.
                    if (node->display) mouth.vertical = true;
                    if (node->type == Node::Type::Text) mouth.vertical = false;
                    nodes.push_back(node);
                }
                continue;
            }

            if (token.symbol == paragraph) {
                flush();
                mouth.vertical = true;
                Logger::log(Logger::Type::Parser, Logger::Level::Debug,
                            "Constructed Paragraph node at line {} column {}",
                            token.location.line, token.location.column);
                nodes.push_back(arena.compose<Node>(
                    Node::Type::Paragraph, std::string_view{}, token.location, memory::Slice<Node*>{}));
                continue;
            }

            if (token.category == Catcodes::Category::Escape) {
                flush();

                // A register's name standing on its own is an assignment to
                // it, as TeX reads one: `\parskip=6pt`, `\count0 5`.
                const semantics::Registers& registers = mouth.state.registers;
                if (registers.target(token.symbol) || registers.bank(token.symbol)) {
                    const std::array<Token, 2> assignment{
                        Token{assign, Catcodes::Category::Escape, token.location, mouth.lexicon.resolve(assign)},
                        token,
                    };
                    mouth.stream().inject(std::span{assignment});
                    continue;
                }

                // A name \noexpand kept from expanding means nothing here, as
                // TeX's \relax does not.
                if (token.symbol == none) continue;

                if (tracebacks.size() < tolerance) {
                    const std::string message =
                        "Undefined control sequence " + std::string(token.text);

                    Logger::log(Logger::Type::Parser, Logger::Level::Error,
                                "Parsing error at line {} column {}: {}",
                                token.location.line, token.location.column, message);

                    tracebacks.emplace_back(Traceback::Type::Macro, token.location, message);
                }

                if (tracebacks.size() >= tolerance) {
                    Logger::log(Logger::Type::Parser, Logger::Level::Error,
                                "Too many errors; abandoning this parse pass");
                    break;
                }

                continue;
            }

            // A face changed since the run began -- inside the expander, where
            // the parser never saw it happen -- ends the run here.
            if (changes && *changes != seen) flush();
            if (buffer.empty()) {
                position = token.location;
                text = arena.compose<Node>(Node::Type::Text, std::string_view{}, position, memory::Slice<Node*>{});
                if (stamp) stamp(*text);
                if (changes) seen = *changes;
            }
            // A character starts a paragraph; a space between paragraphs
            // does not, as TeX's does not.
            if (token.category != Catcodes::Category::Space) mouth.vertical = false;
            buffer += token.text;
        }

        if (!terminated) {
            const std::string message =
                std::string("Unclosed group: expected '") + closing + "' before end of input";
            Logger::log(Logger::Type::Parser, Logger::Level::Error, "{}", message);
            tracebacks.emplace_back(Traceback::Type::Group, position, message);
        }

        memory::Slice<Node*> slice = arena.allocate<Node*>(nodes.size());
        if (!nodes.empty()) {
            std::ranges::copy(nodes, slice.begin());
        }

        Logger::log(Logger::Type::Parser, Logger::Level::Informative,
                    "Allocated AST slice containing {} node pointers in arena", slice.size());

        return slice;
    }

    void Parser::bind(const std::string_view name, Handler handler) {
        this->bind(mouth.lexicon.intern(name), std::move(handler));
    }

    void Parser::bind(const Symbol symbol, Handler handler) {
        const auto needed = static_cast<std::size_t>(symbol) + 1;
        if (needed > handlers.size()) {
            handlers.resize(std::max<std::size_t>(needed, handlers.size() * 2));
        }
        Logger::log(Logger::Type::Parser, Logger::Level::Debug,
                    "Bound custom AST node handler for symbol {}", symbol);
        // Each where it was made, so a table grown while a handler runs --
        // a \let in the argument it reads -- moves only the pointers.
        handlers[symbol] = handler ? std::make_unique<const Handler>(std::move(handler)) : nullptr;
    }

}