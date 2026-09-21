/// @file
/// @brief Driver: wires the pipeline together and writes the PDF.
///
/// Order matters in two places. Logger::init() must not be followed by calls
/// that reset the filter, or the command line is discarded. And the cursor is
/// a stack, so the buffer ingested last is read first.
///
/// @par Disabled
/// The render primitive layer is commented out below, in three places marked
/// "render primitives". Its Wrapper::ingest() still expects the old
/// environments() and references() accessors, which the syntax core no longer
/// has. Restoring it means changing those two parameters to one
/// `const syntax::primitives::Blocks&` and passing commands.structure().
/// Until then the document still lexes, expands and parses; only directive
/// nodes go unproduced.
#include "logger.hpp"
#include "layout/document.hpp"
#include "layout/typesetter.hpp"
#include "memory/arena.hpp"
#include "memory/sandbox/policy.hpp"
#include "modules.hpp"
#include "render/composer.hpp"
#include "render/pdf.hpp"
// render primitives, disabled
// #include "render/primitives/wrapper.hpp"
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
#include "syntax/traceback.hpp"
#include "typography/font.hpp"
#include "typography/fontconfig.hpp"
#include "typography/hyphenator.hpp"
#include "typography/registry.hpp"
#include "typography/shaper.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

namespace {

    [[nodiscard]] std::filesystem::path self(const char* argument) {
        std::error_code error;

    #if defined(_WIN32)
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;) {
            const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                                     static_cast<DWORD>(buffer.size()));
            if (written == 0) break;
            if (written < buffer.size()) {
                buffer.resize(written);
                return std::filesystem::path(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
    #elif defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            if (auto resolved = std::filesystem::canonical(buffer.c_str(), error); !error) {
                return resolved;
            }
        }
    #else
        if (auto resolved = std::filesystem::read_symlink("/proc/self/exe", error); !error) {
            return resolved;
        }
    #endif

        if (argument != nullptr && *argument != '\0') {
            if (auto resolved = std::filesystem::absolute(argument, error); !error) {
                return resolved;
            }
        }
        return std::filesystem::current_path();
    }

    struct Release {
        render::typography::FontConfig& options;
        ~Release() { options.dispose(); }
    };

}

