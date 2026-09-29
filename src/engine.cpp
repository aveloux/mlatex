/// @file
/// @brief Engine implementation: the pipeline `main()` runs.
///
/// The order of the wiring is most of what this file says. Three of the
/// dependencies run the other way from the reading order and each of them
/// would be a silent misbehaviour rather than a compile error:
///
/// - The cursor is a stack, so the buffer ingested **last** is read **first**.
///   The document is ingested before the prelude it depends on.
/// - syntax::primitives::Context holds a reference to the conditional module,
///   which the syntax Wrapper owns, so the Wrapper is built first.
/// - render::primitives::Context holds a reference to the block module, which
///   the syntax Wrapper also owns, so the syntax layer is installed first.
#include "engine.hpp"
#include "layout/document.hpp"
#include "layout/typesetter.hpp"
#include "memory/arena.hpp"
#include "syntax/modules.hpp"
#include "render/composer.hpp"
#include "render/pdf.hpp"
#include "render/primitives/context.hpp"
#include "render/primitives/wrapper.hpp"
#include "syntax/argument.hpp"
#include "syntax/cursor.hpp"
#include "syntax/expression/unicodes.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/wrapper.hpp"
#include "syntax/semantics/union.hpp"
#include "syntax/tokens.hpp"
#include "typography/hyphenator.hpp"
#include "typography/library.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace engine {

    // The font tree as the build listed it, so a run need not walk it.
