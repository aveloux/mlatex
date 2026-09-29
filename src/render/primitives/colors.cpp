/// @file
/// @brief Color primitives: `\\textcolor`, `\\definecolor` and `\\colorlet`.
#include "render/primitives/colors.hpp"
#include "logger.hpp"

#include "syntax/argument.hpp"
#include "syntax/semantics/scope.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace render::primitives {

    /// @brief Text without the spaces around it.
    [[nodiscard]] static std::string_view trim(std::string_view text) noexcept {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        return text;
    }

    Colors::Colors(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\textcolor");
        lexicon.intern("\\definecolor");
        lexicon.intern("\\colorlet");
    }

    std::string Colors::write(const graphics::Color& color) {
        return std::format("rgba({},{},{},{})", color.r * 255.0f, color.g * 255.0f, color.b * 255.0f, color.alpha);
    }

    graphics::Color Colors::resolve(const std::string_view text, const syntax::primitives::Variables& variables) {
        // One name: the document's own first, so it may give `red` a red of
        // its own, then whatever the parser reads.
        const auto named = [&variables](const std::string_view name) {
            const std::string_view bare = trim(name);
            if (const std::string* defined = variables.get("color." + std::string(bare))) {
                return graphics::Color::parse(*defined);
            }
            return graphics::Color::parse(bare);
        };

        // xcolor's mixtures, read left to right: `a!p!b` is p percent of a
        // on b, and a missing b is white. Each result is the next one's a.
        std::vector<std::string_view> parts;
        for (const auto part : std::views::split(text, '!')) parts.emplace_back(part.begin(), part.end());
        if (parts.empty()) return graphics::Color::parse(text);

        graphics::Color color = named(parts[0]);
        for (std::size_t index = 1; index < parts.size(); index += 2) {
            const std::string_view written = trim(parts[index]);
            float share = 100.0f;
            if (std::from_chars(written.data(), written.data() + written.size(), share).ec != std::errc{}) {
                share = 100.0f;
            }
            const float part = std::clamp(share / 100.0f, 0.0f, 1.0f);

            const graphics::Color other = index + 1 < parts.size() ? named(parts[index + 1])
                                                                  : graphics::Color{1.0f, 1.0f, 1.0f, 1.0f};
            color = {color.r * part + other.r * (1.0f - part), color.g * part + other.g * (1.0f - part),
                     color.b * part + other.b * (1.0f - part), color.alpha * part + other.alpha * (1.0f - part)};
        }
        return color;
    }

    std::optional<std::string> Colors::convert(std::string_view model, std::string_view value) {
        model = trim(model);
        value = trim(value);

        if (model == "HTML") {
            const bool hex = value.size() == 6 && std::ranges::all_of(value, [](const char digit) {
                return std::isxdigit(static_cast<unsigned char>(digit)) != 0;
            });
            if (!hex) return std::nullopt;
            return "#" + std::string(value);
        }

        std::array<float, 4> numbers{};
        std::size_t count = 0;
        for (const auto piece : std::views::split(value, ',')) {
            if (count == numbers.size()) return std::nullopt;
            const std::string_view number = trim(std::string_view(piece.begin(), piece.end()));
            const auto [stop, failure] = std::from_chars(number.data(), number.data() + number.size(), numbers[count]);
            if (failure != std::errc{} || stop != number.data() + number.size()) return std::nullopt;
            ++count;
        }

        // Every model's values lie between zero and a top of its own.
        const auto fits = [&numbers, count](const std::size_t wanted, const float top) {
            return count == wanted &&
                   std::all_of(numbers.begin(), numbers.begin() + static_cast<std::ptrdiff_t>(wanted),
                               [top](const float number) { return number >= 0.0f && number <= top; });
        };

        const auto [c, m, y, k] = numbers;
        if (model == "RGB" && fits(3, 255.0f)) return write({c / 255.0f, m / 255.0f, y / 255.0f, 1.0f});
        if (model == "rgb" && fits(3, 1.0f)) return write({c, m, y, 1.0f});
        if (model == "gray" && fits(1, 1.0f)) return write({c, c, c, 1.0f});
        if (model == "cmy" && fits(3, 1.0f)) return write({1.0f - c, 1.0f - m, 1.0f - y, 1.0f});
        if (model == "cmyk" && fits(4, 1.0f)) {
            return write({(1.0f - c) * (1.0f - k), (1.0f - m) * (1.0f - k), (1.0f - y) * (1.0f - k), 1.0f});
        }
        return std::nullopt;
    }

    void Colors::operator()(syntax::Parser& parser, Context& context) const {
        // Neither makes anything to set, so both are the expander's: they
        // run where they are read and leave only the variable behind. xcolor
        // lets a list of target models stand first in brackets; there is
        // only the one target here, so it is read and set aside.
        parser.mouth.bind("\\definecolor", [this, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            (void)mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0);

            const std::string name = syntax::Argument::text(mouth);
            const std::string model = syntax::Argument::text(mouth);
            const std::string value = syntax::Argument::expanded(mouth);

            if (trim(name).empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "\\definecolor needs a name for the color");
                return;
            }
            const std::optional<std::string> color = convert(model, value);
            if (!color) {
                tracebacks.emplace_back(
                    syntax::Traceback::Type::Argument, origin,
                    std::format("\\definecolor{{{}}}: '{}' is not a color in the '{}' model", name, value, model));
                return;
            }
            context.variables.define("color." + std::string(trim(name)), *color);
        });

        parser.mouth.bind("\\colorlet", [this, &context](syntax::Mouth& mouth) {
            const memory::Location origin = mouth.lookahead().location;
            (void)mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0);

            const std::string name = syntax::Argument::text(mouth);
            const std::string value = syntax::Argument::expanded(mouth);
            if (trim(name).empty()) {
                tracebacks.emplace_back(syntax::Traceback::Type::Argument, origin,
                                         "\\colorlet needs a name for the color");
                return;
            }

            // Resolved now, as xcolor does: a later change to what it was
            // made from does not reach back into it.
            context.variables.define("color." + std::string(trim(name)), write(resolve(value, context.variables)));
        });

        // A color as a document names one: `[rgb]{0.1,0.2,0.3}` in a model of
        // its own, as \definecolor would read it, or a name or a mixture as
        // written -- kept in the arena, since the text it colors is set long
        // after it is read.
        const auto tint = [&context](syntax::Mouth& mouth) {
            std::string model;
            for (const syntax::Token& token : mouth.argument(syntax::Mouth::Parameter{.optional = true}, 0)) {
                model += token.text;
            }
            const std::string written = syntax::Argument::text(mouth);
            const std::optional<std::string> converted = model.empty() ? std::nullopt : convert(model, written);
            const graphics::Color color = converted ? graphics::Color::parse(*converted)
                                                    : resolve(written, context.variables);
            auto* kept = context.arena.compose<layout::Node::Color>();
            *kept = {color.r, color.g, color.b, color.alpha};
            return kept;
        };

        // \color: the text from here to the end of the group or block it is
        // written in, in a color -- which the text keeps as it is read.
        parser.bind("\\color", [&context, tint](syntax::Parser& parser) -> syntax::Node* {
            context.selection.color(tint(parser.mouth));
            return nullptr;
        });
        parser.bind("\\normalcolor", [&context](syntax::Parser&) -> syntax::Node* {
            context.selection.color(nullptr);
            return nullptr;
        });

        // \textcolor{red}{...}: the same, around one group, which a line may
        // still break inside as it would anywhere else.
        parser.bind("\\textcolor", [this, &context, tint](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth;
            memory::Arena& arena = parser.arena;
            const memory::Location origin = mouth.lookahead().location;

            const layout::Node::Color* color = tint(mouth);
            syntax::Token open = mouth.read();
            while (open.category == syntax::Catcodes::Category::Space) open = mouth.read();
            if (!open.is(syntax::Catcodes::Category::Group, '{')) {
                if (!open.empty()) mouth.stream().inject(std::span{&open, 1});
                tracebacks.emplace_back(syntax::Traceback::Type::Group, origin, "\\textcolor needs a brace group");
                return nullptr;
            }

            const Selection kept = context.selection;
            context.selection.color(color);
            mouth.push(syntax::semantics::Scope::Type::Group);
            const memory::Slice<syntax::Node*> content = parser.parse('}');
            mouth.pop(syntax::semantics::Scope::Type::Group);
            context.selection = kept;
            return arena.compose<syntax::Node>(syntax::Node::Type::Group, std::string_view{}, origin, content);
        });

        // \pagecolor: what every page is painted in behind its text.
        parser.bind("\\pagecolor", [&context, tint](syntax::Parser& parser) -> syntax::Node* {
            context.document.furniture.background = *tint(parser.mouth);
            return nullptr;
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound color primitives");
    }

}
