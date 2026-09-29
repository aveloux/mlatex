/// @file
/// @brief Block structure primitives: `\\begin` and `\\end`.
///
/// One handler serves both spellings. `\\enter` and `\\leave` are bound to the
/// same closures as `\\begin` and `\\end`, so the two are the same primitive
/// under two names rather than two implementations to keep in step.
#include "syntax/primitives/blocks.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <array>
#include <span>

#include <format>
#include <string>
#include <utility>

namespace syntax::primitives {

    Blocks::Blocks(Lexicon& lexicon) noexcept : lexicon(&lexicon) {}

    Symbol Blocks::title(Mouth& mouth) const {
        const std::string joined = Argument::text(mouth);
        if (joined.empty()) return none;
        return mouth.lexicon().intern(joined);
    }

    std::string_view Blocks::innermost() const noexcept {
        if (open.empty() || !lexicon) return {};
        return lexicon->resolve(open.back());
    }

    void Blocks::watch(const std::string_view name, Handler entering, Handler leaving,
                      const bool transparent) const {
        if (!lexicon || name.empty()) return;

        // Indexed by the interned name, so dispatch is one bounds check and
        // one load however many blocks are being watched.
        const auto slot = static_cast<std::size_t>(lexicon->intern(name));
        if (slot >= hooks.size()) hooks.resize(slot + 1);

        Hook& hook = hooks[slot];
        if (!hook.entering && !hook.leaving) {
            hook = Hook{.entering = std::move(entering), .leaving = std::move(leaving), .transparent = transparent};
            return;
        }

        // A block another module already watches -- the document itself is
        // watched for its hooks and for its endnotes -- keeps both. Openings
        // run in the order they were asked for; closings the other way round,
        // and since each puts what it has in front of what is read next, the
        // first one asked for is the first one read either way.
        hook.entering = [earlier = std::move(hook.entering), later = std::move(entering)](Mouth& mouth) {
            if (earlier) earlier(mouth);
            if (later) later(mouth);
        };
        hook.leaving = [earlier = std::move(hook.leaving), later = std::move(leaving)](Mouth& mouth) {
            if (later) later(mouth);
            if (earlier) earlier(mouth);
        };
        hook.transparent = hook.transparent || transparent;
    }

    void Blocks::watch(Handler entering, Handler leaving) const {
        every.entering = std::move(entering);
        every.leaving = std::move(leaving);
    }

