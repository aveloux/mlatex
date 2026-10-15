/// @file
/// @brief Named characters: the ones a document cannot type for itself.
///
/// Every one of these is the same handler over a different string, so they are
/// bound from one table. The text is returned as a node rather than pushed
/// back through the lexer, which is what keeps a `%` from being read as the
/// comment it would otherwise start.
#include "render/primitives/text/characters.hpp"
#include "logger.hpp"
#include "syntax/argument.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace render::primitives::text {

    /// The accent commands' own characters, `\\'` through `\\t`, in the order
    /// their rows of #composed are kept.
    static constexpr std::string_view marks = "'`^\"~=.uvHckrdbt";

    /// Every precomposed letter, found by its accent's row and its base
    /// letter's byte in one load, rather than by a search of the list above.
    static constexpr auto composed = [] {
        // One accented letter Unicode spells as a character of its own.
        struct Composition {
            char accent; // The accent command's character: `'` for `\\'`.
            char base; // The letter under it.
            std::string_view result; // The one character both make, in UTF-8.
        };

        // Every accented Latin letter a European language writes, precomposed:
        // a face draws `é` as one glyph, spaced and kerned as one, where a
        // letter and a combining mark would be two it has to be told to stack.
        constexpr std::array<Composition, 118> compositions{
            {
                {.accent = '\'', .base = 'a', .result = "á"}, {.accent = '\'', .base = 'e', .result = "é"},
                {.accent = '\'', .base = 'i', .result = "í"}, {.accent = '\'', .base = 'o', .result = "ó"},
                {.accent = '\'', .base = 'u', .result = "ú"}, {.accent = '\'', .base = 'y', .result = "ý"},
                {.accent = '\'', .base = 'A', .result = "Á"}, {.accent = '\'', .base = 'E', .result = "É"},
                {.accent = '\'', .base = 'I', .result = "Í"}, {.accent = '\'', .base = 'O', .result = "Ó"},
                {.accent = '\'', .base = 'U', .result = "Ú"}, {.accent = '\'', .base = 'Y', .result = "Ý"},
                {.accent = '\'', .base = 'c', .result = "ć"}, {.accent = '\'', .base = 'n', .result = "ń"},
                {.accent = '\'', .base = 's', .result = "ś"}, {.accent = '\'', .base = 'z', .result = "ź"},
                {.accent = '\'', .base = 'C', .result = "Ć"}, {.accent = '\'', .base = 'N', .result = "Ń"},
                {.accent = '\'', .base = 'S', .result = "Ś"}, {.accent = '\'', .base = 'Z', .result = "Ź"},
                {.accent = '`', .base = 'a', .result = "à"}, {.accent = '`', .base = 'e', .result = "è"},
                {.accent = '`', .base = 'i', .result = "ì"}, {.accent = '`', .base = 'o', .result = "ò"},
                {.accent = '`', .base = 'u', .result = "ù"}, {.accent = '`', .base = 'A', .result = "À"},
                {.accent = '`', .base = 'E', .result = "È"}, {.accent = '`', .base = 'I', .result = "Ì"},
                {.accent = '`', .base = 'O', .result = "Ò"}, {.accent = '`', .base = 'U', .result = "Ù"},
                {.accent = '^', .base = 'a', .result = "â"}, {.accent = '^', .base = 'e', .result = "ê"},
                {.accent = '^', .base = 'i', .result = "î"}, {.accent = '^', .base = 'o', .result = "ô"},
                {.accent = '^', .base = 'u', .result = "û"}, {.accent = '^', .base = 'A', .result = "Â"},
                {.accent = '^', .base = 'E', .result = "Ê"}, {.accent = '^', .base = 'I', .result = "Î"},
                {.accent = '^', .base = 'O', .result = "Ô"}, {.accent = '^', .base = 'U', .result = "Û"},
                {.accent = '"', .base = 'a', .result = "ä"}, {.accent = '"', .base = 'e', .result = "ë"},
                {.accent = '"', .base = 'i', .result = "ï"}, {.accent = '"', .base = 'o', .result = "ö"},
                {.accent = '"', .base = 'u', .result = "ü"}, {.accent = '"', .base = 'y', .result = "ÿ"},
                {.accent = '"', .base = 'A', .result = "Ä"}, {.accent = '"', .base = 'E', .result = "Ë"},
                {.accent = '"', .base = 'I', .result = "Ï"}, {.accent = '"', .base = 'O', .result = "Ö"},
                {.accent = '"', .base = 'U', .result = "Ü"}, {.accent = '"', .base = 'Y', .result = "Ÿ"},
                {.accent = '~', .base = 'a', .result = "ã"}, {.accent = '~', .base = 'n', .result = "ñ"},
                {.accent = '~', .base = 'o', .result = "õ"}, {.accent = '~', .base = 'A', .result = "Ã"},
                {.accent = '~', .base = 'N', .result = "Ñ"}, {.accent = '~', .base = 'O', .result = "Õ"},
                {.accent = 'c', .base = 'c', .result = "ç"}, {.accent = 'c', .base = 'C', .result = "Ç"},
                {.accent = 'c', .base = 's', .result = "ş"}, {.accent = 'c', .base = 'S', .result = "Ş"},
                {.accent = 'c', .base = 't', .result = "ţ"}, {.accent = 'c', .base = 'T', .result = "Ţ"},
                {.accent = 'v', .base = 'c', .result = "č"}, {.accent = 'v', .base = 'C', .result = "Č"},
                {.accent = 'v', .base = 's', .result = "š"}, {.accent = 'v', .base = 'S', .result = "Š"},
                {.accent = 'v', .base = 'z', .result = "ž"}, {.accent = 'v', .base = 'Z', .result = "Ž"},
                {.accent = 'v', .base = 'r', .result = "ř"}, {.accent = 'v', .base = 'R', .result = "Ř"},
                {.accent = 'v', .base = 'e', .result = "ě"}, {.accent = 'v', .base = 'E', .result = "Ě"},
                {.accent = 'v', .base = 'n', .result = "ň"}, {.accent = 'v', .base = 'N', .result = "Ň"},
                {.accent = 'v', .base = 'd', .result = "ď"}, {.accent = 'v', .base = 'D', .result = "Ď"},
                {.accent = 'v', .base = 't', .result = "ť"}, {.accent = 'v', .base = 'T', .result = "Ť"},
                {.accent = '=', .base = 'a', .result = "ā"}, {.accent = '=', .base = 'e', .result = "ē"},
                {.accent = '=', .base = 'i', .result = "ī"}, {.accent = '=', .base = 'o', .result = "ō"},
                {.accent = '=', .base = 'u', .result = "ū"}, {.accent = '=', .base = 'A', .result = "Ā"},
                {.accent = '=', .base = 'E', .result = "Ē"}, {.accent = '=', .base = 'I', .result = "Ī"},
                {.accent = '=', .base = 'O', .result = "Ō"}, {.accent = '=', .base = 'U', .result = "Ū"},
                {.accent = '.', .base = 'z', .result = "ż"}, {.accent = '.', .base = 'Z', .result = "Ż"},
                {.accent = '.', .base = 'e', .result = "ė"}, {.accent = '.', .base = 'E', .result = "Ė"},
                {.accent = '.', .base = 'I', .result = "İ"}, {.accent = '.', .base = 'c', .result = "ċ"},
                {.accent = '.', .base = 'g', .result = "ġ"}, {.accent = 'u', .base = 'g', .result = "ğ"},
                {.accent = 'u', .base = 'G', .result = "Ğ"}, {.accent = 'u', .base = 'a', .result = "ă"},
                {.accent = 'u', .base = 'A', .result = "Ă"}, {.accent = 'u', .base = 'u', .result = "ŭ"},
                {.accent = 'H', .base = 'o', .result = "ő"}, {.accent = 'H', .base = 'O', .result = "Ő"},
                {.accent = 'H', .base = 'u', .result = "ű"}, {.accent = 'H', .base = 'U', .result = "Ű"},
                {.accent = 'r', .base = 'a', .result = "å"}, {.accent = 'r', .base = 'A', .result = "Å"},
                {.accent = 'r', .base = 'u', .result = "ů"}, {.accent = 'r', .base = 'U', .result = "Ů"},
                {.accent = 'k', .base = 'a', .result = "ą"}, {.accent = 'k', .base = 'A', .result = "Ą"},
                {.accent = 'k', .base = 'e', .result = "ę"}, {.accent = 'k', .base = 'E', .result = "Ę"},
                {.accent = 'd', .base = 'a', .result = "ạ"}, {.accent = 'd', .base = 'e', .result = "ẹ"},
                {.accent = 'd', .base = 'o', .result = "ọ"}, {.accent = 'd', .base = 's', .result = "ṣ"},
            }
        };

        std::array<std::array<std::string_view, 128>, marks.size()> table{};
        for (const auto &[accent, base, result]: compositions) {
            table[marks.find(accent)][static_cast<unsigned char>(base)] = result;
        }
        return table;
    }();

    Characters::Characters(syntax::Lexicon &lexicon) noexcept {
        lexicon.intern("\\ldots");
        lexicon.intern("\\LaTeX");
        lexicon.intern("\\TeX");
    }

    void Characters::operator()(syntax::Parser &parser, Context &) const {
        // Written in UTF-8, because that is what the shaper reads and what the
        // fonts are indexed by. An engine that emitted the old seven-bit
        // approximations -- two hyphens for a dash, three dots for an ellipsis
        // -- would be handing the typesetter a puzzle it has no way to solve.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 110> named{
            {
                // The characters the engine reads as instructions.
                {"\\%", "%"},
                {"\\&", "&"},
                {"\\_", "_"},
                {"\\#", "#"},
                {"\\$", "$"},
                {"\\{", "{"},
                {"\\}", "}"},
                {"\\textbackslash", "\\"},
                {"\\textasciitilde", "~"},
                {"\\textasciicircum", "^"},
                {"\\textunderscore", "_"},
                {"\\textdollar", "$"},
                {"\\textbar", "|"},
                {"\\textless", "<"},
                {"\\textgreater", ">"},

                // The marks a keyboard has no key for.
                {"\\ldots", "…"},
                {"\\dots", "…"},
                {"\\textellipsis", "…"},
                {"\\textendash", "–"},
                {"\\textemdash", "—"},
                {"\\textbullet", "•"},
                {"\\textperiodcentered", "·"},
                {"\\textdegree", "°"},
                {"\\copyright", "©"},
                {"\\textcopyright", "©"},
                {"\\pounds", "£"},
                {"\\dag", "†"},
                {"\\textdagger", "†"},
                {"\\ddag", "‡"},
                {"\\textdaggerdbl", "‡"},
                {"\\S", "§"},
                {"\\textsection", "§"},
                {"\\P", "¶"},
                {"\\textparagraph", "¶"},
                {"\\textregistered", "®"},
                {"\\texttrademark", "™"},
                {"\\textquestiondown", "¿"},
                {"\\textexclamdown", "¡"},
                {"\\textquoteleft", "‘"},
                {"\\textquoteright", "’"},
                {"\\textquotedblleft", "“"},
                {"\\textquotedblright", "”"},
                {"\\textmu", "µ"},
                {"\\textpm", "±"},
                {"\\texttimes", "×"},
                {"\\textdiv", "÷"},
                {"\\textminus", "−"},
                {"\\textlnot", "¬"},
                {"\\textperthousand", "‰"},
                {"\\textonehalf", "½"},
                {"\\textonequarter", "¼"},
                {"\\textthreequarters", "¾"},
                {"\\textonesuperior", "¹"},
                {"\\texttwosuperior", "²"},
                {"\\textthreesuperior", "³"},
                {"\\euro", "€"},
                {"\\texteuro", "€"},
                {"\\textsterling", "£"},
                {"\\textyen", "¥"},
                {"\\textcent", "¢"},
                {"\\textcurrency", "¤"},
                {"\\textcelsius", "°C"},
                {"\\textordfeminine", "ª"},
                {"\\textordmasculine", "º"},
                {"\\textbrokenbar", "¦"},
                {"\\textasteriskcentered", "∗"},
                {"\\textvisiblespace", "␣"},
                {"\\textquotesingle", "'"},
                {"\\textquotedbl", "\""},
                {"\\textleftarrow", "←"},
                {"\\textrightarrow", "→"},
                {"\\textuparrow", "↑"},
                {"\\textdownarrow", "↓"},
                {"\\textnumero", "№"},
                {"\\textbardbl", "‖"},
                {"\\guillemotleft", "«"},
                {"\\guillemotright", "»"},
                {"\\guillemetleft", "«"},
                {"\\guillemetright", "»"},
                {"\\guilsinglleft", "‹"},
                {"\\guilsinglright", "›"},
                {"\\quotedblbase", "„"},
                {"\\quotesinglbase", "‚"},
                {"\\slash", "/"},
                {"\\textfractionsolidus", "⁄"},

                // The letters of other alphabets written with a command of their own.
                {"\\i", "ı"},
                {"\\j", "ȷ"},
                {"\\o", "ø"},
                {"\\O", "Ø"},
                {"\\ss", "ß"},
                {"\\ae", "æ"},
                {"\\AE", "Æ"},
                {"\\oe", "œ"},
                {"\\OE", "Œ"},
                {"\\aa", "å"},
                {"\\AA", "Å"},
                {"\\l", "ł"},
                {"\\L", "Ł"},
                {"\\dh", "ð"},
                {"\\DH", "Ð"},
                {"\\th", "þ"},
                {"\\TH", "Þ"},
                {"\\dj", "đ"},
                {"\\DJ", "Đ"},
                {"\\ng", "ŋ"},
                {"\\NG", "Ŋ"},
                {"\\ij", "ĳ"},
                {"\\IJ", "Ĳ"},
            }
        };

        for (const auto &[name, text]: named) {
            parser.bind(name, [text](const syntax::Parser &parser) -> syntax::Node * {
                return parser.arena.compose<syntax::Node>(
                    syntax::Node::Type::Text, text, parser.mouth.lookahead().location);
            });
        }

        // The accents, over the letter after them or the group after them:
        // `Poincar\'e`, `G\"{o}del`, `\c{c}a`. The letter the accent sits on
        // is read raw -- `\i` read as the dotless i it names -- and set as the
        // one character Unicode has for the pair, or as the letter and a
        // combining mark where it has none.
        static constexpr std::array<std::string_view, 16> accents{
            "\\'", "\\`", "\\^", "\\\"", "\\~", "\\=", "\\.", "\\u", "\\v", "\\H", "\\c", "\\k", "\\r",
            "\\d", "\\b", "\\t",
        };
        for (const std::string_view name: accents) {
            const char accent = name[1];
            const std::size_t row = marks.find(accent);
            parser.bind(name, [accent, row](const syntax::Parser &parser) -> syntax::Node * {
                syntax::Mouth &mouth = parser.mouth;
                const memory::Location origin = mouth.lookahead().location;

                const auto letter = [](const syntax::Token &token) -> std::string_view {
                    if (token.text == "\\i") return "ı";
                    if (token.text == "\\j") return "ȷ";
                    return token.text;
                };

                syntax::Token next = mouth.read();
                while (next.category == syntax::Catcodes::Category::Space) next = mouth.read();

                std::string base;
                if (next.is(syntax::Catcodes::Category::Group, '{')) {
                    for (syntax::Token token = mouth.read();
                         !token.empty() && !token.is(syntax::Catcodes::Category::Group, '}');
                         token = mouth.read()) {
                        base += letter(token);
                    }
                } else if (!next.empty()) {
                    base = letter(next);
                }

                std::string text;
                if (const auto first = base.empty() ? 0u : static_cast<unsigned char>(base.front());
                    first != 0 && first < 128 && !composed[row][first].empty()) {
                    text = std::format("{}{}", composed[row][first], base.substr(1));
                }
                if (text.empty()) {
                    // The letter's own bytes, then the mark: a combining mark
                    // follows the character it sits on.
                    std::size_t span = base.empty() ? 0 : 1;
                    while (span < base.size() && (static_cast<unsigned char>(base[span]) & 0xC0) == 0x80) ++span;
                    // The combining mark each accent command sets.
                    static constexpr std::array<std::pair<char, std::string_view>, 16> combining{
                        {
                            {'\'', "\xCC\x81"}, // U+0301 acute
                            {'`', "\xCC\x80"}, // U+0300 grave
                            {'^', "\xCC\x82"}, // U+0302 circumflex
                            {'"', "\xCC\x88"}, // U+0308 diaeresis
                            {'~', "\xCC\x83"}, // U+0303 tilde
                            {'=', "\xCC\x84"}, // U+0304 macron
                            {'.', "\xCC\x87"}, // U+0307 dot above
                            {'u', "\xCC\x86"}, // U+0306 breve
                            {'v', "\xCC\x8C"}, // U+030C caron
                            {'H', "\xCC\x8B"}, // U+030B double acute
                            {'c', "\xCC\xA7"}, // U+0327 cedilla
                            {'k', "\xCC\xA8"}, // U+0328 ogonek
                            {'r', "\xCC\x8A"}, // U+030A ring above
                            {'d', "\xCC\xA3"}, // U+0323 dot below
                            {'b', "\xCC\xB1"}, // U+0331 macron below
                            {'t', "\xCD\xA1"}, // U+0361 double inverted breve
                        }
                    };
                    const auto found = std::ranges::find(combining, accent, &std::pair<char, std::string_view>::first);
                    const std::string_view mark = found != combining.end() ? found->second : std::string_view{};
                    text = std::format("{}{}{}", base.substr(0, span), mark, base.substr(span));
                }

                return parser.arena.compose<syntax::Node>(
                    syntax::Node::Type::Text, parser.arena.copy(text), origin);
            });
        }

        // mhchem's \ce{2H2O -> 2H2 + O2}, \ce{SO4^2-}: a formula as chemistry
        // writes one, rewritten as the text that sets it. A count after an
        // element or a bracket is lowered, a charge after ^ -- or a sign that
        // ends an element -- raised, an arrow drawn as one; a number that
        // starts a term stays a coefficient.
        parser.bind("\\ce", [](const syntax::Parser &parser) -> syntax::Node * {
            syntax::Mouth &mouth = parser.mouth;
            const std::string formula = syntax::Argument::text(mouth);
            std::string written;
            const auto element = [](const char letter) {
                return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') || letter == ')' ||
                       letter == ']';
            };
            for (std::size_t at = 0; at < formula.size();) {
                const char letter = formula[at];
                if (formula.substr(at).starts_with("<=>")) {
                    written += "$\\rightleftharpoons$";
                    at += 3;
                } else if (formula.substr(at).starts_with("->")) {
                    written += "$\\rightarrow$";
                    at += 2;
                } else if (formula.substr(at).starts_with("<-")) {
                    written += "$\\leftarrow$";
                    at += 2;
                } else if (letter >= '0' && letter <= '9' && at > 0 && element(formula[at - 1])) {
                    std::string count;
                    while (at < formula.size() && formula[at] >= '0' && formula[at] <= '9') count += formula[at++];
                    written += std::format("$_{{{}}}$", count);
                } else if (letter == '^') {
                    std::string charge;
                    ++at;
                    if (at < formula.size() && formula[at] == '{') {
                        while (++at < formula.size() && formula[at] != '}') charge += formula[at];
                        ++at;
                    } else {
                        while (at < formula.size() && formula[at] != ' ') charge += formula[at++];
                    }
                    written += std::format("$^{{{}}}$", charge);
                } else if ((letter == '+' || letter == '-') && at > 0 && element(formula[at - 1]) &&
                           (at + 1 == formula.size() || formula[at + 1] == ' ')) {
                    written += std::format("$^{{{}}}$", letter);
                    ++at;
                } else {
                    written += letter;
                    ++at;
                }
            }
            mouth.ingest(parser.arena.copy(written));
            return nullptr;
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound named characters");
    }
}
