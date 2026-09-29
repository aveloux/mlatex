/// @file
/// @brief The one translation unit that embeds the glossary's bytes.
///
/// assets/glossary.mtex is compiled in as it stands, and the compiler splits
/// it into lines and indexes them by the command each begins with, so a run
/// neither reads nor splits anything to find one.
#include "syntax/glossary.hpp"
#include "syntax/mouth.hpp"
#include "memory/catalog.hpp"
#include "logger.hpp"

#include <array>
#include <cstddef>

namespace syntax {

    /// The glossary, as it stands in assets.
    static constexpr char source[] = {
#embed "../../assets/glossary.mtex" suffix(,)
        0};

    /// How many commands it holds: its lines that begin with a backslash.
    /// Everything else -- a blank line, a comment -- is only for its reader.
    static constexpr std::size_t count = [] {
        const std::string_view text{source, sizeof source - 1};
        std::size_t lines = 0;
        for (std::size_t start = 0; start < text.size();) {
            std::size_t stop = text.find('\n', start);
            if (stop == std::string_view::npos) stop = text.size();
            if (text[start] == '\\') ++lines;
            start = stop + 1;
        }
        return lines;
    }();

    /// Every line, found by the name it begins with: the backslash and the
    /// letters after it. A line keeps the name, so it can be read whole.
    static constexpr memory::Catalog<count> catalog{[] {
        const std::string_view text{source, sizeof source - 1};
        std::array<memory::Catalog<count>::Entry, count> entries{};
        std::size_t filled = 0;
        for (std::size_t start = 0; start < text.size();) {
            std::size_t stop = text.find('\n', start);
            if (stop == std::string_view::npos) stop = text.size();
            std::string_view line = text.substr(start, stop - start);
            if (line.ends_with('\r')) line.remove_suffix(1);
            if (line.starts_with('\\')) {
                std::size_t end = 1;
                while (end < line.size() && ((line[end] >= 'a' && line[end] <= 'z') ||
                                             (line[end] >= 'A' && line[end] <= 'Z') || line[end] == '@')) {
                    ++end;
                }
                entries[filled++] = {line.substr(0, end), line};
            }
            start = stop + 1;
        }
        return entries;
    }()};

    bool Glossary::define(Mouth& mouth, const Token& token) {
        if (token.symbol == none) return false;

        const auto slot = static_cast<std::size_t>(token.symbol);
        if (slot < tried.size() && tried[slot]) return false;
        if (slot >= tried.size()) tried.resize(slot + 1);
        tried[slot] = true;

        // A name the parser reads itself means something already.
        if (mouth.known(token.symbol)) return false;
        const auto line = get(token.text);
        if (!line) return false;

        // The stream is a stack: the line goes in front of the name, and
        // what defines it in front of the line. Neither stands on a line of
        // the document, and neither says it does: the command's body is read
        // wherever it is used, not only here where it was first.
        mouth.ingest(*line, memory::Location{});
        mouth.ingest("\\@shared\\@spanning\\@define", memory::Location{});
        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Defined '{}' from the glossary", token.text);
        return true;
    }

    std::optional<std::string_view> Glossary::get(const std::string_view name) noexcept {
        return catalog.get(name);
    }

}
