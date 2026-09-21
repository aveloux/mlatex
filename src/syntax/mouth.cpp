/// @file
/// @brief Mouth implementation: macro expansion, scoping and argument capture.
///
/// Three things here are worth knowing before changing anything:
///
/// - **Expansion is iterative, not recursive.** expand() loops calling step()
///   until something unexpandable surfaces. A nesting counter therefore cannot
///   detect a runaway; the budget on expansions *per token produced* can.
/// - **Macros are shared, not copied.** Storage is shared_ptr<const Macro> so
///   that an in-flight expansion survives a redefinition and so that taking an
///   undo record costs an atomic increment rather than two vector copies.
/// - **Scoping uses level stamps.** See the Mouth class documentation; the
///   short version is that `\\global` is O(1) and never walks the undo log.
#include "syntax/mouth.hpp"
#include "syntax/lexer.hpp"
#include "syntax/number.hpp"
#include "syntax/semantics/union.hpp"
#include "memory/location.hpp"
#include "logger.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace syntax {

    namespace {

        [[nodiscard]] std::optional<std::size_t> probe(const Cursor& cursor,
                                                       const std::vector<Token>& delimiters) noexcept {
            std::size_t offset = 0;

            for (const Token& want : delimiters) {
                if (want.category == CatCodes::Category::Space) {
                    while (offset < cursor.size() &&
                           cursor.lookahead(offset).category == CatCodes::Category::Space) {
                        offset++;
                    }
                    continue;
                }

                if (offset >= cursor.size()) return std::nullopt;

                if (const Token got = cursor.lookahead(offset); got.symbol != want.symbol) {
                    return std::nullopt;
                }
                offset++;
            }

            return offset;
        }

        void unwrap(std::vector<Token>& tokens) {
            if (tokens.size() < 2) return;
            if (!tokens.front().is(CatCodes::Category::Group, '{')) return;
            if (!tokens.back().is(CatCodes::Category::Group, '}')) return;

            std::size_t scope = 0;
            for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
                if (tokens[index].category != CatCodes::Category::Group) continue;
                if (tokens[index].is('{')) scope++;
                else if (tokens[index].is('}')) {
                    if (scope == 0) return;
                    scope--;
                    if (scope == 0) return;
                }
            }

            tokens.erase(tokens.begin());
            tokens.pop_back();
        }

    }

    namespace {

        /// @brief Does this body contain anything the expander must rewrite?
        /// @param body Replacement text to inspect.
        /// @return True when it holds a parameter reference or a doubled `#`.
        /// @complexity O(n), paid once at definition time rather than per call.
        [[nodiscard]] bool substitutes(const std::vector<Token>& body) noexcept {
            for (std::size_t index = 0; index + 1 < body.size(); ++index) {
                if (body[index].category == CatCodes::Category::Parameter) return true;
            }
            return false;
        }

    }

    Mouth::Mouth(Cursor stream, semantics::Union& state, Lexicon& names, memory::Arena& storage)
        : cursor(std::move(stream)), domain(state), lexicon_(names), arena_(storage) {
        this->paragraph = this->lexicon_.intern("\\par");
    }

    void Mouth::push(const semantics::Scope::Type type) {
        this->marks.push_back(this->records.size());
        this->kinds.push_back(type);
        this->deferred.emplace_back();
        this->domain.push(type);
        Logger::fmt(Logger::Type::Mouth, Logger::Level::Debug,
                    "State push ({}, depth={})", semantics::Scope::name(type), this->marks.size());
    }

    bool Mouth::pop(const semantics::Scope::Type expected) {
        if (this->marks.empty()) {
            const memory::Location location = this->cursor.empty()
                                                  ? memory::Location{}
                                                  : this->cursor.lookahead(0).location;
            const std::string message = "Dangling scope pop boundary constraint violation";
            Logger::fmt(Logger::Type::Mouth, Logger::Level::Error,
                        "Scope pop failure at {}:{}", location.line, location.column);
            this->tracebacks.emplace_back(Traceback::Type::Scope, location, message);
            return false;
        }

        const semantics::Scope::Type actual = this->kinds.back();
        bool matched = true;

        if (actual != expected) {
            matched = false;
            const memory::Location location = this->cursor.empty()
                                                  ? memory::Location{}
                                                  : this->cursor.lookahead(0).location;
            const std::string message = std::format(
                "Scope closed as {} but was opened as {}",
                semantics::Scope::name(expected), semantics::Scope::name(actual));
            Logger::log(Logger::Type::Mouth, Logger::Level::Error, message);
            this->tracebacks.emplace_back(
                actual == semantics::Scope::Type::Environment ? Traceback::Type::Environment
                                                              : Traceback::Type::Group,
                location, message);
        }

        this->domain.pop();

        const std::size_t mark = this->marks.back();
        this->marks.pop_back();
        this->kinds.pop_back();

        // Walk the tail newest-first, then drop it in one resize. Indexing
        // down from size() makes termination obvious and saves a pop_back per
        // record.
        for (std::size_t index = this->records.size(); index > mark; --index) {
            Record& record = this->records[index - 1];

            const auto slot = static_cast<std::size_t>(record.symbol);
            if (slot >= this->macros.size()) continue;
            if (this->levels[slot] == 0) continue;   // globalized since; the record is dead

            this->levels[slot] = record.level;
            this->macros[slot] = std::move(record.macro);
        }
        this->records.resize(mark);

        if (!this->deferred.empty()) {
            std::vector<Token> pending = std::move(this->deferred.back());
            this->deferred.pop_back();
            if (!pending.empty()) {
                this->inject(pending);
            }
        }

        return matched;
    }

    void Mouth::pop() {
        if (this->marks.empty()) {
            this->pop(semantics::Scope::Type::Group);
            return;
        }
        this->pop(this->kinds.back());
    }

    void Mouth::globalize() noexcept { this->global = 1; }

    int Mouth::unglobal() noexcept {
        const int flag = this->global;
        this->global = 0;
        return flag;
    }

    void Mouth::longify() noexcept { this->spanning = 1; }

    int Mouth::unlong() noexcept {
        const int flag = this->spanning;
        this->spanning = 0;
        return flag;
    }

    void Mouth::outerize() noexcept { this->isolated = 1; }

    int Mouth::unouter() noexcept {
        const int flag = this->isolated;
        this->isolated = 0;
        return flag;
    }

    void Mouth::defer(const Token& token) {
        if (!this->deferred.empty()) {
            this->deferred.back().push_back(token);
        }
    }

    void Mouth::schedule(const Token& token) {
        this->scheduled = token;
    }

    Token Mouth::read() noexcept {
        if (this->cursor.empty()) return {};
        return this->cursor.advance();
    }

    void Mouth::abort(const Traceback::Type type, const std::string_view message) {
        this->aborted = true;
        Logger::log(Logger::Type::Mouth, Logger::Level::Error, message);
        this->tracebacks.emplace_back(type, memory::Location{}, message);
        this->cursor.clear();
    }

    Token Mouth::expand() {
        if (this->aborted) return {};

        if (this->depth >= this->limit) {
            this->abort(Traceback::Type::Recursion,
                        std::format("Expansion re-entered more than {} levels deep", this->limit));
            return {};
        }

        this->depth++;
        struct Guard {
            std::size_t& counter;
            ~Guard() { --counter; }
        } guard{this->depth};

        std::size_t spent = 0;

        while (!this->cursor.empty()) {
            if (!this->step()) {
                return this->cursor.advance();
            }

            if (++spent > this->budget) {
                this->abort(Traceback::Type::Recursion,
                            std::format("Runaway expansion: {} expansions without producing a token "
                                        "(a macro is almost certainly defined in terms of itself)",
                                        this->budget));
                return {};
            }

            if (this->cursor.size() > this->capacity) {
                this->abort(Traceback::Type::Memory,
                            std::format("Expansion produced more than {} pending tokens", this->capacity));
                return {};
            }
        }

        return {};
    }

    int Mouth::step() {
        if (this->cursor.empty()) return 0;

        const Token token = this->cursor.lookahead(0);
        if (token.symbol == kInvalidSymbol) return 0;

        if (this->suppressed) {
            this->suppressed = 0;
            return 0;
        }

        if (token.symbol < this->handlers.size() && this->handlers[token.symbol]) {
            this->cursor.advance();
            this->handlers[token.symbol](*this);
            return 1;
        }

        if (token.symbol < this->macros.size() && this->macros[token.symbol]) {
            this->cursor.advance();

            // Shared, not copied: expanding an argument may redefine this very
            // macro, and the old body has to stay alive until we are done with
            // it. A copy here meant deep-copying two vectors per macro call.
            const std::shared_ptr<const Macro> macro = this->macros[token.symbol];

            // Most macros take no arguments and hold no parameter references,
            // so the whole substitution pass and its allocation can be skipped.
            if (macro->literal) {
                this->cursor.inject(macro->body);
                return 1;
            }

            std::vector<std::vector<Token>> arguments;
            arguments.reserve(macro->parameters.size());
            for (const auto& parameter : macro->parameters) {
                arguments.push_back(this->argument(parameter, macro->spanning));
            }

            std::vector<Token> output;
            const auto& body = macro->body;
            std::size_t total = body.size();
            for (const auto& item : arguments) {
                total += item.size();
            }
            output.reserve(total);

            for (std::size_t index = 0; index < body.size(); ++index) {
                if (body[index].category == CatCodes::Category::Parameter && index + 1 < body.size()) {
                    const Token& next = body[index + 1];

                    if (next.category == CatCodes::Category::Parameter) {
                        output.push_back(next);
                        index++;
                        continue;
                    }

                    if (const auto& values = next.values;
                        !values.empty() && values[0] >= '1' && values[0] <= '9') {
                        if (const auto slot = static_cast<std::size_t>(values[0] - '1');
                            slot < arguments.size()) {
                            output.insert(output.end(), arguments[slot].begin(), arguments[slot].end());
                        }
                        index++;
                        continue;
                    }
                }
                output.push_back(body[index]);
            }

            this->cursor.inject(output);
            return 1;
        }

        return 0;
    }

    std::vector<Token> Mouth::argument(const Parameter& parameter, const int allow) {
        std::vector<Token> result;
        if (this->cursor.empty()) return result;

        const auto forbidden = [&](const Token& token) -> int {
            if (token.symbol == this->paragraph && !allow) {
                const std::string message = "Paragraph break inside argument of a macro that isn't \\long";
                Logger::log(Logger::Type::Mouth, Logger::Level::Error, message);
                this->tracebacks.emplace_back(Traceback::Type::Argument, token.location, message);
                return 1;
            }
            if (const auto slot = static_cast<std::size_t>(token.symbol);
                slot < this->macros.size() && this->macros[slot] && this->macros[slot]->isolated) {
                const std::string message =
                    std::format("\\outer macro {} may not appear inside an argument", token.values);
                Logger::log(Logger::Type::Mouth, Logger::Level::Error, message);
                this->tracebacks.emplace_back(Traceback::Type::Argument, token.location, message);
                return 1;
            }
            return 0;
        };

        if (parameter.optional) {
            const Token lead = this->cursor.lookahead(0);
            if (lead.category == CatCodes::Category::Other && lead.is('[')) {
                this->cursor.advance();
                std::size_t scope = 1;
                while (!this->cursor.empty() && scope > 0) {
                    Token token = this->cursor.advance();
                    if (token.is('[')) scope++;
                    else if (token.is(']')) scope--;
                    if (scope > 0) {
                        if (forbidden(token)) break;
                        result.push_back(token);
                    }
                }
            } else {
                result = parameter.fallbacks;
            }
        } else if (!parameter.delimiters.empty()) {
            std::size_t scope = 0;
            bool matched = false;

            while (!this->cursor.empty()) {
                if (scope == 0) {
                    if (const auto span = probe(this->cursor, parameter.delimiters)) {
                        matched = true;
                        for (std::size_t count = 0; count < *span; ++count) {
                            this->cursor.advance();
                        }
                        break;
                    }
                }

                Token token = this->cursor.advance();
                if (forbidden(token)) break;

                if (token.category == CatCodes::Category::Group) {
                    if (token.is('{')) scope++;
                    else if (token.is('}') && scope > 0) scope--;
                }
                result.push_back(token);
            }

            if (!matched) {
                const memory::Location location = this->cursor.empty()
                                                      ? memory::Location{}
                                                      : this->cursor.lookahead(0).location;
                const std::string message = "Delimited macro expansion missing parameter bounding tokens";
                Logger::log(Logger::Type::Mouth, Logger::Level::Error, message);
                this->tracebacks.emplace_back(Traceback::Type::Delimiter, location, message);
            }

            unwrap(result);
        } else {
            while (!this->cursor.empty() &&
                   this->cursor.lookahead(0).category == CatCodes::Category::Space) {
                this->cursor.advance();
            }

            if (const Token token = this->cursor.advance();
                token.category == CatCodes::Category::Group && token.is('{')) {
                std::size_t scope = 1;
                while (!this->cursor.empty() && scope > 0) {
                    Token element = this->cursor.advance();
                    if (element.category == CatCodes::Category::Group) {
                        if (element.is('{')) scope++;
                        else if (element.is('}')) scope--;
                    }
                    if (scope > 0) {
                        if (forbidden(element)) break;
                        result.push_back(element);
                    }
                }
                if (scope > 0) {
                    const std::string message = "Macro argument block truncation missing trailing brace";
                    Logger::log(Logger::Type::Mouth, Logger::Level::Error, message);
                    this->tracebacks.emplace_back(Traceback::Type::Group, token.location, message);
                }
            } else if (forbidden(token)) {
                this->inject(std::span{&token, 1});
            } else if (!token.empty()) {
                result.push_back(token);
            }
        }

        return result;
    }

    void Mouth::inject(const std::span<const Token> batch) {
        this->cursor.inject(batch);
    }

    void Mouth::ingest(const std::string_view source) {
        Lexer lexer(source, this->domain.catcodes(), this->lexicon_);

        std::vector<Token> tokens;
        tokens.reserve(source.size());   // worst case is one token per byte

        while (!lexer.empty()) {
            if (Token token = lexer.advance(); !token.empty()) {
                tokens.push_back(token);
            }
        }

        for (const auto& fault : lexer.tracebacks()) {
            this->tracebacks.push_back(fault);
        }

        Logger::fmt(Logger::Type::Mouth, Logger::Level::Debug,
                    "Lexed buffer mapping block elements size {}", tokens.size());

        this->cursor.adopt(std::move(tokens));
    }

    void Mouth::bind(const std::string_view name, Handler handler) {
        this->bind(this->lexicon_.intern(name), std::move(handler));
    }

    void Mouth::bind(const Symbol symbol, Handler handler) {
        const auto needed = static_cast<std::size_t>(symbol) + 1;
        if (needed > this->handlers.size()) {
            this->handlers.resize(std::max<std::size_t>(needed, this->handlers.size() * 2));
        }
        this->handlers[symbol] = std::move(handler);
    }

    void Mouth::reserve(const std::size_t slot) {
        if (slot < this->macros.size()) return;
        const std::size_t needed = std::max<std::size_t>(slot + 1, this->macros.size() * 2);
        this->macros.resize(needed);
        this->levels.resize(needed);
    }

    void Mouth::define(const std::string_view name, Macro macro) {
        this->define(this->lexicon_.intern(name), std::move(macro));
    }

    void Mouth::define(const Symbol symbol, Macro macro) {
        const auto slot = static_cast<std::size_t>(symbol);
        const int universal = this->unglobal();

        macro.active = true;
        macro.literal = macro.parameters.empty() && !substitutes(macro.body);
        this->reserve(slot);

        if (universal) {
            this->levels[slot] = 0;
        } else if (const auto level = static_cast<std::uint32_t>(this->marks.size());
                   level != 0 && this->levels[slot] != level) {
            this->records.push_back(Record{symbol, this->macros[slot], this->levels[slot]});
            this->levels[slot] = level;
        }

        this->macros[slot] = std::make_shared<const Macro>(std::move(macro));

        if (this->scheduled) {
            const Token pending = *this->scheduled;
            this->scheduled.reset();
            this->inject(std::span{&pending, 1});
        }
    }

    void Mouth::undefine(const std::string_view name) {
        this->undefine(this->lexicon_.intern(name));
    }

    void Mouth::undefine(const Symbol symbol) {
        const auto slot = static_cast<std::size_t>(symbol);
        const int universal = this->unglobal();

        if (slot < this->macros.size() && this->macros[slot]) {
            if (universal) {
                this->levels[slot] = 0;
            } else if (const auto level = static_cast<std::uint32_t>(this->marks.size());
                       level != 0 && this->levels[slot] != level) {
                this->records.push_back(Record{symbol, this->macros[slot], this->levels[slot]});
                this->levels[slot] = level;
            }

            this->macros[slot] = nullptr;
        }

        if (this->scheduled) {
            const Token pending = *this->scheduled;
            this->scheduled.reset();
            this->inject(std::span{&pending, 1});
        }
    }

    Token Mouth::lookahead(const std::size_t offset) const noexcept {
        return this->cursor.lookahead(offset);
    }

    const Mouth::Macro* Mouth::lookup(const Symbol symbol) const noexcept {
        const auto slot = static_cast<std::size_t>(symbol);
        if (slot < this->macros.size() && this->macros[slot]) {
            return this->macros[slot].get();
        }
        return nullptr;
    }

    const Mouth::Handler* Mouth::primitive(const Symbol symbol) const noexcept {
        const auto slot = static_cast<std::size_t>(symbol);
        if (slot < this->handlers.size() && this->handlers[slot]) {
            return &this->handlers[slot];
        }
        return nullptr;
    }

    std::vector<Traceback> Mouth::drain() {
        std::vector<Traceback> taken;
        taken.swap(this->tracebacks);
        return taken;
    }

    std::optional<std::int32_t> Mouth::integer(const semantics::Registers& ledger, const Symbol count) {
        return Number::integer(this->cursor, ledger, count);
    }

    std::optional<std::int32_t> Mouth::dimension(const semantics::Registers& ledger,
                                                 const Symbol count, const Symbol unit) {
        return Number::dimension(this->cursor, ledger, count, unit);
    }

}