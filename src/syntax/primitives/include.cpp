/// @file
/// @brief Include primitive: pulls embedded files, and packages of them, into
///        the token stream.
///
/// Every file is read the same way, through read(): the folder it sits in is
/// made the current one, its tokens go in front of everything else, and a
/// marker behind them leaves the folder again once they have all been read.
/// Everything else here is only deciding which file a name means.
#include "syntax/primitives/include.hpp"
#include "syntax/primitives/variables.hpp"
#include "syntax/argument.hpp"
#include "syntax/number.hpp"
#include "logger.hpp"

#include "syntax/modules.hpp"

#include <array>
#include <format>
#include <span>
#include <string>

namespace syntax::primitives {

    Include::Include(Lexicon& lexicon) noexcept {
        lexicon.intern("\\include");
        lexicon.intern("\\input");
        lexicon.intern("\\usepackage");
        lexicon.intern("\\requirepackage");
        lexicon.intern("\\ifpackageloaded");
        lexicon.intern("\\providepackage");

        // A name with a colon in it: the lexer ends a control word at the
        // colon, so no document can write this and trip it by accident.
        leave = lexicon.intern("\\include:leave");
    }

    bool Include::loaded(const std::string_view name) const {
        return packages.contains(name);
    }

    void Include::read(Mouth& mouth, const std::string_view key, const std::string_view text) const {
        const std::size_t cut = key.find_last_of('/');
        folders.emplace_back(cut == std::string_view::npos ? std::string_view{} : key.substr(0, cut));

        // The cursor is a stack, so the marker goes in first and the file in
        // front of it: the file is read, then the marker, then whatever came
        // after the \include.
        const Token marker{.symbol = leave, .category = Catcodes::Category::Escape,
                           .text = mouth.lexicon.resolve(leave)};
        mouth.stream().inject(std::span{&marker, 1});
        // One of the engine's own files stands on no line of the document,
        // and says so: what its macros report is reported where the
        // document's own tokens stand, or without a place, never at a line
        // of a file the document's writer has never seen.
        const auto embedded = modules::get(key);
        mouth.ingest(text, embedded && embedded->data() == text.data() ? std::optional{memory::Location{}}
                                                                       : std::nullopt);

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Reading '{}'", key);
    }

    std::vector<std::string> Include::split(const std::string_view list) {
        std::vector<std::string> found;
        std::size_t start = 0;
        while (start <= list.size()) {
            std::size_t stop = list.find(',', start);
            if (stop == std::string_view::npos) stop = list.size();
            std::string_view name = list.substr(start, stop - start);
            while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
            while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
            if (!name.empty()) found.emplace_back(name);
            start = stop + 1;
        }
        return found;
    }

    std::optional<std::string_view> Include::get(const Context& context, const std::string_view key) {
        if (context.files) {
            if (const auto file = context.files->find(key); file != context.files->end()) return file->second;
        }
        if (const auto module = modules::get(key)) return module;
        if (context.disk) {
            if (const std::string* file = context.disk(key)) return *file;
        }
        return std::nullopt;
    }

    void Include::load(Mouth& mouth, const Context& context, const std::string_view name,
                       const memory::Location origin) const {
        if (loaded(name)) return;

        const std::string key = std::string(name) + "/main.mtex";
        const auto found = get(context, key);

        if (!found) {
            // A package that is another under a newer or an older name --
            // xurl is url, soulutf8 is soul -- as the core names them: that
            // one, loaded, and this marked loaded beside it.
            packages.emplace(name);
            if (const std::string* other = context.variables.get("alias." + std::string(name)); other && *other != name) {
                load(mouth, context, *other, origin);
                return;
            }
            // Otherwise not a failure: marked loaded, as LaTeX would have
            // it, and the package's commands found in the glossary where it
            // knows them. What it does not know is reported where it is used.
            tracebacks.emplace_back(
                Traceback::Type::Warning, origin,
                std::format("File `{}.sty' not found; its commands are read as the glossary knows them", name));
            return;
        }

        // Marked loaded before it is read, so a package that asks for itself
        // -- or for one that asks for it back -- is read once, not forever.
        packages.emplace(name);
        read(mouth, key, *found);
    }