int main(int count, char* arguments[]) {
    Logger::init(count, arguments);

    const auto start = std::chrono::high_resolution_clock::now();

    memory::Arena arena(16 * 1024 * 1024);
    memory::Arena scratch(1024 * 1024);

    const auto opened = std::chrono::high_resolution_clock::now();

    const std::filesystem::path binary = self(count > 0 ? arguments[0] : nullptr);
    const std::filesystem::path root = binary.parent_path().parent_path();
    const std::filesystem::path assets = root / "assets";
    const std::filesystem::path directory = assets / "fonts";
    const std::filesystem::path configuration = directory / "aliases.conf";
    const std::filesystem::path source = root / "build" / "main.tex";
    const std::filesystem::path destination = root / "build" / "main.pdf";

    if (!std::filesystem::exists(configuration)) {
        std::cerr << "Alias configuration file missing at: " << configuration.string() << '\n';
        return 1;
    }

    if (!std::filesystem::exists(source)) {
        std::cerr << "TeX file missing at: " << source.string() << '\n';
        return 1;
    }

    #if defined(_WIN32)
        _putenv_s("FONTCONFIG_FILE", configuration.string().c_str());
    #else
        setenv("FONTCONFIG_FILE", configuration.string().c_str(), 1);
    #endif

    render::typography::FontConfig options(arena);
    const Release release{options};

    if (!options.compose(configuration.string())) {
        std::cerr << "Configuration load failure: " << configuration.string() << '\n';
        return 1;
    }

    std::size_t total = 0;
    if (std::filesystem::exists(directory)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                if (const auto extension = entry.path().extension().string();
                    extension == ".otf" || extension == ".ttf" || extension == ".pfb") {
                    if (options.compose(entry.path().string())) {
                        ++total;
                    }
                }
            }
        }
    }

    const auto location = options.find(scratch, "text");
    if (!location) {
        std::cerr << "Font query failure for family 'text'\n";
        return 1;
    }

    render::typography::Registry registry(arena);
    constexpr render::typography::Registry::Spec specification{
        .family = "text",
        .weight = 400,
        .slant = 0,
        .size = 12.0f
    };

    render::typography::Font* font = registry.get(specification, *location);
    if (!font) {
        std::cerr << "Registry font instantiation failure\n";
        return 1;
    }

    const auto mark = std::chrono::high_resolution_clock::now();

    syntax::Lexicon lexicon(arena);
    syntax::semantics::Union state{};

    syntax::expression::Unicodes unicodes;
    unicodes.compose("alpha", 0x03B1, syntax::expression::Unicodes::Category::Ordinary);
    unicodes.compose("xi",    0x03BE, syntax::expression::Unicodes::Category::Ordinary);
    unicodes.compose("pi",    0x03C0, syntax::expression::Unicodes::Category::Ordinary);
    unicodes.compose("omega", 0x03C9, syntax::expression::Unicodes::Category::Ordinary);
    unicodes.compose("infty", 0x221E, syntax::expression::Unicodes::Category::Ordinary);

    syntax::Cursor cursor(std::vector<syntax::Token>{});
    syntax::Mouth mouth(std::move(cursor), state, lexicon, arena);

    render::typography::Shaper shaper(arena);
    render::layout::Typesetter typesetter(arena, scratch);
    render::Composer composer(arena, scratch, shaper, typesetter);

    render::typography::Hyphenator hyphenator(arena);
    const std::filesystem::path patterns = assets / "hyphenation" / "en-us.pat";
    if (std::filesystem::exists(patterns)) {
        hyphenator.load(patterns.string());
    }
    composer.document().hyphenate(hyphenator);

    syntax::Parser parser(mouth, arena);

    // render primitives, disabled -- selection is only read by visuals below
    // render::primitives::configuration::Selection selection;
    // selection.font(font);

    // This driver reads main.tex from disk itself, so a document it runs may
    // pull in its own files. Everything else the sandbox denies by default.
    sandbox::Policy policy;
    policy.read = true;

    syntax::primitives::Wrapper commands(lexicon);

    // Context holds a reference to the conditional module, so the Wrapper has
    // to exist first. One Context is shared by every module, which is how two
    // modules end up reading the same register bank.
    syntax::primitives::Context context{policy, state.registers(), commands.conditionals()};
    commands(mouth, context);

    // render primitives, disabled -- ingest() still wants environments() and
    // references(), which the syntax core replaced with structure().
    // render::primitives::Wrapper visuals;
    // visuals.ingest(
    //     parser, composer.document(), state.registers(), registry, options, scratch,
    //     shaper, typesetter, unicodes, selection,
    //     commands.structure()
    // );

    std::ifstream file(source, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open LaTeX file: " << source.string() << '\n';
        return 1;
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string content(static_cast<std::size_t>(size), '\0');
    if (size > 0 && !file.read(content.data(), size)) {
        std::cerr << "Failed to read LaTeX file: " << source.string() << '\n';
        return 1;
    }

    if (const auto core = syntax::modules::find("main.mtex")) {
        mouth.ingest(content);   // injected first, read last: the cursor is a stack
        mouth.ingest(*core);
    } else {
        std::cerr << "Error: Embedded module 'main.mtex' not found!\n";
        return 1;
    }

    const memory::Slice<syntax::Node*> outputs = parser.parse();
    const auto step = std::chrono::high_resolution_clock::now();

    // Three independent sources, because each layer keeps its own list: the
    // parser's, the expander's, and the core's -- which Wrapper has already
    // merged across its modules.
    bool broken = parser.failed();
    if (broken) {
        parser.report(std::cerr);
    }

    for (const auto& fault : mouth.history()) {
        std::cerr << fault.format() << '\n';
        broken = true;
    }

    for (const auto& fault : commands.tracebacks()) {
        std::cerr << fault.format() << '\n';
        broken = true;
    }

    if (const std::size_t unclosed = commands.structure().depth(); unclosed != 0) {
        std::cerr << "Document ended with " << unclosed << " block(s) still open\n";
        broken = true;
    }

    for (std::size_t index = 0; index < outputs.count; ++index) {
        const syntax::Node* node = outputs[index];
        if (!node) continue;

        if (node->type == syntax::Node::Type::Expression && node->expression) {
            composer.document().append(node->expression, *font);
        } else if (node->type == syntax::Node::Type::Text) {
            if (!node->value.empty()) {
                composer.document().append(node->value, *font, specification.size);
            }
        } else if (node->type == syntax::Node::Type::Paragraph) {
            continue;   // a break carries no text
        } else if (node->type == syntax::Node::Type::Directive && node->directive) {
            composer.document().append(static_cast<render::layout::Node*>(node->directive));
        }
    }

    const auto composed = std::chrono::high_resolution_clock::now();
    if (!render::Pdf::compose(composer, 612.0f, 792.0f, destination.string())) {
        std::cerr << "Failed to output PDF to: " << destination.string() << '\n';
        return 1;
    }
    const auto tick = std::chrono::high_resolution_clock::now();

    const auto first  = std::chrono::duration<double, std::micro>(mark - opened).count();
    const auto second = std::chrono::duration<double, std::micro>(step - mark).count();
    const auto third  = std::chrono::duration<double, std::micro>(composed - step).count();
    const auto fourth = std::chrono::duration<double, std::micro>(tick - composed).count();
    const auto whole  = std::chrono::duration<double, std::micro>(tick - start).count();

    std::cout << std::fixed << std::setprecision(4);

    std::cout << "[Pipeline Metrics]\n";
    std::cout << "Registered font files     : " << total << '\n';
    std::cout << "Top-level AST Nodes       : " << outputs.count << '\n';
    std::cout << "Document Paragraphs       : " << composer.document().paragraphs().count << '\n';
    std::cout << "Output File               : " << destination.string() << "\n\n";

    std::cout << "[Subsystem Benchmarks]\n";
    std::cout << "Typography & Font Init    : " << first  << " us\n";
    std::cout << "Pratt Syntax Parsing      : " << second << " us\n";
    std::cout << "Document Composition      : " << third  << " us\n";
    std::cout << "PDF Composition & Render  : " << fourth << " us\n";
    std::cout << "Total End-to-End Execution: " << whole  << " us\n";

    Logger::close();
    return broken ? 1 : 0;
}