    void Blocks::operator()(Mouth& mouth, Context&) const {
        // A name as one token, and a character as one: what a definition
        // built here is written in.
        const auto word = [](Mouth& mouth, const std::string& name) {
            const Symbol symbol = mouth.lexicon().intern(name);
            return Token{.symbol = symbol, .category = CatCodes::Category::Escape, .text = mouth.lexicon().resolve(symbol)};
        };
        const auto brace = [](Mouth& mouth, const char written) {
            const Symbol symbol = mouth.lexicon().intern(std::string_view(&written, 1));
            return Token{.symbol = symbol, .category = CatCodes::Category::Group, .text = mouth.lexicon().resolve(symbol)};
        };

        const Mouth::Handler entering = [this, word](Mouth& mouth) {
            const Symbol name = title(mouth);
            if (name == none) {
                tracebacks_.emplace_back(Traceback::Type::Environment,
                                         mouth.lookahead().location,
                                         "\\begin needs a block name");
                return;
            }

            open.push_back(name);

            const auto slot = static_cast<std::size_t>(name);
            const bool transparent = slot < hooks.size() && hooks[slot].transparent && !hooks[slot].own;
            if (!transparent) mouth.push(semantics::Scope::Type::Environment);

            // After the scope opens, so a hook that defines something has it
            // undone when the block closes. A transparent block opens none of
            // its own, and manages whatever scope it needs itself.
            if (every.entering) every.entering(mouth);
            if (slot < hooks.size() && hooks[slot].own) {
                const Token code = word(mouth, "\\" + std::string(mouth.lexicon().resolve(name)) + ":open");
                mouth.stream().inject(std::span{&code, 1});
            } else if (slot < hooks.size() && hooks[slot].entering) {
                hooks[slot].entering(mouth);
            } else if (!(slot < hooks.size() && hooks[slot].leaving)) {
                // LaTeX's own way with a block of no hooks: \begin{name} is
                // \name -- a declaration, `\begin{small}`, or a command a
                // document or a package defined -- and a name meaning nothing
                // is reported, and set as a group.
                const Token call = word(mouth, "\\" + std::string(mouth.lexicon().resolve(name)));
                if (mouth.known(call.symbol) || (mouth.reader.means && mouth.reader.means(call.symbol))) {
                    mouth.stream().inject(std::span{&call, 1});
                } else {
                    tracebacks_.emplace_back(Traceback::Type::Warning, mouth.lookahead().location,
                                             std::format("Environment {} undefined", mouth.lexicon().resolve(name)));
                }
            }
        };

        // A block closed, once whatever its end code opened has been.
        const auto close = [this](Mouth& mouth, const Symbol name) {
            if (open.empty()) {
                tracebacks_.emplace_back(Traceback::Type::Environment,
                                         mouth.lookahead().location,
                                         std::format("Extra \\end{{{}}}", mouth.lexicon().resolve(name)));
                return;
            }

            if (open.back() != name) {
                tracebacks_.emplace_back(
                    Traceback::Type::Environment, mouth.lookahead().location,
                    std::format("\\begin{{{}}} ended by \\end{{{}}}",
                                mouth.lexicon().resolve(open.back()),
                                mouth.lexicon().resolve(name)));
                // Still closed: leaving the scope open would cascade every
                // later \end into the same complaint.
            }

            const auto slot = static_cast<std::size_t>(open.back());
            const bool own = slot < hooks.size() && hooks[slot].own;
            const bool transparent = slot < hooks.size() && hooks[slot].transparent && !own;

            // Before the scope closes, so a hook still sees whatever the block
            // established inside it.
            if (slot < hooks.size() && hooks[slot].leaving && !own) {
                hooks[slot].leaving(mouth);
            }
            if (every.leaving) every.leaving(mouth);

            open.pop_back();
            if (!transparent) mouth.pop(semantics::Scope::Type::Environment);
        };

        const Mouth::Handler leaving = [this, word, close](Mouth& mouth) {
            const Symbol name = title(mouth);
            if (name == none) {
                tracebacks_.emplace_back(Traceback::Type::Environment,
                                         mouth.lookahead().location,
                                         "\\end needs a block name");
                return;
            }

            // A document's own block: its end code first, then the close --
            // and a block of no hooks the same way, its \endname when it has
            // one.
            const auto slot = static_cast<std::size_t>(name);
            if (slot < hooks.size() && hooks[slot].own) {
                closing.push_back(name);
                const std::array<Token, 2> code{word(mouth, "\\" + std::string(mouth.lexicon().resolve(name)) + ":close"),
                                                word(mouth, "\\end:finish")};
                mouth.stream().inject(std::span{code});
                return;
            }
            if (!(slot < hooks.size() && (hooks[slot].entering || hooks[slot].leaving))) {
                const Token ending = word(mouth, "\\end" + std::string(mouth.lexicon().resolve(name)));
                if (mouth.known(ending.symbol) || (mouth.reader.means && mouth.reader.means(ending.symbol))) {
                    closing.push_back(name);
                    const std::array<Token, 2> code{ending, word(mouth, "\\end:finish")};
                    mouth.stream().inject(std::span{code});
                    return;
                }
            }
            close(mouth, name);
        };
        mouth.bind("\\end:finish", [this, close](Mouth& mouth) {
            if (closing.empty()) return;
            const Symbol name = closing.back();
            closing.pop_back();
            close(mouth, name);
        });

        // \\newenvironment and its kin: the block's two macros defined by the
        // definitions a document would write for them, as tokens -- its
        // parameters as \\newcommand reads them, or xparse's letters -- and the
        // block marked the document's own.
        const auto defining = [this, word, brace](Mouth& mouth, const bool renewing, const bool providing,
                                                   const bool described) {
            if (mouth.lookahead().is('*')) mouth.read();
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = Argument::text(mouth);

            // What stands between the name and the begin code: `[1][Note]`,
            // or xparse's `{O{} m}`.
            std::vector<Token> parameters;
            if (described) {
                parameters = mouth.argument({}, 1);
            } else {
                while (!mouth.lookahead().empty() && !mouth.lookahead().is(CatCodes::Category::Group, '{')) {
                    parameters.push_back(mouth.read());
                }
            }
            const std::vector<Token> begin = mouth.argument({}, 1);
            const std::vector<Token> end = mouth.argument({}, 1);
            if (name.empty()) {
                tracebacks_.emplace_back(Traceback::Type::Environment, origin, "\\newenvironment needs a block name");
                return;
            }

            const auto slot = static_cast<std::size_t>(mouth.lexicon().intern(name));
            if (slot >= hooks.size()) hooks.resize(slot + 1);
            Hook& hook = hooks[slot];
            const bool native = !hook.own && (hook.entering || hook.leaving);
            if (providing && (hook.own || native)) return;
            if (renewing && native) {
                tracebacks_.emplace_back(Traceback::Type::Warning, origin,
                                         "renewenvironment " + name + ": the engine sets this block itself, and still does");
                return;
            }
            hook.own = true;

            std::vector<Token> written;
            written.push_back(word(mouth, "\\@spanning"));
            written.push_back(word(mouth, described ? "\\@declare" : "\\@define"));
            written.push_back(word(mouth, "\\" + name + ":open"));
            if (described) written.push_back(brace(mouth, '{'));
            written.insert(written.end(), parameters.begin(), parameters.end());
            if (described) written.push_back(brace(mouth, '}'));
            written.push_back(brace(mouth, '{'));
            written.insert(written.end(), begin.begin(), begin.end());
            written.push_back(brace(mouth, '}'));
            written.push_back(word(mouth, "\\@define"));
            written.push_back(word(mouth, "\\" + name + ":close"));
            written.push_back(brace(mouth, '{'));
            written.insert(written.end(), end.begin(), end.end());
            written.push_back(brace(mouth, '}'));
            if (described) written.erase(written.begin());
            mouth.stream().inject(std::span{written});
        };
        mouth.bind("\\newenvironment", [defining](Mouth& mouth) { defining(mouth, false, false, false); });
        mouth.bind("\\renewenvironment", [defining](Mouth& mouth) { defining(mouth, true, false, false); });
        mouth.bind("\\provideenvironment", [defining](Mouth& mouth) { defining(mouth, false, true, false); });
        mouth.bind("\\NewDocumentEnvironment", [defining](Mouth& mouth) { defining(mouth, false, false, true); });
        mouth.bind("\\RenewDocumentEnvironment", [defining](Mouth& mouth) { defining(mouth, true, false, true); });
        mouth.bind("\\ProvideDocumentEnvironment", [defining](Mouth& mouth) { defining(mouth, false, true, true); });
        mouth.bind("\\DeclareDocumentEnvironment", [defining](Mouth& mouth) { defining(mouth, false, false, true); });

        mouth.bind("\\begin", entering);
        mouth.bind("\\end", leaving);

        // The same primitive under a plainer name.
        mouth.bind("\\enter", entering);
        mouth.bind("\\leave", leaving);

        Logger::log(Logger::Type::Semantics, Logger::Level::Debug, "Bound block primitives");
    }

}