#include "fonts.inc"

    /// @brief The pipeline itself, whichever way the document and its
    ///        reports arrive.
    /// @param assets      The engine's own assets directory.
    /// @param source      The document to read, or null when @p text is the document.
    /// @param text        The document's text, when @p source is null.
    /// @param destination Where to write the PDF, or null when @p pdf is to hold it.
    /// @param pdf         Receives the PDF, when @p destination is null.
    /// @param texts       Receives each page's text, or null for none.
    /// @param host        Values, commands and files the calling program hands in.
    /// @param report      Where the statistics and timings go, or null for nowhere.
    /// @param errors      Where every error goes.
    /// @param folios      The page each anchor landed on in a first pass, when
    ///                    this is the second; null on a first.
    /// @return True when the document compiled with nothing left broken.
    static bool run(
        const std::filesystem::path& assets,
        const std::filesystem::path* source,
        const std::string_view text,
        const std::filesystem::path* destination,
        std::string* pdf,
        std::vector<std::string>* texts,
        const Host& host,
        std::ostream* report,
        std::ostream& errors,
        const std::vector<std::string>* folios = nullptr
    ) {
        const auto started = std::chrono::high_resolution_clock::now();

        // One arena for everything that outlives a pass, one for what does
        // not. Font files land in the first, so it is sized for them.
        memory::Arena arena(16 * 1024 * 1024);
        memory::Arena scratch(4 * 1024 * 1024);

        if (source && !std::filesystem::exists(*source)) {
            errors << "Document missing at: " << source->string() << '\n';
            return false;
        }

        // The font tree is indexed, not parsed: this records where each file
        // is, from the listing the build took of the tree, and the first face
        // a document actually asks for is the first one read.
        render::typography::Library library(arena);
        const std::size_t indexed = library.survey((assets / "fonts").string(), faces);

        // Two roles, pointed at concrete faces. A document may change either
        // with `\\textfont` or `\\mathfont`; these are only where it starts.
        library.alias("text", "lmroman10-regular");
        library.alias("expression", "NewCMMath-Regular");

        // The faces nearly every document sets in besides those two -- Latin
        // Modern's italic and bold, its sizes for headings, scripts and notes,
        // its typewriter -- opened on another thread while this one builds the
        // fonts, wires the primitives and reads the document. Opening one is a
        // file, a mapping and the pages under it, and none of that has to wait
        // for the document to ask; a byte of each page is read so the page is
        // in when the shaper wants it. A face nobody asks for costs only the
        // other thread's time.
        static constexpr std::array<std::string_view, 10> likely{
            "lmroman10-italic", "lmroman10-bold", "lmroman12-regular", "lmroman17-regular", "lmroman7-regular",
            "lmroman5-regular", "lmroman8-regular", "lmroman9-regular", "lmroman10-bolditalic", "lmmono10-regular",
        };
        const auto warm = [&library] {
            for (const std::string_view family : likely) {
                const render::typography::Library::Entry* entry = library.read(family);
                if (!entry) continue;
                std::uint8_t touched = 0;
                for (std::size_t at = 0; at < entry->bytes.size(); at += 4096) touched ^= entry->bytes[at];
                static_cast<void>(*static_cast<volatile std::uint8_t*>(&touched));
            }
        };
    #if defined(_WIN32)
        // The process's thread pool, as the PDF writer uses it: a worker is
        // usually alive already, and handing it work costs microseconds.
        struct Worker {
            PTP_WORK work{nullptr};
            ~Worker() {
                if (!work) return;
                WaitForThreadpoolWorkCallbacks(work, FALSE);
                CloseThreadpoolWork(work);
            }
        };
        const Worker worker{CreateThreadpoolWork(
            [](PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) { (*static_cast<const decltype(warm)*>(context))(); },
            const_cast<void*>(static_cast<const void*>(&warm)), nullptr)};
        if (worker.work) SubmitThreadpoolWork(worker.work);
    #elif defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        // No thread to spare: each face opens when it is first asked for.
    #else
        const std::jthread worker(warm);
    #endif

        const auto surveyed = std::chrono::high_resolution_clock::now();

        render::typography::Registry registry(arena, library);

        constexpr float body = 12.0f;
        const render::typography::Font* prose = registry.get({.family = "text", .size = body});
        const render::typography::Font* maths = registry.get({.family = "expression", .size = body});

        if (!prose) {
            errors << "No text face available; looked under " << (assets / "fonts").string() << '\n';
            return false;
        }
        if (!maths) {
            // Not fatal: formulas then set in the text face, which will be
            // missing the Greek and the maths table, but the document still
            // comes out.
            errors << "Warning: no maths face available; formulas will use the text face\n";
        }

        const auto typeset = std::chrono::high_resolution_clock::now();

        const render::typography::Shaper shaper(arena, &registry);
        render::layout::Typesetter typesetter(arena, registry, shaper);
        render::Composer composer(arena, scratch, shaper, typesetter);

        // Hyphenation is optional: a missing pattern file means words are not
        // broken, not that the run fails. The language is normally compiled
        // into the engine, so its patterns are read from memory rather than
        // from disk; a build that left it out reads the file instead.
        const render::typography::Hyphenator hyphenator;
        const std::filesystem::path language = assets / "hyphens" / "hyph-en-us.pat.txt";
        std::size_t patterns = hyphenator.embed(language.filename().string());
        if (patterns == 0) patterns = hyphenator.compose(language.string());
        composer.document.hyphenate(&hyphenator);

        const auto hyphenated = std::chrono::high_resolution_clock::now();

        syntax::Lexicon lexicon(arena);
        syntax::semantics::Union state{};
        const syntax::expression::Unicodes unicodes;

        syntax::Mouth mouth(syntax::Cursor(std::vector<syntax::Token>{}), state, lexicon, arena);
        syntax::Parser parser(mouth, arena);

        // Context holds a reference to the conditional module, so the
        // Wrapper has to exist first. One Context is shared by every module,
        // which is how two modules end up reading the same register bank.
        // The files beside a document read from disk, each read the first
        // time it is named and kept for the rest of the run. A name is read
        // only from inside the document's own folder: one written from a
        // root, or climbing out through `..`, is refused, and a document
        // that came from memory has no folder to read from at all -- only
        // the files it wrote for itself, which are kept here too.
        syntax::primitives::Files nearby;
        const std::filesystem::path folder = source ? source->parent_path() : std::filesystem::path{};
        const syntax::primitives::Writer write = [&nearby](const std::string_view name, const std::string_view text) {
            nearby.insert_or_assign(std::string(name), std::string(text));
        };
        syntax::primitives::Reader disk = [&nearby](const std::string_view name) -> const std::string* {
            const auto kept = nearby.find(name);
            return kept == nearby.end() || kept->second.empty() ? nullptr : &kept->second;
        };
        if (source) {
            disk = [&nearby, &folder](const std::string_view name) -> const std::string* {
                if (const auto kept = nearby.find(name); kept != nearby.end()) {
                    return kept->second.empty() ? nullptr : &kept->second;
                }
                const std::filesystem::path relative(
                    std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size()));
                bool inside = !name.empty() && !relative.has_root_name() && !relative.has_root_directory();
                for (const std::filesystem::path& part : relative) inside = inside && part != "..";

                std::string bytes;
                if (inside) {
                    if (std::ifstream file(folder / relative, std::ios::binary | std::ios::ate); file) {
                        const std::streamsize size = file.tellg();
                        bytes.resize(static_cast<std::size_t>(std::max<std::streamsize>(size, 0)));
                        file.seekg(0, std::ios::beg);
                        if (size > 0 && !file.read(bytes.data(), size)) bytes.clear();
                    }
                }
                // Kept whether it was found or not, so a name asked for twice
                // looks at the disk once; an empty file reads as none.
                const auto [kept, added] = nearby.emplace(std::string(name), std::move(bytes));
                return kept->second.empty() ? nullptr : &kept->second;
            };
        }

        syntax::primitives::Wrapper core(lexicon);
        syntax::primitives::Context expansion{state.registers, core.relay, core.variables};
        expansion.files = &host.files;
        expansion.disk = disk;
        core(mouth, expansion);

        // Whatever the caller handed in, set before the first token is read,
        // so the document and every package see it from their first line.
        for (const auto& [name, value] : host.variables) core.variables.define(name, value);

        // The calling program's own commands, bound beside the primitives
        // and read the way a primitive reads: arguments expanded, handed
        // over as text, and the answer read in the command's place. The
        // answer is copied into the arena first, because a token's text is a
        // view and the string it came back in is gone once this returns.
        for (const Command& command : host.commands) {
            std::string_view name = command.name;
            if (name.starts_with('\\')) name.remove_prefix(1);
            if (name.empty() || !command.handler) continue;

            mouth.bind("\\" + std::string(name), [&command](syntax::Mouth& mouth) {
                std::vector<std::string> arguments;
                arguments.reserve(command.arity);
                for (std::size_t index = 0; index < command.arity; ++index) {
                    arguments.push_back(syntax::Argument::expanded(mouth));
                }

                const std::string answer = command.handler(arguments);
                if (!answer.empty()) mouth.ingest(mouth.arena.copy(answer));
            });
        }

        render::primitives::Selection selection;
        selection.text(prose);
        selection.formula(maths ? maths : prose);

        // An em is the body face's size, which is what `1em` means to the
        // expander from here on.
        state.registers.quad = static_cast<std::int32_t>(prose->size() * 65536.0f);

        // The render layer's Context names the block module the syntax layer
        // owns, which is how a list environment hooks `\\begin` without
        // either layer having to know about the other's modules.
        const render::primitives::Wrapper visuals(lexicon);
        render::primitives::Context rendering{
            composer.document, typesetter, state.registers, registry, library,
            shaper, unicodes, core.blocks, core.variables, arena, selection
        };
        rendering.files = &host.files;
        rendering.disk = disk;
        rendering.write = write;
        rendering.folios = folios;
        rendering.patterns = (assets / "hyphens").string();
        visuals(parser, rendering);

        const auto wired = std::chrono::high_resolution_clock::now();

        // The document, whole. On Windows straight from the system: one
        // open, one read, into the string kept -- a stream would set up a
        // locale and a buffer of its own to copy the same bytes through --
        // shared the way a stream shares it, so a document an editor has open
        // still reads.
        std::string content;
        if (!source) {
            content = text;
        } else {
    #if defined(_WIN32)
            const HANDLE file = CreateFileW(source->c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                if (LARGE_INTEGER size{}; GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= MAXDWORD) {
                    content.resize(static_cast<std::size_t>(size.QuadPart));
                    DWORD read = 0;
                    if (!ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr) ||
                        read != content.size()) {
                        content.clear();
                    }
                }
                CloseHandle(file);
            }
    #else
            if (std::ifstream file(*source, std::ios::binary | std::ios::ate); file) {
                if (const std::streamsize size = file.tellg(); size > 0) {
                    file.seekg(0, std::ios::beg);
                    content.resize(static_cast<std::size_t>(size));
                    if (!file.read(content.data(), size)) content.clear();
                }
            }
    #endif
        }
        if (content.empty()) {
            errors << "Document unreadable or empty: " << (source ? source->string() : "(text)") << '\n';
            return false;
        }

        const auto loaded = std::chrono::high_resolution_clock::now();

        const auto prelude = syntax::modules::get("main.mtex");
        if (!prelude) {
            errors << "Embedded module 'main.mtex' not found\n";
            return false;
        }

        const auto found = std::chrono::high_resolution_clock::now();

        // The cursor is a stack: ingested first, read last, so the document
        // goes in before the prelude it is written against. main.mtex is the
        // only prelude file ingested here; it reaches every other topic
        // itself with \include, in whatever order suits their own
        // dependencies, so a new topic file is a line in main.mtex and not a
        // change here.
        mouth.ingest(content);
        // Between the two, \jobname: the name of the file being set, without
        // its extension, as LaTeX takes it -- what finds the .bbl BibTeX
        // wrote beside it.
        if (source) {
            mouth.ingest(mouth.arena.copy("\\@define\\jobname{" + source->stem().string() + "}"), memory::Location{});
        }
        mouth.ingest(*prelude, memory::Location{});

        const auto ingested = std::chrono::high_resolution_clock::now();

        const memory::Slice<syntax::Node*> outputs = parser.parse(0);
        const auto parsed = std::chrono::high_resolution_clock::now();

        // A page asked for -- a `\\pageref`, a table of contents -- is known
        // only once the pages are, after the text that asks has been set. So
        // this pass is set and drawn only to find where each anchor landed,
        // and the document read again with that, as LaTeX's second run reads
        // what its first wrote down; what it reports is the second's to say.
        const bool again = rendering.paged && !folios;

        // Four independent sources, because each layer keeps its own list:
        // the parser's, the expander's, and one per primitive layer -- which
        // each Wrapper has already merged across its own modules. One
        // reporter, called once per source, rather than the same loop
        // written out four times.
        bool broken = false;
        const auto collect = [&broken, &errors](const std::vector<syntax::Traceback>& faults) {
            for (const syntax::Traceback& fault : faults) {
                errors << fault.format() << '\n';
                broken = broken || fault.fatal();
            }
        };
        if (!again) {
            collect(parser.traceback());
            collect(mouth.traceback());
            collect(core.traceback());
            collect(visuals.traceback());
        }

        if (const std::size_t unclosed = core.blocks.depth(); unclosed != 0 && !again) {
            const std::string_view innermost = core.blocks.innermost();
            errors << (innermost == "document" ? std::string("*** (job aborted, no legal \\end found)")
                                               : std::format("error (environment): \\begin{{{}}} ended by \\end{{document}}",
                                                             innermost))
                   << '\n';
            broken = true;
        }

        const auto checked = std::chrono::high_resolution_clock::now();

        // The parse produced a flat list of what the document said; handing
        // it to the document decides what belongs together. Text and
        // formulas use the face selected for their own role, so a
        // `\\textfont` part way through is honoured from that point on.
        // Recursive, because a brace group is a boundary around nodes rather
        // than a node in its own right: what it holds goes in where it stood.
        const auto gather = [&composer, &selection](
            auto&& again, const memory::Slice<syntax::Node*> nodes) -> void {
            for (std::size_t index = 0; index < nodes.count; ++index) {
                const syntax::Node* node = nodes[index];
                if (!node) continue;

                // The face a style or a group recorded when the text was
                // read, and whatever is selected now for text that was never
                // claimed.
                const auto* face = static_cast<const render::typography::Font*>(node->face);

                switch (node->type) {
                    case syntax::Node::Type::Group:
                        again(again, node->nodes);
                        break;
                    case syntax::Node::Type::Text:
                        if (face ? face : selection.text()) {
                            composer.document.append(node->value, face ? *face : *selection.text(), body,
                                                       static_cast<const render::layout::Node::Color*>(node->tint));
                        }
                        break;
                    case syntax::Node::Type::Expression:
                        if (face ? face : selection.formula()) {
                            composer.document.append(node->expression,
                                                       face ? *face : *selection.formula(),
                                                       static_cast<const render::layout::Node::Color*>(node->tint));
                        }
                        break;
                    case syntax::Node::Type::Directive:
                        // `display` is the primitive saying whether its box
                        // stands on its own or belongs in the line it was
                        // written in.
                        composer.document.append(
                            static_cast<render::layout::Node*>(node->directive), node->display);
                        break;
                    case syntax::Node::Type::Paragraph:
                        composer.document.separate();
                        break;
                    default:
                        break;
                }
            }
        };

        // The language chosen last while the document was read was told to
        // the document then, for the boxes; its text goes in from the start,
        // where the language is the one the run began with.
        composer.document.hyphenate(&hyphenator);
        gather(gather, outputs);

        // What the file says of itself, as hyperref's \hypersetup gave it, and
        // whether it keeps to PDF/A-2b: pdfx's `a-2b` and its kin, or
        // LaTeX's `\DocumentMetadata{pdfstandard=A-2b}`.
        {
            using Metadata = render::layout::Document::Metadata;
            Metadata& about = composer.document.metadata;
            for (const auto& [key, field] : {std::pair{"hyperref.pdftitle", &Metadata::title},
                                             std::pair{"hyperref.pdfauthor", &Metadata::author},
                                             std::pair{"hyperref.pdfsubject", &Metadata::subject},
                                             std::pair{"hyperref.pdfkeywords", &Metadata::keywords}}) {
                if (const std::string* value = core.variables.get(key)) about.*field = *value;
            }
            const std::string* standard = core.variables.get("metadata.pdfstandard");
            about.archival = (standard && (standard->contains('a') || standard->contains('A'))) ||
                             std::ranges::any_of(std::array{"pdfx.a-1b", "pdfx.a-2b", "pdfx.a-2u", "pdfx.a-3b", "pdfx.a-3u"},
                                                 [&core](const char* key) { return core.variables.get(key) != nullptr; });
        }

        const auto composed = std::chrono::high_resolution_clock::now();

        // Laid out and written in two steps rather than one, so the two
        // costs can be told apart: line breaking and pagination are the
        // engine's, and the rest belongs to the PDF writer and the fonts it
        // embeds.
        const memory::Slice<render::layout::Pager::Page> pages =
            typesetter.compose(composer.document);
        const auto laid = std::chrono::high_resolution_clock::now();

        if (again) {
            const render::layout::Document::Configuration& page = composer.document.configuration;
            for (const render::layout::Pager::Page& sheet : pages) {
                static_cast<void>(composer.draw(sheet.nodes, sheet.notes, page.left, page.top));
            }
            return run(assets, source, text, destination, pdf, texts, host, report, errors, &composer.anchors);
        }

        if (destination) {
            if (!render::Pdf::compose(composer, pages, destination->string())) {
                errors << "Failed to write PDF to: " << destination->string() << '\n';
                return false;
            }
        } else if (pdf) {
            *pdf = render::Pdf::render(composer, pages);
            if (pdf->empty()) {
                errors << "Failed to make a PDF: the document has no pages\n";
                return false;
            }
        }
        if (texts) *texts = composer.texts;

        const auto finished = std::chrono::high_resolution_clock::now();

        const auto span = [](const auto& from, const auto& to) {
            return std::chrono::duration<double, std::micro>(to - from).count();
        };

        if (!report) return !broken;

        std::ostream& out = *report;
        const render::layout::Document::Configuration& page = composer.document.configuration;

        out << std::fixed << std::setprecision(1);
        out << "Pipeline\n";
        out << "  Font files indexed      : " << indexed << '\n';
        out << "  Font bytes resident     : " << library.resident() << '\n';
        out << "  Fonts built             : " << registry.count() << '\n';
        out << "  Faces opened            : " << registry.opened() << '\n';
        out << "  Faces embedded          : " << composer.faces.size() << '\n';
        out << "  Images embedded         : " << composer.pictures.size() << '\n';
        out << "  Hyphenation patterns    : " << patterns << '\n';
        out << "  Top-level nodes         : " << outputs.count << '\n';
        out << "  Document blocks         : " << composer.document.count() << '\n';
        out << "  Pages                   : " << pages.count << '\n';
        out << "  Page size               : " << page.width << " by " << page.height << " pt\n";
        out << "  Output file             : " << (destination ? destination->string() : "(memory)") << '\n';
        out << '\n';

        out << std::setprecision(4);
        out << "Timing\n";
        out << "  Font index              : " << span(started, surveyed) << " us\n";
        out << "  Font build              : " << span(surveyed, typeset) << " us\n";
        out << "  Hyphenation patterns    : " << span(typeset, hyphenated) << " us\n";
        out << "  Primitives wiring       : " << span(hyphenated, wired) << " us\n";
        out << "  Document read           : " << span(wired, loaded) << " us\n";
        out << "  Prelude lookup          : " << span(loaded, found) << " us\n";
        out << "  Token ingestion         : " << span(found, ingested) << " us\n";
        out << "  Parsing                 : " << span(ingested, parsed) << " us\n";
        out << "  Traceback collection    : " << span(parsed, checked) << " us\n";
        out << "  Tree composition        : " << span(checked, composed) << " us\n";
        out << "  Line breaking and paging: " << span(composed, laid) << " us\n";
        out << "  PDF output              : " << span(laid, finished) << " us\n";
        out << "  Total                   : " << span(started, finished) << " us\n";

        return !broken;
    }

    /// @brief A command's name as it is kept: without its backslash.
    [[nodiscard]] static std::string_view bare(std::string_view name) noexcept {
        if (name.starts_with('\\')) name.remove_prefix(1);
        return name;
    }

    Session::Session(std::filesystem::path assets) : assets(std::move(assets)) {}

    void Session::set(const std::string_view name, const std::string_view value) {
        const auto known = std::ranges::find(host.variables, name, &Variables::value_type::first);
        if (known != host.variables.end()) {
            known->second = value;
        } else {
            host.variables.emplace_back(name, value);
        }
    }

    void Session::unset(const std::string_view name) {
        std::erase_if(host.variables, [name](const auto& pair) { return pair.first == name; });
    }

    void Session::define(Command command) {
        command.name = std::string(bare(command.name));
        const auto known = std::ranges::find(host.commands, command.name, &Command::name);
        if (known != host.commands.end()) {
            *known = std::move(command);
        } else {
            host.commands.push_back(std::move(command));
        }
    }

    void Session::forget(const std::string_view name) {
        std::erase_if(host.commands, [name = bare(name)](const Command& command) { return command.name == name; });
    }

    void Session::provide(const std::string_view name, std::string bytes) {
        host.files.insert_or_assign(std::string(name), std::move(bytes));
    }

    void Session::withdraw(const std::string_view name) {
        if (const auto known = host.files.find(name); known != host.files.end()) host.files.erase(known);
    }

    bool Session::typeset(const std::string_view document) {
        std::ostringstream errors;
        const bool made = engine::typeset(assets, document, pdf, host, errors, &pages);
        error = errors.str();
        return made;
    }

    std::filesystem::path locate(const std::filesystem::path& binary) {
        std::error_code failure;

        for (std::filesystem::path step = binary.parent_path();
             !step.empty() && step != step.root_path(); step = step.parent_path()) {
            if (std::filesystem::path assets = step / "assets";
                std::filesystem::is_directory(assets / "fonts", failure) && !failure) {
                return assets;
            }
        }
        return {};
    }

    bool compose(
        const std::filesystem::path& assets,
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const Host& host
    ) {
        return run(assets, &source, {}, &destination, nullptr, nullptr, host, &std::cout, std::cerr);
    }

    bool typeset(
        const std::filesystem::path& assets,
        const std::string_view document,
        std::string& pdf,
        const Host& host,
        std::ostream& errors,
        std::vector<std::string>* texts
    ) {
        pdf.clear();
        if (texts) texts->clear();
        return run(assets, nullptr, document, nullptr, &pdf, texts, host, nullptr, errors);
    }

}
