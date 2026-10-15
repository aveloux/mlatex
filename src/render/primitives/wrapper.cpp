/// @file
/// @brief Installs the render primitive set and gathers what it reported.
///
/// The bind() calls are independent -- a later module may rebind a name an
/// earlier one took, and the last one installed wins.
#include "render/primitives/wrapper.hpp"
#include "logger.hpp"

#include <array>
#include <cstddef>

namespace render::primitives {
    Wrapper::Wrapper(syntax::Lexicon &lexicon) noexcept
        : pages(lexicon), typefaces(lexicon), styles(lexicon), characters(lexicon), groups(lexicon),
          boxes(lexicon), glue(lexicon),
          rules(lexicon), penalties(lexicon), tables(lexicon), sections(lexicon), paragraphs(lexicon),
          lists(lexicon), formulas(lexicon), references(lexicon), citations(lexicon), footnotes(lexicon),
          counters(lexicon), theorems(lexicon), floats(lexicon), algorithms(lexicon), illustrations(lexicon),
          colors(lexicon), requests(lexicon), verbatim(lexicon), plots(lexicon), diagrams(lexicon), languages(lexicon) {
    }

    void Wrapper::operator()(syntax::Parser &parser, Context &context) const {
        // The counters first, because the modules that number things define
        // their own counters as they are installed.
        context.counters = &counters;

        // One fold over every module. Adding a primitive means adding a member
        // and a name here; nothing else changes.
        bind(parser, context, pages, typefaces, styles, characters, groups, boxes, glue, rules,
             penalties, tables, sections, paragraphs, lists, formulas, references, citations,
             footnotes, counters, theorems, floats, algorithms, illustrations, colors, requests, verbatim,
             plots, diagrams, languages);

        Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                    "Render primitives installed");
    }

    std::vector<syntax::Traceback> Wrapper::traceback() const {
        // Only the modules that can report. Spacing, rules and penalties have
        // no failure to report: a number they cannot scan means a space or a
        // break of no size, which is what was written and not an error.
        // Colors reports only a color it was asked to define and could not;
        // one it is asked to use and cannot read is just black.
        const std::array<const std::vector<syntax::Traceback> *, 23> parts{
            &pages.traceback(), &typefaces.traceback(), &styles.traceback(),
            &boxes.traceback(), &tables.traceback(), &sections.traceback(),
            &paragraphs.traceback(), &lists.traceback(), &formulas.traceback(),
            &references.traceback(), &citations.traceback(), &footnotes.traceback(),
            &counters.traceback(), &theorems.traceback(), &floats.traceback(), &algorithms.traceback(),
            &illustrations.traceback(),
            &requests.traceback(),
            &colors.traceback(), &verbatim.traceback(), &plots.traceback(), &diagrams.traceback(),
            &languages.traceback(),
        };

        std::size_t total = 0;
        for (const auto *list: parts) total += list->size();

        std::vector<syntax::Traceback> gathered;
        gathered.reserve(total);
        for (const auto *list: parts) {
            gathered.insert(gathered.end(), list->begin(), list->end());
        }
        return gathered;
    }
}
