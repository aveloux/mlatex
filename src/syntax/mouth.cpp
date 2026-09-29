/// @file
/// @brief Mouth implementation: macro expansion, scoping and argument capture.
///
/// Three things here are worth knowing before changing anything:
///
/// - **Expansion is iterative, not recursive.** expand() loops, running one
///   handler or one macro per turn, until something unexpandable surfaces. A
///   nesting counter therefore cannot detect a runaway; the budget on
///   expansions *per token produced* can.
/// - **Macros are shared, not copied.** Storage is shared_ptr<const Macro> so
///   that an in-flight expansion survives a redefinition and so that taking an
///   undo record costs an atomic increment rather than two vector copies.
/// - **Scoping uses level stamps.** See the Mouth class documentation; the
///   short version is that a global definition is O(1) and never walks the
///   undo log.
#include "syntax/mouth.hpp"
#include "syntax/lexer.hpp"
#include "syntax/semantics/union.hpp"
#include "memory/location.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace syntax {

    Mouth::Mouth(Cursor stream, semantics::Union& state, Lexicon& lexicon, memory::Arena& arena)
        : cursor(std::move(stream)), state_(state), lexicon_(lexicon), arena_(arena) {
        this->symbol_ = this->lexicon_.intern("\\par");
    }

    void Mouth::push(const semantics::Scope::Type type) {
        this->marks.push_back(this->records.size());
        this->types.push_back(type);
        this->state_.push();
        Logger::log(Logger::Type::Mouth, Logger::Level::Debug,
                    "State push ({}, depth={})", semantics::Scope::name(type), this->marks.size());
    }

    bool Mouth::pop(const semantics::Scope::Type expected) {
        const memory::Location location = this->cursor.empty()
                                              ? memory::Location{}
                                              : this->cursor.lookahead(0).location;

        if (this->marks.empty()) {
            this->tracebacks_.emplace_back(Traceback::Type::Scope, location,
                                           "A group closed that was never opened");
            return false;
        }

        // What \aftergroup put by for this group, read again as it closes,
        // in the order it was put by.
        const std::size_t level = this->marks.size();
        std::size_t first = this->deferred.size();
        while (first > 0 && this->deferred[first - 1].first == level) --first;
        if (first < this->deferred.size()) {
            std::vector<Token> after;
            after.reserve(this->deferred.size() - first);
            for (std::size_t index = first; index < this->deferred.size(); ++index) {
                after.push_back(this->deferred[index].second);
            }
            this->deferred.resize(first);
            this->cursor.inject(after);
        }

        const semantics::Scope::Type actual = this->types.back();
        const bool matched = actual == expected;
        if (!matched) {
            this->tracebacks_.emplace_back(
                actual == semantics::Scope::Type::Environment ? Traceback::Type::Environment
                                                              : Traceback::Type::Group,
                location,
                std::format("Scope closed as {} but was opened as {}",
                            semantics::Scope::name(expected), semantics::Scope::name(actual)));
        }

        this->state_.pop();

        const std::size_t mark = this->marks.back();
        this->marks.pop_back();
        this->types.pop_back();

        // Walk the tail newest-first, then drop it in one resize. Indexing
        // down from size() makes termination obvious and saves a pop_back per
        // record.
        for (std::size_t index = this->records.size(); index > mark; --index) {
            Record& record = this->records[index - 1];

            const auto slot = static_cast<std::size_t>(record.symbol);
            if (slot >= this->macros.size()) continue;
            if (this->levels[slot] == 0) continue;   // made global since; the record is dead

            this->levels[slot] = record.level;
            this->macros[slot] = std::move(record.macro);
        }
        this->records.resize(mark);

        return matched;
    }

    Token Mouth::read() noexcept {
        if (this->cursor.empty()) return {};
        return this->cursor.advance();
    }

    Token Mouth::expand() {
        if (this->error_) return {};

        if (this->depth >= this->limit) {
            this->error_ = true;
            this->tracebacks_.emplace_back(
                Traceback::Type::Recursion, memory::Location{},
                std::format("Expansion re-entered more than {} levels deep", this->limit));
            this->cursor.dispose();
            return {};
        }

        this->depth++;
        struct Guard {
            std::size_t& counter;
            ~Guard() { --counter; }
        } guard{this->depth};

        for (std::size_t spent = 0; !this->cursor.empty(); ++spent) {
            if (spent > this->budget) {
                this->error_ = true;
                this->tracebacks_.emplace_back(
                    Traceback::Type::Recursion, memory::Location{},
                    std::format("Runaway expansion: {} expansions without producing a token "
                                "(a macro is almost certainly defined in terms of itself)",
                                this->budget));
                this->cursor.dispose();
                return {};
            }

            if (this->cursor.size() > this->capacity) {
                this->error_ = true;
                this->tracebacks_.emplace_back(
                    Traceback::Type::Memory, memory::Location{},
                    std::format("Expansion produced more than {} pending tokens", this->capacity));
                this->cursor.dispose();
                return {};
            }

            const Token token = this->cursor.lookahead(0);
            const auto slot = static_cast<std::size_t>(token.symbol);

            // A macro before a primitive of the same name: a definition
            // replaces whatever a name meant, as it does in TeX, so a
            // document's own `\newcommand{\set}` is its \set from there on.
            // Only a name or an active character means anything: a `!` a
            // document made active is a macro, and every other `!` a `!`.
            const bool defined = token.symbol != none && slot < this->macros.size() && this->macros[slot] &&
                                 (token.category == CatCodes::Category::Escape ||
                                  token.category == CatCodes::Category::Active);
            if (defined && this->robust_ && this->macros[slot]->robust) return this->cursor.advance();

            // A primitive: run it, and look again at whatever it left.
            if (!defined && token.symbol != none && slot < this->handlers.size() && this->handlers[slot]) {
                this->cursor.advance();
                (*this->handlers[slot])(*this);
                continue;
            }

            // Anything that is neither a primitive nor a macro is the answer
            // -- unless it is a name a CTAN package gives a meaning, whose
            // definition the glossary puts in front of it the first time it
            // is met. A macro is replaced by its body, and whatever that
            // begins with looked at again.
            if (!defined) {
                if (token.category == CatCodes::Category::Escape && this->glossary.define(*this, token)) continue;

                // `@` is a letter to the engine's own modules and an ordinary
                // character to a document, as LaTeX has it: a name nothing
                // means that runs on past an `@` -- `\xymatrix@C=1em`,
                // `\ar@{-->}` -- is the name before it, then its characters.
                if (token.category == CatCodes::Category::Escape && !this->known(token.symbol)) {
                    if (const std::size_t at = token.text.find('@', 2); at != std::string_view::npos) {
                        const Symbol name = this->lexicon_.intern(token.text.substr(0, at));
                        if (this->known(name)) {
                            std::vector<Token> split{Token{name, CatCodes::Category::Escape, token.location,
                                                           this->lexicon_.resolve(name)}};
                            for (std::size_t index = at; index < token.text.size(); ++index) {
                                const Symbol letter = this->lexicon_.intern(token.text.substr(index, 1));
                                const bool alphabetic = std::isalpha(static_cast<unsigned char>(token.text[index])) != 0;
                                split.push_back(Token{letter, alphabetic ? CatCodes::Category::Letter : CatCodes::Category::Other,
                                                      token.location, this->lexicon_.resolve(letter)});
                            }
                            this->cursor.advance();
                            this->cursor.inject(split);
                            continue;
                        }
                    }
                }
                return this->cursor.advance();
            }
            this->step();
        }

        return {};
    }

    bool Mouth::step() {
        if (this->cursor.empty()) return false;

        const Token token = this->cursor.lookahead(0);
        const auto slot = static_cast<std::size_t>(token.symbol);
        if (token.symbol == none) return false;

        if (slot >= this->macros.size() || !this->macros[slot] ||
            (token.category != CatCodes::Category::Escape && token.category != CatCodes::Category::Active)) {
            if (slot >= this->handlers.size() || !this->handlers[slot]) return false;
            this->cursor.advance();
            (*this->handlers[slot])(*this);
            return true;
        }

        this->cursor.advance();

        // Shared, not copied: expanding an argument may redefine this very
        // macro, and the old body has to stay alive until we are done with
        // it. A copy here meant deep-copying two vectors per macro call.
        const std::shared_ptr<const Macro> macro = this->macros[slot];

        // Most macros take no arguments and hold no parameter references,
        // so the whole substitution pass and its allocation can be skipped.
        if (macro->literal) {
            this->cursor.inject(macro->body);
            return true;
        }

        // What the definition wrote before its first parameter is matched
        // and passed over; a call that does not match it is reported, and
        // read on as though it had.
        for (const Token& expected : macro->prefix) {
            if (this->cursor.empty() || this->cursor.lookahead(0).text != expected.text) {
                this->tracebacks_.emplace_back(Traceback::Type::Argument, token.location,
                                               std::format("Use of {} does not match its definition", token.text));
                break;
            }
            this->cursor.advance();
        }

        std::vector<std::vector<Token>> arguments;
        arguments.reserve(macro->parameters.size());
        for (const auto& parameter : macro->parameters) {
            arguments.push_back(this->argument(parameter, macro->spanning));
        }

        std::vector<Token> output;
        const auto& body = macro->body;
        std::size_t total = body.size();
        for (const auto& item : arguments) total += item.size();
        output.reserve(total);

        for (std::size_t index = 0; index < body.size(); ++index) {
            if (body[index].category == CatCodes::Category::Parameter && index + 1 < body.size()) {
                const Token& next = body[index + 1];

                // `##` is one `#`, kept for a definition inside the body.
                if (next.category == CatCodes::Category::Parameter) {
                    output.push_back(next);
                    index++;
                    continue;
                }

                if (const auto& values = next.text;
                    !values.empty() && values[0] >= '1' && values[0] <= '9') {
                    if (const auto place = static_cast<std::size_t>(values[0] - '1');
                        place < arguments.size()) {
                        output.insert(output.end(), arguments[place].begin(), arguments[place].end());
                    }
                    index++;
                    continue;
                }
            }
            output.push_back(body[index]);
        }

        this->cursor.inject(output);
        return true;
    }

    std::vector<Token> Mouth::argument(const Parameter& parameter, const int allow) {
        std::vector<Token> result;
        if (this->cursor.empty()) return result;

        // Room for an ordinary argument from the start. Grown a token at a
        // time, a vector reallocates its way up through every size on the
        // way, and each of those is a trip to the heap for a word or two.
        result.reserve(16);

        // What may not stand inside an argument: a paragraph break unless the
        // macro is `\long`, and an `\outer` macro ever.
        const auto forbidden = [&](const Token& token) -> bool {
            if (token.symbol == this->symbol_ && !allow) {
                this->tracebacks_.emplace_back(Traceback::Type::Argument, token.location,
                                               "Paragraph break inside argument of a macro that isn't \\long");
                return true;
            }
            if (const auto slot = static_cast<std::size_t>(token.symbol);
                slot < this->macros.size() && this->macros[slot] && this->macros[slot]->isolated) {
                this->tracebacks_.emplace_back(
                    Traceback::Type::Argument, token.location,
                    std::format("\\outer macro {} may not appear inside an argument", token.text));
                return true;
            }
            return false;
        };

        if (!parameter.present.empty()) {
            while (!this->cursor.empty() && this->cursor.lookahead(0).category == CatCodes::Category::Space) {
                this->cursor.advance();
            }
            if (this->cursor.empty() || this->cursor.lookahead(0).text != parameter.present.front().text) {
                return parameter.fallbacks;
            }
            this->cursor.advance();
            return {parameter.present.begin() + 1, parameter.present.end()};
        }

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
            return result;
        }

        if (!parameter.delimiters.empty()) {
            std::size_t scope = 0;
            bool matched = false;

            while (!this->cursor.empty()) {
                // At the outer level, does the stream start with the
                // delimiters here? A space in them stands for any run of
                // blanks, as it does in TeX's parameter text.
                if (scope == 0) {
                    std::size_t offset = 0;
                    bool found = true;
                    for (const Token& want : parameter.delimiters) {
                        if (want.category == CatCodes::Category::Space) {
                            while (offset < this->cursor.size() &&
                                   this->cursor.lookahead(offset).category == CatCodes::Category::Space) {
                                offset++;
                            }
                            continue;
                        }
                        if (offset >= this->cursor.size() ||
                            this->cursor.lookahead(offset).symbol != want.symbol) {
                            found = false;
                            break;
                        }
                        offset++;
                    }
                    if (found) {
                        matched = true;
                        for (std::size_t count = 0; count < offset; ++count) this->cursor.advance();
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
                this->tracebacks_.emplace_back(
                    Traceback::Type::Delimiter,
                    this->cursor.empty() ? memory::Location{} : this->cursor.lookahead(0).location,
                    "A delimited argument ran out before its delimiter");
            }

            // One layer of braces around the whole argument is TeX's way of
            // hiding a delimiter inside it, and goes: `{a,b}` is `a,b`. Only
            // when the braces really enclose all of it -- `{a}{b}` keeps both.
            if (result.size() >= 2 && result.front().is(CatCodes::Category::Group, '{') &&
                result.back().is(CatCodes::Category::Group, '}')) {
                std::size_t depth_ = 0;
                bool whole = true;
                for (std::size_t index = 0; index + 1 < result.size(); ++index) {
                    if (result[index].category != CatCodes::Category::Group) continue;
                    if (result[index].is('{')) {
                        depth_++;
                    } else if (result[index].is('}') && --depth_ == 0) {
                        whole = false;
                        break;
                    }
                }
                if (whole) {
                    result.erase(result.begin());
                    result.pop_back();
                }
            }
            return result;
        }

        // Undelimited: one token, or one brace group without its braces.
        while (!this->cursor.empty() && this->cursor.lookahead(0).category == CatCodes::Category::Space) {
            this->cursor.advance();
        }

        const Token token = this->cursor.advance();
        if (token.category == CatCodes::Category::Group && token.is('{')) {
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
                this->tracebacks_.emplace_back(Traceback::Type::Group, token.location,
                                               "An argument's group was never closed");
            }
        } else if (forbidden(token)) {
            this->cursor.inject(std::span{&token, 1});
        } else if (!token.empty()) {
            result.push_back(token);
        }

        return result;
    }

    void Mouth::ingest(const std::string_view source, const std::optional<memory::Location> origin) {
        Lexer lexer(source, this->state_.catcodes(), this->lexicon_);

        std::vector<Token> tokens;
        tokens.reserve(source.size());   // worst case is one token per byte

        while (!lexer.empty()) {
            if (Token token = lexer.advance(); !token.empty()) {
                if (origin) token.location = *origin;
                tokens.push_back(token);
            }
        }

        this->tracebacks_.insert(this->tracebacks_.end(),
                                 lexer.tracebacks().begin(), lexer.tracebacks().end());
        this->cursor.adopt(std::move(tokens));
    }

    void Mouth::recategorize(const char character, const CatCodes::Category category, const bool global) {
        using Category = CatCodes::Category;
        const std::vector<Token> unread = this->cursor.source();
        if (unread.empty()) return;

        // A local change reaches the end of the group it is made in: the
        // first `}`, `\endgroup` or `\end` that closes more than was opened.
        std::size_t reach = unread.size();
        if (!global) {
            const Symbol begingroup = this->lexicon_.intern("\\begingroup");
            const Symbol endgroup = this->lexicon_.intern("\\endgroup");
            const Symbol begin = this->lexicon_.intern("\\begin");
            const Symbol end = this->lexicon_.intern("\\end");
            long nesting = 0;
            for (std::size_t at = 0; at < unread.size(); ++at) {
                const Token& token = unread[at];
                if (token.is(Category::Group, '{') || token.symbol == begingroup || token.symbol == begin) ++nesting;
                if (token.is(Category::Group, '}') || token.symbol == endgroup || token.symbol == end) --nesting;
                if (nesting < 0) {
                    reach = at;
                    break;
                }
            }
        }

        const auto mine = [character](const Token& token) {
            return token.category != Category::Escape && token.text.size() == 1 && token.text[0] == character;
        };
        // The next token stands straight after the one before on its line.
        const auto follows = [](const Token& token, const std::uint32_t line, const std::uint32_t column) {
            return token.location.line == line && token.location.column == column;
        };
        // A control word's name: the letters that follow its escape on its
        // line, and the blanks after them, which TeX reads past.
        const auto word = [&](std::string& name, std::size_t& at, std::uint32_t column) {
            const std::uint32_t line = unread[at].location.line;
            while (at + 1 < reach && follows(unread[at + 1], line, column) &&
                   (unread[at + 1].category == Category::Letter || (category == Category::Letter && mine(unread[at + 1])))) {
                name += unread[++at].text;
                ++column;
            }
            while (at + 1 < reach && unread[at + 1].category == Category::Space && unread[at + 1].location.line == line) ++at;
        };

        std::vector<Token> result;
        result.reserve(unread.size());
        for (std::size_t at = 0; at < unread.size(); ++at) {
            Token token = unread[at];
            if (at >= reach) {
                result.push_back(token);
                continue;
            }
            // A control word the character, a letter now, carries on:
            // `\my_name` once `_` is one.
            const bool worded = token.category == Category::Escape && token.text.size() > 1 &&
                                std::ranges::all_of(token.text.substr(1), [](const char letter) {
                                    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || letter == '@';
                                });
            const auto column = static_cast<std::uint32_t>(token.location.column + token.text.size());
            if (category == Category::Letter && worded && at + 1 < reach && mine(unread[at + 1]) &&
                follows(unread[at + 1], token.location.line, column)) {
                std::string name(token.text);
                word(name, at, column);
                token.symbol = this->lexicon_.intern(name);
                token.text = this->lexicon_.resolve(token.symbol);
                result.push_back(token);
                continue;
            }
            if (!mine(token)) {
                result.push_back(token);
                continue;
            }

            switch (category) {
                case Category::Ignore:
                    break;
                case Category::Comment: {
                    const std::uint32_t line = token.location.line;
                    while (at + 1 < reach && unread[at + 1].location.line == line) ++at;
                    break;
                }
                case Category::Escape: {
                    // `|textbf` a control word, `|{` a control symbol.
                    std::string name = "\\";
                    const Token* after = at + 1 < reach ? &unread[at + 1] : nullptr;
                    if (after && after->category == Category::Letter &&
                        follows(*after, token.location.line, token.location.column + 1)) {
                        word(name, at, token.location.column + 1);
                    } else if (after && after->text.size() == 1 && after->category != Category::Escape &&
                               after->location.line == token.location.line) {
                        name += unread[++at].text;
                    }
                    token.symbol = this->lexicon_.intern(name);
                    token.text = this->lexicon_.resolve(token.symbol);
                    token.category = Category::Escape;
                    result.push_back(token);
                    break;
                }
                case Category::Space:
                    token.symbol = this->lexicon_.intern(" ");
                    token.text = this->lexicon_.resolve(token.symbol);
                    token.category = Category::Space;
                    result.push_back(token);
                    break;
                default:
                    token.category = category;
                    result.push_back(token);
                    break;
            }
        }
        this->cursor.source(result);
    }

    void Mouth::bind(const std::string_view name, Handler handler) {
        this->bind(this->lexicon_.intern(name), std::move(handler));
    }

    void Mouth::bind(const Symbol symbol, Handler handler) {
        const auto needed = static_cast<std::size_t>(symbol) + 1;
        if (needed > this->handlers.size()) {
            this->handlers.resize(std::max<std::size_t>(needed, this->handlers.size() * 2));
        }
        // Each where it was made, so a table grown by a primitive binding
        // another while it runs -- \newif making its switch -- moves only
        // the pointers, never the primitive that is running.
        this->handlers[symbol] = handler ? std::make_unique<const Handler>(std::move(handler)) : nullptr;
        if (symbol < this->lent.size()) this->lent[symbol] = none;
    }

    void Mouth::lend(const Symbol symbol, const Symbol source) {
        const Handler* handler = this->handler(source);
        if (!handler) return;
        const Symbol origin = this->primitive(source);
        this->bind(symbol, *handler);
        if (symbol >= this->lent.size()) this->lent.resize(static_cast<std::size_t>(symbol) + 1, none);
        this->lent[symbol] = origin;
    }

    Symbol Mouth::primitive(const Symbol symbol) const noexcept {
        return symbol < this->lent.size() && this->lent[symbol] != none ? this->lent[symbol] : symbol;
    }

    void Mouth::define(const Symbol symbol, Macro macro, const bool global) {
        const auto slot = static_cast<std::size_t>(symbol);

        // A body with no parameter reference can be injected as it stands,
        // which is decided once here rather than on every call.
        macro.literal = macro.parameters.empty() && macro.prefix.empty() &&
                        std::ranges::none_of(macro.body, [](const Token& token) {
                            return token.category == CatCodes::Category::Parameter;
                        });

        // The tables grow by doubling, so a document that defines a thousand
        // macros resizes them ten times rather than a thousand.
        if (slot >= this->macros.size()) {
            const std::size_t needed = std::max<std::size_t>(slot + 1, this->macros.size() * 2);
            this->macros.resize(needed);
            this->levels.resize(needed);
        }

        if (global) {
            this->levels[slot] = 0;
        } else if (const auto level = static_cast<std::uint32_t>(this->marks.size());
                   level != 0 && this->levels[slot] != level) {
            this->records.push_back(Record{symbol, this->macros[slot], this->levels[slot]});
            this->levels[slot] = level;
        }

        this->macros[slot] = std::make_shared<const Macro>(std::move(macro));
    }

    void Mouth::undefine(const Symbol symbol, const bool global) {
        const auto slot = static_cast<std::size_t>(symbol);
        if (slot >= this->macros.size() || !this->macros[slot]) return;

        if (global) {
            this->levels[slot] = 0;
        } else if (const auto level = static_cast<std::uint32_t>(this->marks.size());
                   level != 0 && this->levels[slot] != level) {
            this->records.push_back(Record{symbol, this->macros[slot], this->levels[slot]});
            this->levels[slot] = level;
        }

        this->macros[slot] = nullptr;
    }

    Token Mouth::lookahead(const std::size_t offset) const noexcept {
        return this->cursor.lookahead(offset);
    }

    const Mouth::Macro* Mouth::macro(const Symbol symbol) const noexcept {
        const auto slot = static_cast<std::size_t>(symbol);
        return slot < this->macros.size() ? this->macros[slot].get() : nullptr;
    }

    const Mouth::Handler* Mouth::handler(const Symbol symbol) const noexcept {
        const auto slot = static_cast<std::size_t>(symbol);
        return slot < this->handlers.size() ? this->handlers[slot].get() : nullptr;
    }

}
