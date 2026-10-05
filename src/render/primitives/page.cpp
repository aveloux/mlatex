/// @file
/// @brief Page geometry primitives: `\\documentclass` and the margins.
///
/// Every one of the dimension primitives does the same three things -- skip an
/// optional `=`, scan a dimension, store it -- so they are bound from one loop
/// over a table of which field each name writes. Adding `\\gutter` is a row.
#include "render/primitives/page.hpp"
#include "render/primitives/counters.hpp"
#include "render/primitives/styles.hpp"
#include "layout/line.hpp"
#include "syntax/argument.hpp"
#include "syntax/semantics/scope.hpp"
#include "logger.hpp"

#include "syntax/modules.hpp"
#include "syntax/number.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render::primitives {

    Page::Page(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\documentclass");
        lexicon.intern("\\thepage");
        lexicon.intern("\\pagestyle");
        lexicon.intern("\\thispagestyle");
        lexicon.intern("\\pagenumbering");
        lexicon.intern("\\fancyhead");
        lexicon.intern("\\fancyfoot");
        lexicon.intern("\\fancyhf");
        lexicon.intern("\\pagerules");
    }

    void Page::operator()(syntax::Parser& parser, Context& context) const {
        using Configuration = layout::Document::Configuration;

        // Which field each name writes. A pointer-to-member keeps the handler
        // below a single closure rather than seven copies of it.
        static constexpr std::array<std::pair<std::string_view, float Configuration::*>, 8> fields{{
            {"\\pagewidth", &Configuration::width},
            {"\\pageheight", &Configuration::height},
            {"\\@leftmargin", &Configuration::left},
            {"\\@rightmargin", &Configuration::right},
            {"\\@topmargin", &Configuration::top},
            {"\\@bottommargin", &Configuration::bottom},
            {"\\leading", &Configuration::leading},
            {"\\parindent", &Configuration::indent},
        }};

        // The page's own lengths, as registers, so that any dimension may be
        // written in terms of them -- `0.4\\textwidth`, `-\\baselineskip` --
        // and a box may set `\\linewidth` for what is inside it. Put back in
        // step with the page wherever the page changes.
        using Registers = syntax::semantics::Registers;
        static constexpr std::array<std::string_view, 8> extents{
            "\\textwidth", "\\linewidth", "\\columnwidth", "\\hsize",
            "\\textheight", "\\paperwidth", "\\paperheight", "\\baselineskip",
        };
        for (std::size_t index = 0; index < extents.size(); ++index) {
            context.registers.bind(parser.mouth.lexicon.intern(extents[index]), Registers::Type::Dimension,
                                   Registers::reserved + index);
        }
        // \\parindent as well, after the two \\fbox lengths that follow these:
        // `\\the\\parindent`, `0.5\\parindent`. (\\parskip is a register of
        // its own, which the core names.)
        static constexpr std::size_t indent = Registers::reserved + 10;
        context.registers.bind(parser.mouth.lexicon.intern("\\parindent"), Registers::Type::Dimension, indent);
        const auto publish = [&context] {
            const Configuration& page = context.document.configuration;
            const float column = context.document.column(page.columns);
            const std::array<float, extents.size()> values{
                page.width - page.left - page.right, column, column, column, page.height - page.top - page.bottom,
                page.width, page.height, page.leading,
            };
            for (std::size_t index = 0; index < values.size(); ++index) {
                context.registers.set(Registers::Type::Dimension, Registers::reserved + index,
                                      static_cast<std::int32_t>(values[index] * 65536.0f), true);
            }
            context.registers.set(Registers::Type::Dimension, indent, static_cast<std::int32_t>(page.indent * 65536.0f), true);
        };
        publish();

        // LaTeX's counters of how many floats each area of a page may hold,
        // at the standard classes' values, for a document to set.
        if (context.counters) {
            context.counters->set("topnumber", 2);
            context.counters->set("bottomnumber", 1);
            context.counters->set("totalnumber", 3);
            context.counters->set("dbltopnumber", 2);
        }

        for (const auto& [name, field] : fields) {
            parser.mouth.bind(name, [this, &context, field, name, publish](syntax::Mouth& mouth) {
                // The `=` is noise, as it is everywhere in the language: read
                // it if it is there, and put back whatever else was.
                syntax::Token equals = mouth.read();
                if (equals.text != "=") mouth.stream().inject(std::span{&equals, 1});

                const auto scanned = syntax::Number::dimension(mouth, context.registers);
                if (!scanned) {
                    tracebacks.emplace_back(
                        syntax::Traceback::Type::Dimension, mouth.lookahead().location,
                        std::string(name) + " needs a dimension");
                    return;
                }

                // Dimensions are scanned in scaled points; the page is in
                // points, which is what the renderer draws in.
                context.document.configuration.*field =
                    static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
                publish();
            });
        }

        // LaTeX's standard classes: the body size its option names, the sheet,
        // and the text block its size files work out for that sheet -- as
        // wide as a comfortable line at that size, as many whole lines tall
        // as fit, and the block with its head and foot centred on the page.
        // A package such as geometry sets the margins afterwards as it would
        // there.
        // LaTeX's lengths. The page's own -- \\parindent, \\parskip,
        // \\textwidth and the rest -- set its configuration, the text block
        // kept where it stands; any other is a register, as \\newlength
        // makes one, set or added to as \\set and \\increase would.
        const auto adjust = [&context](const std::string& name, const float amount, const bool added) {
            Configuration& page = context.document.configuration;
            const float column = page.width - page.left - page.right;
            const float tall = page.height - page.top - page.bottom;
            const auto apply = [added, amount](const float current) { return added ? current + amount : amount; };

            if (name == "\\parindent") {
                page.indent = apply(page.indent);
            } else if (name == "\\parskip") {
                page.skip = apply(page.skip);
            } else if (name == "\\baselineskip") {
                page.leading = apply(page.leading);
            } else if (name == "\\textwidth") {
                page.right = page.width - page.left - apply(column);
            } else if (name == "\\textheight") {
                page.bottom = page.height - page.top - apply(tall);
            } else if (name == "\\oddsidemargin" || name == "\\evensidemargin") {
                page.left = 72.0f + apply(page.left - 72.0f);
                page.right = page.width - page.left - column;
            } else if (name == "\\topmargin") {
                // LaTeX's is measured from an inch down to the head, with the
                // head and the gap under it -- \\headheight, \\headsep -- still
                // above the text: the block moves, and keeps its height.
                constexpr float head = 12.0f + 25.0f;
                const float top = 72.0f + apply(page.top - 72.0f - head) + head;
                page.bottom = page.height - top - tall;
                page.top = top;
            } else {
                return false;
            }
            return true;
        };

        for (const bool added : {false, true}) {
            parser.mouth.bind(added ? "\\addtolength" : "\\setlength", [&context, adjust, added, publish](syntax::Mouth& mouth) {
                std::string name;
                for (const syntax::Token& token : mouth.argument({}, 0)) name += token.text;
                const std::string value = syntax::Argument::text(mouth);

                // The page's own, and \parskip's register beside it; any
                // other length is its register alone, local to the group it
                // is set in -- \linewidth in a minipage among them.
                if (adjust(name, measure(value, mouth, context), added)) {
                    publish();
                    if (name != "\\parskip") return;
                }
                if (name.empty()) return;
                mouth.ingest(context.arena.copy((added ? "\\@increase" : "\\@set") + name + (added ? " by " : "=") +
                                                "\\dimexpr " + value + "\\relax "));
            });
        }

        // A page length assigned as TeX assigns one -- `\textwidth=15cm`,
        // `\parskip=6pt` -- reached only its register. The lines are broken
        // and the pages made once the document ends, so it is then that the
        // page takes what its registers hold.
        context.blocks.watch("document", {}, [&context, publish](syntax::Mouth& mouth) {
            Configuration& page = context.document.configuration;
            const auto held = [&context](const std::size_t index) {
                return static_cast<float>(context.registers.get(Registers::Type::Dimension, Registers::reserved + index)) /
                       65536.0f;
            };
            const auto moved = [](const float value, const float current) { return std::abs(value - current) > 0.01f; };
            if (moved(held(5), page.width)) page.width = held(5);
            if (moved(held(6), page.height)) page.height = held(6);
            if (moved(held(0), page.width - page.left - page.right)) page.right = page.width - page.left - held(0);
            if (moved(held(4), page.height - page.top - page.bottom)) page.bottom = page.height - page.top - held(4);
            if (moved(held(7), page.leading)) page.leading = held(7);
            if (const auto skip = context.registers.target(mouth.lexicon.intern("\\parskip"))) {
                page.skip = static_cast<float>(context.registers.get(skip->type, skip->slot)) / 65536.0f;
            }
            if (const auto gap = context.registers.target(mouth.lexicon.intern("\\columnsep"))) {
                page.gap = static_cast<float>(context.registers.get(gap->type, gap->slot)) / 65536.0f;
            }

            // Where floats may go, as the document leaves LaTeX's own
            // parameters: the fractions its macros hold, the counts its
            // counters, and the space round a float its registers.
            layout::Pager::Placement& rules = page.placement;
            const auto fraction = [&mouth](const std::string_view name, float& field) {
                const syntax::Mouth::Macro* macro = mouth.macro(mouth.lexicon.intern(name));
                if (!macro) return;
                std::string written;
                for (const syntax::Token& token : macro->body) written += token.text;
                float value = field;
                if (std::from_chars(written.data(), written.data() + written.size(), value).ec == std::errc{} &&
                    value >= 0.0f && value <= 1.0f) {
                    field = value;
                }
            };
            fraction("\\topfraction", rules.top);
            fraction("\\bottomfraction", rules.bottom);
            fraction("\\textfraction", rules.text);
            fraction("\\floatpagefraction", rules.page);
            fraction("\\dbltopfraction", rules.spanning);
            fraction("\\dblfloatpagefraction", rules.sheet);
            if (context.counters) {
                const auto number = [&context](const std::string_view name, std::size_t& field) {
                    if (context.counters->contains(name)) {
                        field = static_cast<std::size_t>(std::max(context.counters->value(name), 0));
                    }
                };
                number("topnumber", rules.heads);
                number("bottomnumber", rules.feet);
                number("totalnumber", rules.most);
                number("dbltopnumber", rules.spans);
            }
            const auto length = [&context, &mouth](const std::string_view name, float& field) {
                if (const auto slot = context.registers.target(mouth.lexicon.intern(name))) {
                    field = static_cast<float>(context.registers.get(slot->type, slot->slot)) / 65536.0f;
                }
            };
            length("\\floatsep", rules.apart);
            length("\\textfloatsep", rules.clearance);
            length("\\intextsep", rules.amid);
            publish();
        });

        parser.mouth.bind("\\documentclass", [this, &context, publish](syntax::Mouth& mouth) {
            std::string options;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                options += token.text;
            }
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = syntax::Argument::text(mouth);

            // Every class is set as article is, at the size and on the sheet
            // its options ask for, in the columns its papers are printed in:
            // two for the conference and journal classes that print so. A
            // class the engine carries a module for -- its title page, its
            // commands, its margins -- has that read next, with the class's
            // options as its variables. Any other class is set as article,
            // and says so.
            struct Class {
                std::string_view name;   ///< The class.
                std::size_t columns;     ///< How many columns it sets unless told otherwise.
            };
            static constexpr std::array<Class, 36> classes{{
                {"article", 1}, {"report", 1}, {"book", 1}, {"letter", 1}, {"memoir", 1}, {"minimal", 1},
                {"proc", 2}, {"slides", 1}, {"extarticle", 1}, {"extreport", 1}, {"extbook", 1},
                {"amsart", 1}, {"amsbook", 1}, {"amsproc", 1}, {"scrartcl", 1}, {"scrreprt", 1}, {"scrbook", 1},
                {"scrlttr2", 1}, {"IEEEtran", 2}, {"IEEEconf", 2}, {"acmart", 1}, {"sig-alternate", 2},
                {"llncs", 1}, {"elsarticle", 1}, {"revtex4", 1}, {"revtex4-1", 1}, {"revtex4-2", 1},
                {"svjour3", 1}, {"svmult", 1}, {"sn-jnl", 1}, {"aa", 2}, {"mnras", 2}, {"aastex631", 1},
                {"aastex7", 1}, {"standalone", 1}, {"tufte-handout", 1},
            }};
            const auto known = std::ranges::find(classes, std::string_view(name), &Class::name);
            const std::string module = name + "/main.mtex";
            const bool carried = syntax::modules::get(module).has_value();
            if (known == classes.end() && !carried) {
                tracebacks.emplace_back(
                    syntax::Traceback::Type::Warning, origin,
                    std::format("File `{}.cls' not found; the document is set as article", name));
            }

            float width = 612.0f;    // letter paper, as LaTeX's own default
            float height = 792.0f;
            float body = name == "beamer" ? 10.95f : 10.0f;   // beamer's slides are eleven-point
            bool turned = false;
            std::size_t columns = known == classes.end() ? 1 : known->columns;
            for (const auto piece : std::views::split(std::string_view(options), ',')) {
                std::string_view option(piece.begin(), piece.end());
                while (!option.empty() && option.front() == ' ') option.remove_prefix(1);
                while (!option.empty() && option.back() == ' ') option.remove_suffix(1);

                if (option == "a4paper") {
                    width = 595.28f;
                    height = 841.89f;
                } else if (option == "a5paper") {
                    width = 419.53f;
                    height = 595.28f;
                } else if (option == "b5paper") {
                    width = 498.9f;
                    height = 708.66f;
                } else if (option == "letterpaper") {
                    width = 612.0f;
                    height = 792.0f;
                } else if (option == "legalpaper") {
                    width = 612.0f;
                    height = 1008.0f;
                } else if (option == "executivepaper") {
                    width = 522.0f;
                    height = 756.0f;
                } else if (option == "landscape") {
                    turned = true;
                } else if (option == "twocolumn" || option == "sigconf" || option == "sigplan" ||
                           option == "reprint" || option == "5p") {
                    // acmart's proceedings, revtex's journal pages and
                    // elsarticle's widest layout are two columns too.
                    columns = 2;
                } else if (option == "onecolumn" || option == "preprint" || option == "manuscript" ||
                           option == "draftcls" || option == "review") {
                    columns = 1;
                } else if (option == "10pt") {
                    body = 10.0f;
                } else if (option == "11pt") {
                    body = 10.95f;
                } else if (option == "12pt") {
                    body = 12.0f;
                }
            }
            if (turned && width < height) std::swap(width, height);

            // size10.clo, size11.clo and size12.clo: the baseline, the
            // indentation, the line's width -- the whole block's, when it is
            // shared by two columns -- and the space above the first.
            const bool eleven = body > 10.5f && body < 11.5f;
            const bool twelve = body >= 11.5f;
            const float baseline = twelve ? 14.5f : eleven ? 13.6f : 12.0f;
            const float indent = twelve ? 18.0f : eleven ? 17.0f : 15.0f;
            const float line = columns > 1 ? std::min(twelve ? 450.0f : eleven ? 430.0f : 410.0f, width - 108.0f)
                                           : std::min(twelve ? 390.0f : eleven ? 360.0f : 345.0f, width - 144.0f);
            const float lines = std::floor((height - 252.0f) / baseline);
            const float tall = lines * baseline + body;
            constexpr float head = 12.0f + 25.0f;   // \headheight and \headsep
            constexpr float foot = 30.0f;           // \footskip
            const float top = 72.0f + (height - 144.0f - head - tall - foot) * 0.5f + head;

            Configuration page = context.document.configuration;
            page.width = width;
            page.height = height;
            page.left = (width - line) * 0.5f;
            page.right = page.left;
            page.top = top;
            page.bottom = height - top - tall;
            page.leading = baseline;
            page.indent = indent;
            page.size = body;
            page.columns = columns;
            context.document.configuration = page;
            spread.assign(1, columns);
            publish();

            // report and book number headings down to a subsection and list
            // them as deep, where article goes one level further.
            if (context.counters && (name == "report" || name == "book" || name == "memoir")) {
                context.counters->set("secnumdepth", 2);
                context.counters->set("tocdepth", 2);
            }
            // Theirs and KOMA-Script's: the references a chapter, `Bibliography`.
            if (name == "report" || name == "book" || name == "memoir" || name == "scrreprt" || name == "scrbook") {
                mouth.ingest("\\@define\\@bibkind{chapter}\\@define\\@bibtitle{\\bibname}", origin);
            }

            // The body's face at the body's size, and the formulas' beside it.
            if (const typography::Font* text = Styles::resolve(context, Styles::Cut::Normal, body)) {
                context.selection.text(text);
                context.document.furniture.face = text;
            }
            if (const typography::Font* formula = context.selection.formula()) {
                if (const typography::Font* sized = context.registry.get({.family = formula->family(), .size = body})) {
                    context.selection.formula(sized);
                }
            }
            context.document.fallback = context.selection.formula();
            context.registers.quad = static_cast<std::int32_t>(body * 65536.0f);

            // The class's own module, read as a package is: its options kept
            // as its variables, and itself marked loaded.
            if (carried) {
                mouth.ingest(context.arena.copy(std::format("\\usepackage[{}]{{{}}}", options, name)), origin);
            }

            Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                        "Document class '{}': {} by {} points, {}-point body", name, width, height, body);
        });

        // beamer's frame: a page of its own, its title at its head -- given
        // as its argument, or by the \frametitle it opens with -- and what it
        // holds set in the middle of the rest, at its top for `[t]` or at its
        // foot for `[b]`. An overlay, `<2->`, is read and let go: every slide
        // of a frame is the one page here. How a title looks is the class's
        // \frametitle and \framesubtitle.
        const syntax::Symbol titled = parser.mouth.lexicon.intern("\\frametitle");
        const syntax::Symbol subtitled = parser.mouth.lexicon.intern("\\framesubtitle");
        context.blocks.watch(
            "frame",
            [this, titled, subtitled](syntax::Mouth& mouth) {
                using Category = syntax::Catcodes::Category;
                const auto blank = [&mouth] {
                    while (mouth.lookahead().category == Category::Space) mouth.read();
                };
                const auto overlay = [&mouth, blank] {
                    blank();
                    if (!mouth.lookahead().is('<')) return;
                    while (!mouth.lookahead().empty() && !mouth.read().is('>')) {}
                };
                overlay();
                std::string options;
                for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                    options += token.text;
                }
                const auto has = [&options](const std::string_view key) {
                    for (const auto piece : std::views::split(std::string_view(options), ',')) {
                        std::string_view option(piece.begin(), piece.end());
                        while (!option.empty() && option.front() == ' ') option.remove_prefix(1);
                        while (!option.empty() && option.back() == ' ') option.remove_suffix(1);
                        if (option == key) return true;
                    }
                    return false;
                };
                const char placement = has("t") ? 't' : has("b") ? 'b' : 'c';
                frames.push_back(placement);

                // The title and subtitle as arguments, or as the commands
                // the frame opens with, their overlays and short forms let go.
                std::array<std::vector<syntax::Token>, 2> heads{};
                for (std::vector<syntax::Token>& head : heads) {
                    blank();
                    if (mouth.lookahead().is(Category::Group, '{')) head = mouth.argument({}, 0);
                }
                for (std::size_t which = 0; which < heads.size(); ++which) {
                    blank();
                    if (mouth.lookahead().symbol != (which == 0 ? titled : subtitled)) continue;
                    mouth.read();
                    overlay();
                    static_cast<void>(mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0));
                    heads[which] = mouth.argument({}, 0);
                }

                // Read in this order: the title, the subtitle, and the fill
                // above what the frame holds -- each put in front of the one
                // after it, so last first. The page before ended with its
                // frame, so what stands between two frames -- a section's
                // line of the contents -- goes with the next.
                if (placement != 't') mouth.ingest("\\vspace*{\\fill}");
                for (std::size_t which = heads.size(); which-- > 0;) {
                    if (heads[which].empty()) continue;
                    const syntax::Symbol command = which == 0 ? titled : subtitled;
                    std::vector<syntax::Token> written{
                        {.symbol = command, .category = Category::Escape, .text = mouth.lexicon.resolve(command)},
                        {.symbol = mouth.lexicon.intern("{"), .category = Category::Group, .text = "{"}};
                    written.insert(written.end(), heads[which].begin(), heads[which].end());
                    written.push_back({.symbol = mouth.lexicon.intern("}"), .category = Category::Group, .text = "}"});
                    mouth.stream().inject(std::span{written});
                }
            },
            [this](syntax::Mouth& mouth) {
                const char placement = frames.empty() ? 'c' : frames.back();
                if (!frames.empty()) frames.pop_back();
                mouth.ingest(placement == 'b' ? "\\par\\clearpage " : "\\par\\vfill\\clearpage ");
            });

        // The letter class's letter: on a page of its own, to the recipient
        // its argument names. How it is set -- the addresses, the date, the
        // closing -- is the class's \@letter, which reads that argument, and
        // its \@endletter.
        context.blocks.watch(
            "letter", [](syntax::Mouth& mouth) { mouth.ingest("\\@letter "); },
            [](syntax::Mouth& mouth) { mouth.ingest("\\@endletter "); });

        // LaTeX's titlepage: a page of its own with no number on it, the
        // pages after counted from one.
        context.blocks.watch(
            "titlepage", [](syntax::Mouth& mouth) { mouth.ingest("\\clearpage\\thispagestyle{empty}"); },
            [](syntax::Mouth& mouth) { mouth.ingest("\\par\\clearpage\\setcounter{page}{1}"); });

        using Directive = layout::Node::Directive;

        // A plain page's number is set in the face the text starts in, and a
        // character that face lacks looked for in the formulas' face, which
        // carries the symbols.
        context.document.furniture.face = context.selection.text();
        context.document.fallback = context.selection.formula();

        // The page's number, where it is written: in a head or a foot, or in
        // the text, where it takes the room two digits would.
        parser.bind("\\thepage", [&context](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const typography::Font* font = context.selection.text();

            float width = 0.0f;
            if (font) {
                const typography::Font* fonts[] = {font};
                for (const layout::Node* piece : context.shaper.shape(memory::Slice{fonts, 1uz}, "00", {})) {
                    width += layout::Line::advance(piece);
                }
            }

            auto* node = arena.compose<layout::Node>();
            node->directive({.command = Directive::Command::Number, .width = width, .font = font});
            return directive(arena, node, parser.mouth.lookahead().location);
        });

        // `\\@columns 2`: the text from here on in so many columns side by
        // side; `\\@columns 0`, in those in force before the last change.
        // \\twocolumn, \\onecolumn, the multicols block and a title set
        // across a two-column page are each written with it. What is
        // measured against \\linewidth from here on is measured against a
        // column.
        parser.bind("\\@columns", [this, &context](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            const memory::Location origin = mouth.lookahead().location;
            const std::int32_t asked = syntax::Number::integer(mouth, context.registers).value_or(1);
            if (spread.empty()) spread.push_back(context.document.configuration.columns);
            if (asked > 0) {
                spread.push_back(static_cast<std::size_t>(asked));
            } else if (spread.size() > 1) {
                spread.pop_back();
            }

            if (const auto gap = context.registers.target(mouth.lexicon.intern("\\columnsep"))) {
                context.document.configuration.gap =
                    static_cast<float>(context.registers.get(gap->type, gap->slot)) / 65536.0f;
            }
            const float width = context.document.column(spread.back());
            for (const std::size_t index : {1uz, 2uz, 3uz}) {
                context.registers.set(Registers::Type::Dimension, Registers::reserved + index,
                                      static_cast<std::int32_t>(width * 65536.0f), false);
            }

            auto* node = parser.arena.compose<layout::Node>(layout::Node::Type::Directive);
            node->directive({.command = Directive::Command::Columns, .index = spread.back()});
            return directive(parser.arena, node, origin, true);
        });

        // multicol's block, `\\begin{multicols}{3}[a heading across them]`:
        // \\multicolsep of space, the heading across the text block, then the
        // text in so many columns, balanced where the block ends, and the
        // space again. The starred form, which leaves its columns
        // unbalanced, is set the same.
        for (const std::string_view name : {"multicols", "multicols*"}) {
            context.blocks.watch(
                name,
                [&context](syntax::Mouth& mouth) {
                    const std::string count = syntax::Argument::text(mouth);
                    std::string heading;
                    for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                        heading += token.text;
                        if (token.text.size() > 1 && token.text.front() == '\\') heading += ' ';
                    }
                    mouth.ingest(context.arena.copy("\\par\\vskip\\multicolsep " + heading + "\\par\\@columns " +
                                                    count + " "));
                },
                [](syntax::Mouth& mouth) { mouth.ingest("\\par\\@columns0 \\vskip\\multicolsep "); });
        }

        // A style from this page on, or for this page alone.
        static constexpr std::array<std::pair<std::string_view, Directive::Style>, 5> styles{{
            {"plain", Directive::Style::Plain}, {"empty", Directive::Style::Empty},
            {"headings", Directive::Style::Headings}, {"myheadings", Directive::Style::Headings},
            {"fancy", Directive::Style::Fancy},
        }};
        for (const bool local : {false, true}) {
            parser.bind(local ? "\\thispagestyle" : "\\pagestyle",
                        [this, local](syntax::Parser& parser) -> syntax::Node* {
                memory::Arena& arena = parser.arena;
                const memory::Location origin = parser.mouth.lookahead().location;
                const std::string name = syntax::Argument::text(parser.mouth);

                const auto found = std::ranges::find(styles, name, &std::pair<std::string_view, Directive::Style>::first);
                Directive::Style style = found == styles.end() ? Directive::Style::Fancy : found->second;
                if (found == styles.end()) {
                    // A style a package defines, as LaTeX's \pagestyle has
                    // it: `\ps@name`, run now -- titleps' styles, KOMA's
                    // scrheadings -- setting the heads and feet a fancy page
                    // draws.
                    syntax::Mouth& mouth = parser.mouth;
                    const syntax::Symbol defined = mouth.lexicon.intern("\\ps@" + name);
                    if (!mouth.known(defined)) {
                        tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                                 "Unknown page style '" + name + "'");
                        return directive(arena, nullptr, origin);
                    }
                    const syntax::Token call{.symbol = defined, .category = syntax::Catcodes::Category::Escape,
                                             .text = mouth.lexicon.resolve(defined)};
                    mouth.stream().inject(std::span{&call, 1});
                }

                auto* node = arena.compose<layout::Node>();
                node->directive({.command = Directive::Command::Page, .style = style, .local = local});
                return directive(arena, node, origin, true);
            });
        }

        parser.bind("\\pagenumbering", [this](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            const std::string name = syntax::Argument::text(parser.mouth);

            Directive order{.command = Directive::Command::Page};
            if (name == "arabic") {
                order.numbering = Directive::Numbering::Arabic;
            } else if (name == "roman" || name == "Roman") {
                order.numbering = Directive::Numbering::Roman;
                order.capital = name == "Roman";
            } else if (name == "alph" || name == "Alph") {
                order.numbering = Directive::Numbering::Alphabetic;
                order.capital = name == "Alph";
            } else {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "Unknown page numbering '" + name + "'");
                return directive(arena, nullptr, origin);
            }

            auto* node = arena.compose<layout::Node>();
            node->directive(order);
            return directive(arena, node, origin, true);
        });

        // `\@folio{5}`: the number the page this lands on takes, the pages
        // after it counting on -- what `\setcounter{page}{5}` asks for.
        parser.bind("\\@folio", [](syntax::Parser& parser) -> syntax::Node* {
            memory::Arena& arena = parser.arena;
            const memory::Location origin = parser.mouth.lookahead().location;
            const std::string written = syntax::Argument::text(parser.mouth);
            std::size_t number = 0;
            if (std::from_chars(written.data(), written.data() + written.size(), number).ec != std::errc{}) {
                return directive(arena, nullptr, origin);
            }
            auto* node = arena.compose<layout::Node>();
            node->directive({.command = Directive::Command::Page, .index = number + 1});
            return directive(arena, node, origin, true);
        });

        // What goes in a head or a foot: read in brackets as fancyhdr writes
        // it -- L, C and R for the places, E and O for even and odd pages,
        // which are one and the same here -- and set now, in the face in use,
        // so the page only has to place it.
        const auto furnishing = [this, &context](syntax::Parser& parser, const bool head, const bool foot) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            std::string places;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                places += token.text;
            }

            syntax::Token open = mouth.read();
            if (!open.is(syntax::Catcodes::Category::Group, '{')) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "A head or a foot needs its text in braces");
                if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                return directive(arena, nullptr, origin);
            }
            mouth.push(syntax::semantics::Scope::Type::Group);
            const memory::Slice<syntax::Node*> content = parser.parse('}');
            mouth.pop(syntax::semantics::Scope::Type::Group);
            stamp(content, context);

            std::vector<layout::Node*> gathered;
            for (const syntax::Node* child : content) compose(gathered, child, context);
            const memory::Slice<layout::Node*> nodes = arena.allocate<layout::Node*>(gathered.size());
            std::ranges::copy(gathered, nodes.begin());

            const bool every = places.find_first_of("LCR") == std::string::npos;
            layout::Document::Furniture& furniture = context.document.furniture;
            for (std::size_t place = 0; place < 3; ++place) {
                if (!every && places.find("LCR"[place]) == std::string::npos) continue;
                if (head) furniture.head[place] = nodes;
                if (foot) furniture.foot[place] = nodes;
            }
            return directive(arena, nullptr, origin);
        };
        parser.bind("\\fancyhead", [furnishing](syntax::Parser& parser) { return furnishing(parser, true, false); });
        parser.bind("\\fancyfoot", [furnishing](syntax::Parser& parser) { return furnishing(parser, false, true); });
        parser.bind("\\fancyhf", [furnishing](syntax::Parser& parser) { return furnishing(parser, true, true); });

        parser.mouth.bind("\\pagerules", [&context](syntax::Mouth& mouth) {
            std::array<float, 2> widths{context.document.furniture.rule, context.document.furniture.line};
            for (float& width : widths) {
                syntax::Token open = mouth.read();
                if (!open.is(syntax::Catcodes::Category::Group, '{') && !open.empty()) {
                    mouth.stream().inject(std::span{&open, 1});
                }
                if (const auto scanned = syntax::Number::dimension(mouth, context.registers)) {
                    width = static_cast<float>(*scanned) / static_cast<float>(syntax::Number::scale);
                }
                syntax::Token close = mouth.read();
                if (!close.is(syntax::Catcodes::Category::Group, '}') && !close.empty()) {
                    mouth.stream().inject(std::span{&close, 1});
                }
            }
            context.document.furniture.rule = widths[0];
            context.document.furniture.line = widths[1];
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound page primitives");
    }

}
