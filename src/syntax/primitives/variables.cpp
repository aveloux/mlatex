/// @file
/// @brief Variables implementation: the named values a document reads.
///
/// A value is text until it is read, and reading it is lexing it into the
/// stream -- copied into the arena first, so the tokens made from it never
/// point into a value a later `\\setvariable` has since replaced.
#include "syntax/primitives/variables.hpp"
#include "syntax/argument.hpp"
#include "logger.hpp"

#include <format>
#include <span>
#include <string>

namespace syntax::primitives {

    Variables::Variables(Lexicon& lexicon) noexcept {
        lexicon.intern("\\variable");
        lexicon.intern("\\setvariable");
        lexicon.intern("\\ifvariable");
        lexicon.intern("\\unsetvariable");
        lexicon.intern("\\ifstrequal");
        lexicon.intern("\\setkeys");
    }

    void Variables::define(const std::string_view name, const std::string_view value) const {
        if (const auto found = values.find(name); found != values.end()) {
            found->second = value;
            return;
        }
        values.emplace(std::string(name), std::string(value));
    }

    void Variables::unset(const std::string_view name) const {
        if (const auto found = values.find(name); found != values.end()) values.erase(found);
    }

    void Variables::assign(const std::string_view family, const std::string_view list) const {
        const auto trim = [](std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.remove_suffix(1);
            return text;
        };

        std::size_t start = 0;
        int depth = 0;
        for (std::size_t index = 0; index <= list.size(); ++index) {
            const char letter = index < list.size() ? list[index] : ',';
            if (letter == '{') ++depth;
            if (letter == '}' && depth > 0) --depth;
            if (letter != ',' || depth != 0) continue;

            const std::string_view item = trim(list.substr(start, index - start));
            start = index + 1;
            if (item.empty()) continue;

            // The first equals sign outside braces splits the key from its value.
            std::size_t split = std::string_view::npos;
            int level = 0;
            for (std::size_t place = 0; place < item.size(); ++place) {
                if (item[place] == '{') ++level;
                if (item[place] == '}' && level > 0) --level;
                if (item[place] == '=' && level == 0) {
                    split = place;
                    break;
                }
            }

            const std::string_view key = trim(item.substr(0, split));
            std::string_view value = split == std::string_view::npos ? std::string_view{"true"}
                                                                      : trim(item.substr(split + 1));
            if (value.size() >= 2 && value.front() == '{' && value.back() == '}') {
                value = trim(value.substr(1, value.size() - 2));
            }
            if (!key.empty()) define(std::string(family) + "." + std::string(key), value);
        }
    }

    const std::string* Variables::get(const std::string_view name) const noexcept {
        const auto found = values.find(name);
        return found == values.end() ? nullptr : &found->second;
    }

    void Variables::operator()(Mouth& mouth, Context&) const {
        mouth.bind("\\variable", [this](Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            const std::string name = Argument::text(mouth);

            const std::string* value = get(name);
            if (!value) {
                tracebacks.emplace_back(Traceback::Type::Primitive, origin,
                                         std::format("\\variable: nothing is set under '{}'", name));
                return;
            }
            if (!value->empty()) mouth.ingest(mouth.arena.copy(*value));
        });

        // The value is expanded before it is kept, so a running total is a
        // number and not the calculation that made it -- which would refer to
        // itself the next time round.
        mouth.bind("\\setvariable", [this](Mouth& mouth) {
            const std::string name = Argument::text(mouth);
            const std::string value = Argument::expanded(mouth);
            define(name, value);
            Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Variable '{}' set to '{}'", name, value);
        });

        mouth.bind("\\setkeys", [this](Mouth& mouth) {
            const std::string family = Argument::text(mouth);
            assign(family, Argument::expanded(mouth));
        });

        mouth.bind("\\unsetvariable", [this](Mouth& mouth) { unset(Argument::text(mouth)); });

        // Both sides expanded first, so each compares by what it comes to.
        mouth.bind("\\ifstrequal", [](Mouth& mouth) {
            const std::string left = Argument::expanded(mouth);
            const std::string right = Argument::expanded(mouth);
            const std::vector<Token> same = mouth.argument({}, 1);
            const std::vector<Token> different = mouth.argument({}, 1);

            const std::vector<Token>& chosen = left == right ? same : different;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        });

        mouth.bind("\\ifvariable", [this](Mouth& mouth) {
            const std::string name = Argument::text(mouth);
            const std::vector<Token> present = mouth.argument({}, 1);
            const std::vector<Token> absent = mouth.argument({}, 1);

            const std::vector<Token>& chosen = get(name) ? present : absent;
            if (!chosen.empty()) mouth.stream().inject(std::span{chosen});
        });

        Logger::log(Logger::Type::Mouth, Logger::Level::Debug, "Bound the variable primitives");
    }

}
