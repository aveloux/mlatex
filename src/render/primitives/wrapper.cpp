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

    Wrapper::Wrapper(syntax::Lexicon& lexicon) noexcept
        : page(lexicon), typeface(lexicon), styles(lexicon), symbols(lexicon), groups(lexicon),
          boxes(lexicon), spacing(lexicon),
          rules(lexicon), penalties(lexicon), tables(lexicon), sections(lexicon), paragraphs(lexicon),
          lists(lexicon), expressions(lexicon), references(lexicon), citations(lexicon), footnotes(lexicon),
          counters(lexicon), theorems(lexicon), floats(lexicon), algorithms(lexicon), illustrations(lexicon), colors(lexicon), requests(lexicon), verbatim(lexicon), plots(lexicon), languages(lexicon) {}

    void Wrapper::operator()(syntax::Parser& parser, Context& context) const {
        // The counters first, because the modules that number things define
        // their own counters as they are installed.
        context.counters = &counters;

        // One fold over every module. Adding a primitive means adding a member
        // and a name here; nothing else changes.
        bind(parser, context, page, typeface, styles, symbols, groups, boxes, spacing, rules,
             penalties, tables, sections, paragraphs, lists, expressions, references, citations,
             footnotes, counters, theorems, floats, algorithms, illustrations, colors, requests, verbatim,
             plots, languages);

        Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                    "Render primitives installed");
    }

    std::vector<syntax::Traceback> Wrapper::tracebacks() const {
        // Only the modules that can report. Spacing, rules and penalties have
        // no failure to report: a number they cannot scan means a space or a
        // break of no size, which is what was written and not an error.
        // Colors reports only a color it was asked to define and could not;
        // one it is asked to use and cannot read is just black.
        const std::array<const std::vector<syntax::Traceback>*, 22> lists_{
            &page.tracebacks(), &typeface.tracebacks(), &styles.tracebacks(),
            &boxes.tracebacks(), &tables.tracebacks(), &sections.tracebacks(),
            &paragraphs.tracebacks(), &lists.tracebacks(), &expressions.tracebacks(),
            &references.tracebacks(), &citations.tracebacks(), &footnotes.tracebacks(),
            &counters.tracebacks(), &theorems.tracebacks(), &floats.tracebacks(), &algorithms.tracebacks(),
            &illustrations.tracebacks(),
            &requests.tracebacks(),
            &colors.tracebacks(), &verbatim.tracebacks(), &plots.tracebacks(), &languages.tracebacks(),
        };

        std::size_t total = 0;
        for (const auto* list : lists_) total += list->size();

        std::vector<syntax::Traceback> gathered;
        gathered.reserve(total);
        for (const auto* list : lists_) {
            gathered.insert(gathered.end(), list->begin(), list->end());
        }
        return gathered;
    }

}