    void Include::operator()(Mouth& mouth, Context& context) const {
        mouth.bind(leave, [this](Mouth&) {
            if (!folders.empty()) folders.pop_back();
        });

        const auto including = [this, &context](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            std::string name = Argument::text(mouth);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());

            if (name.empty()) {
                tracebacks.emplace_back(Traceback::Type::Argument, origin,
                                         "\\include needs a file name");
                return;
            }

            // A .bib file is not TeX to read but entries to cite: input, it is
            // imported, a source of the bibliography as biblatex's
            // \addbibresource makes one, and read by whichever of
            // \bibliography and \printbibliography sets the list.
            const std::string folder = folders.empty() ? std::string{} : folders.back();
            if (name.ends_with(".bib")) {
                for (const std::string& candidate : {folder.empty() ? name : folder + "/" + name, name}) {
                    if (!get(context, candidate)) continue;
                    mouth.ingest(mouth.arena.copy("\\addbibresource{" + candidate + "}"), memory::Location{});
                    return;
                }
                tracebacks.emplace_back(Traceback::Type::Primitive, origin, std::format("File `{}' not found", name));
                return;
            }

            // Beside the file being read first, then from the top; each time
            // as written, then with an extension a bare name leaves off --
            // this engine's own, or LaTeX's, for a chapter written for it.
            std::array<std::string, 6> candidates{};
            std::size_t count = 0;
            if (!folder.empty()) {
                candidates[count++] = folder + "/" + name;
                candidates[count++] = folder + "/" + name + ".mtex";
                candidates[count++] = folder + "/" + name + ".tex";
            }
            candidates[count++] = name;
            candidates[count++] = name + ".mtex";
            candidates[count++] = name + ".tex";

            for (std::size_t index = 0; index < count; ++index) {
                if (const auto found = get(context, candidates[index])) {
                    read(mouth, candidates[index], *found);
                    return;
                }
            }

            // A folder is a package, and reading one is loading it.
            if (get(context, name + "/main.mtex")) {
                load(mouth, context, name, origin);
                return;
            }

            tracebacks.emplace_back(Traceback::Type::Primitive, origin,
                                     std::format("File `{}.tex' not found", name));
        };

        // Options are the package's to read: each `key=value` becomes the
        // variable `package.key`, and a bare word the variable `package.word`
        // holding `true` -- \setkeys's own rule, so a package reads what it
        // was given with \variable and \ifvariable, or all of them in order
        // as `package.options`.
        const auto requiring = [this, &context](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;

            std::string options;
            for (const Token& token : mouth.argument(Mouth::Parameter{.optional = true}, 0)) {
                options += token.text;
            }
            // Several at once, read in the order they were written. Each goes
            // in front of the stream as it loads, so the last one written has
            // to be loaded first for the first one to be read first.
            const std::vector<std::string> asked = split(Argument::text(mouth));

            if (asked.empty()) {
                tracebacks.emplace_back(Traceback::Type::Argument, origin,
                                         "\\usepackage needs a package name");
                return;
            }

            // The list as written is kept too, as `package.options`, for a
            // package that reads its options in order: babel's last language
            // is the one the document is in.
            for (const std::string& name : asked) {
                if (options.empty()) continue;
                context.variables.assign(name, options);
                context.variables.define(name + ".options", options);
            }
            for (auto name = asked.rbegin(); name != asked.rend(); ++name) {
                load(mouth, context, *name, origin);
            }
        };

        // A package whose whole job the engine does itself: marked loaded
        // with nothing read, so a document's \\usepackage for it -- and its
        // \\ifpackageloaded -- find it there. Its options are still kept as
        // its variables, as any package's are.
        const auto providing = [this](Mouth& mouth) {
            for (std::string& name : split(Argument::text(mouth))) packages.emplace(std::move(name));
        };

        const auto asking = [this](Mouth& mouth) {
            const std::string name = Argument::text(mouth);
            const std::vector<Token> present = mouth.argument({}, 1);
            const std::vector<Token> absent = mouth.argument({}, 1);

            const std::vector<Token>& chosen = loaded(name) ? present : absent;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        };

        // Whether a file could be read -- one handed in, or one of the
        // engine's own -- as written or with the extension a bare name
        // leaves off: LaTeX's \\IfFileExists.
        mouth.bind("\\iffile", [&context](Mouth& mouth) {
            const std::string name = Argument::text(mouth);
            const std::vector<Token> present = mouth.argument({}, 1);
            const std::vector<Token> absent = mouth.argument({}, 1);

            const bool found = !name.empty() && (get(context, name) || get(context, name + ".mtex") ||
                                                 get(context, name + "/main.mtex"));
            const std::vector<Token>& chosen = found ? present : absent;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        });

        // TeX's writing, to files and to the terminal. A run writes no file:
        // what would go to one is read and let go, and what goes to the
        // terminal -- \write16, \message, \typeout -- to the engine's log.
        // \immediate says when a write happens, which here is always now.
        mouth.bind("\\immediate", [](Mouth&) {});
        mouth.bind("\\write", [&context](Mouth& mouth) {
            const std::int32_t stream = Number::integer(mouth, context.registers).value_or(-1);
            const std::string text = Argument::expanded(mouth);
            if (stream < 0 || stream > 15) Logger::log(Logger::Type::Mouth, Logger::Level::Informative, "{}", text);
        });
        for (const std::string_view name : {"\\message", "\\typeout"}) {
            mouth.bind(name, [](Mouth& mouth) {
                Logger::log(Logger::Type::Mouth, Logger::Level::Informative, "{}", Argument::expanded(mouth));
            });
        }

        // \warning{text}: what a package says a document should hear about,
        // reported beside the errors without making the document fail --
        // \PackageWarning and \ClassWarning are built on it.
        mouth.bind("\\warning", [this](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            tracebacks.emplace_back(Traceback::Type::Warning, origin, Argument::expanded(mouth));
        });
        mouth.bind("\\openout", [&context](Mouth& mouth) {
            static_cast<void>(Number::integer(mouth, context.registers));
            if (mouth.lookahead().is('=')) mouth.read();
            // The name, to the space or the \relax that ends it.
            for (Token token = mouth.lookahead(); !token.empty() && token.category != Catcodes::Category::Space &&
                                                  token.category != Catcodes::Category::Escape;
                 token = mouth.lookahead()) {
                mouth.read();
            }
        });
        mouth.bind("\\closeout", [&context](Mouth& mouth) {
            static_cast<void>(Number::integer(mouth, context.registers));
        });
        mouth.bind("\\newwrite", [](Mouth& mouth) { static_cast<void>(mouth.read()); });

        // \endinput: the rest of the file being read is left unread -- up to
        // the mark its reading left after it, which is put back, so the
        // folder it was read from is left as it would have been.
        mouth.bind("\\endinput", [this](Mouth& mouth) {
            for (Token token = mouth.read(); !token.empty(); token = mouth.read()) {
                if (token.symbol != leave) continue;
                mouth.stream().inject(std::span{&token, 1});
                return;
            }
        });

        mouth.bind("\\include", including);
        mouth.bind("\\input", including);
        mouth.bind("\\usepackage", requiring);
        mouth.bind("\\requirepackage", requiring);
        mouth.bind("\\ifpackageloaded", asking);
        mouth.bind("\\providepackage", providing);

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Bound the include primitives");
    }

}
