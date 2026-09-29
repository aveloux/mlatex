/// @file
/// @brief Named characters: the ones a document cannot type for itself.
///
/// Every one of these is the same handler over a different string, so they are
/// bound from one table. The text is returned as a node rather than pushed
/// back through the lexer, which is what keeps a `%` from being read as the
/// comment it would otherwise start.
#include "render/primitives/symbols.hpp"
#include "logger.hpp"
#include "syntax/argument.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace render::primitives {

    /// @brief One accented letter Unicode spells as a character of its own.
    struct Composition {
        char accent;               ///< The accent command's character: `'` for `\\'`.
        char base;                 ///< The letter under it.
        std::string_view result;   ///< The one character both make, in UTF-8.
    };

    /// Every accented Latin letter a European language writes, precomposed:
    /// a face draws `é` as one glyph, spaced and kerned as one, where a
    /// letter and a combining mark would be two it has to be told to stack.
    static constexpr std::array<Composition, 118> compositions{{
        {'\'', 'a', "á"}, {'\'', 'e', "é"}, {'\'', 'i', "í"}, {'\'', 'o', "ó"}, {'\'', 'u', "ú"},
        {'\'', 'y', "ý"}, {'\'', 'A', "Á"}, {'\'', 'E', "É"}, {'\'', 'I', "Í"}, {'\'', 'O', "Ó"},
        {'\'', 'U', "Ú"}, {'\'', 'Y', "Ý"}, {'\'', 'c', "ć"}, {'\'', 'n', "ń"}, {'\'', 's', "ś"},
        {'\'', 'z', "ź"}, {'\'', 'C', "Ć"}, {'\'', 'N', "Ń"}, {'\'', 'S', "Ś"}, {'\'', 'Z', "Ź"},
        {'`', 'a', "à"}, {'`', 'e', "è"}, {'`', 'i', "ì"}, {'`', 'o', "ò"}, {'`', 'u', "ù"},
        {'`', 'A', "À"}, {'`', 'E', "È"}, {'`', 'I', "Ì"}, {'`', 'O', "Ò"}, {'`', 'U', "Ù"},
        {'^', 'a', "â"}, {'^', 'e', "ê"}, {'^', 'i', "î"}, {'^', 'o', "ô"}, {'^', 'u', "û"},
        {'^', 'A', "Â"}, {'^', 'E', "Ê"}, {'^', 'I', "Î"}, {'^', 'O', "Ô"}, {'^', 'U', "Û"},
        {'"', 'a', "ä"}, {'"', 'e', "ë"}, {'"', 'i', "ï"}, {'"', 'o', "ö"}, {'"', 'u', "ü"},
        {'"', 'y', "ÿ"}, {'"', 'A', "Ä"}, {'"', 'E', "Ë"}, {'"', 'I', "Ï"}, {'"', 'O', "Ö"},
        {'"', 'U', "Ü"}, {'"', 'Y', "Ÿ"},
        {'~', 'a', "ã"}, {'~', 'n', "ñ"}, {'~', 'o', "õ"}, {'~', 'A', "Ã"}, {'~', 'N', "Ñ"},
        {'~', 'O', "Õ"},
        {'c', 'c', "ç"}, {'c', 'C', "Ç"}, {'c', 's', "ş"}, {'c', 'S', "Ş"}, {'c', 't', "ţ"},
        {'c', 'T', "Ţ"},
        {'v', 'c', "č"}, {'v', 'C', "Č"}, {'v', 's', "š"}, {'v', 'S', "Š"}, {'v', 'z', "ž"},
        {'v', 'Z', "Ž"}, {'v', 'r', "ř"}, {'v', 'R', "Ř"}, {'v', 'e', "ě"}, {'v', 'E', "Ě"},
        {'v', 'n', "ň"}, {'v', 'N', "Ň"}, {'v', 'd', "ď"}, {'v', 'D', "Ď"}, {'v', 't', "ť"},
        {'v', 'T', "Ť"},
        {'=', 'a', "ā"}, {'=', 'e', "ē"}, {'=', 'i', "ī"}, {'=', 'o', "ō"}, {'=', 'u', "ū"},
        {'=', 'A', "Ā"}, {'=', 'E', "Ē"}, {'=', 'I', "Ī"}, {'=', 'O', "Ō"}, {'=', 'U', "Ū"},
        {'.', 'z', "ż"}, {'.', 'Z', "Ż"}, {'.', 'e', "ė"}, {'.', 'E', "Ė"}, {'.', 'I', "İ"},
        {'.', 'c', "ċ"}, {'.', 'g', "ġ"},
        {'u', 'g', "ğ"}, {'u', 'G', "Ğ"}, {'u', 'a', "ă"}, {'u', 'A', "Ă"}, {'u', 'u', "ŭ"},
        {'H', 'o', "ő"}, {'H', 'O', "Ő"}, {'H', 'u', "ű"}, {'H', 'U', "Ű"},
        {'r', 'a', "å"}, {'r', 'A', "Å"}, {'r', 'u', "ů"}, {'r', 'U', "Ů"},
        {'k', 'a', "ą"}, {'k', 'A', "Ą"}, {'k', 'e', "ę"}, {'k', 'E', "Ę"},
        {'d', 'a', "ạ"}, {'d', 'e', "ẹ"}, {'d', 'o', "ọ"}, {'d', 's', "ṣ"},
    }};

    /// The accent commands' own characters, `\\'` through `\\t`, in the order
    /// their rows of #composed are kept.
    static constexpr std::string_view marks = "'`^\"~=.uvHckrdbt";

    /// Every precomposed letter, found by its accent's row and its base
    /// letter's byte in one load, rather than by a search of the list above.
    static constexpr auto composed = [] {
        std::array<std::array<std::string_view, 128>, marks.size()> table{};
        for (const auto& [accent, base, result] : compositions) {
            table[marks.find(accent)][static_cast<unsigned char>(base)] = result;
        }
        return table;
    }();

    Symbols::Symbols(syntax::Lexicon& lexicon) noexcept {
        lexicon.intern("\\ldots");
        lexicon.intern("\\LaTeX");
        lexicon.intern("\\TeX");
    }

    void Symbols::operator()(syntax::Parser& parser, Context&) const {
        // Written in UTF-8, because that is what the shaper reads and what the
        // fonts are indexed by. An engine that emitted the old seven-bit
        // approximations -- two hyphens for a dash, three dots for an ellipsis
        // -- would be handing the typesetter a puzzle it has no way to solve.
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 110> named{{
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
        }};

        for (const auto& [name, text] : named) {
            parser.bind(name, [text](const syntax::Parser& parser) -> syntax::Node* {
                return parser.arena().compose<syntax::Node>(
                    syntax::Node::Type::Text, text, parser.mouth().lookahead().location);
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
        for (const std::string_view name : accents) {
            const char accent = name[1];
            const std::size_t row = marks.find(accent);
            parser.bind(name, [accent, row](const syntax::Parser& parser) -> syntax::Node* {
                syntax::Mouth& mouth = parser.mouth();
                const memory::Location origin = mouth.lookahead().location;

                const auto letter = [](const syntax::Token& token) -> std::string_view {
                    if (token.text == "\\i") return "ı";
                    if (token.text == "\\j") return "ȷ";
                    return token.text;
                };

                syntax::Token next = mouth.read();
                while (next.category == syntax::CatCodes::Category::Space) next = mouth.read();

                std::string base;
                if (next.is(syntax::CatCodes::Category::Group, '{')) {
                    for (syntax::Token token = mouth.read();
                         !token.empty() && !token.is(syntax::CatCodes::Category::Group, '}');
                         token = mouth.read()) {
                        base += letter(token);
                    }
                } else if (!next.empty()) {
                    base = letter(next);
                }

                std::string text;
                if (const auto first = base.empty() ? 0u : static_cast<unsigned char>(base.front());
                    first != 0 && first < 128 && !composed[row][first].empty()) {
                    text = std::string(composed[row][first]) + base.substr(1);
                }
                if (text.empty()) {
                    // The letter's own bytes, then the mark: a combining mark
                    // follows the character it sits on.
                    std::size_t span = base.empty() ? 0 : 1;
                    while (span < base.size() && (static_cast<unsigned char>(base[span]) & 0xC0) == 0x80) ++span;
                    const std::string_view mark = accent == '\'' ? "\xCC\x81"   // U+0301 acute
                                                  : accent == '`'  ? "\xCC\x80"   // U+0300 grave
                                                  : accent == '^'  ? "\xCC\x82"   // U+0302 circumflex
                                                  : accent == '"'  ? "\xCC\x88"   // U+0308 diaeresis
                                                  : accent == '~'  ? "\xCC\x83"   // U+0303 tilde
                                                  : accent == '='  ? "\xCC\x84"   // U+0304 macron
                                                  : accent == '.'  ? "\xCC\x87"   // U+0307 dot above
                                                  : accent == 'u'  ? "\xCC\x86"   // U+0306 breve
                                                  : accent == 'v'  ? "\xCC\x8C"   // U+030C caron
                                                  : accent == 'H'  ? "\xCC\x8B"   // U+030B double acute
                                                  : accent == 'c'  ? "\xCC\xA7"   // U+0327 cedilla
                                                  : accent == 'k'  ? "\xCC\xA8"   // U+0328 ogonek
                                                  : accent == 'r'  ? "\xCC\x8A"   // U+030A ring above
                                                  : accent == 'd'  ? "\xCC\xA3"   // U+0323 dot below
                                                  : accent == 'b'  ? "\xCC\xB1"   // U+0331 macron below
                                                  : accent == 't'  ? "\xCD\xA1"   // U+0361 double inverted breve
                                                                   : "";
                    text = base.substr(0, span) + std::string(mark) + base.substr(span);
                }

                return parser.arena().compose<syntax::Node>(
                    syntax::Node::Type::Text, parser.arena().copy(text), origin);
            });
        }

        // mhchem's \ce{2H2O -> 2H2 + O2}, \ce{SO4^2-}: a formula as chemistry
        // writes one, rewritten as the text that sets it. A count after an
        // element or a bracket is lowered, a charge after ^ -- or a sign that
        // ends an element -- raised, an arrow drawn as one; a number that
        // starts a term stays a coefficient.
        parser.bind("\\ce", [](syntax::Parser& parser) -> syntax::Node* {
            syntax::Mouth& mouth = parser.mouth();
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
                    written += "$_{" + count + "}$";
                } else if (letter == '^') {
                    std::string charge;
                    ++at;
                    if (at < formula.size() && formula[at] == '{') {
                        while (++at < formula.size() && formula[at] != '}') charge += formula[at];
                        ++at;
                    } else {
                        while (at < formula.size() && formula[at] != ' ') charge += formula[at++];
                    }
                    written += "$^{" + charge + "}$";
                } else if ((letter == '+' || letter == '-') && at > 0 && element(formula[at - 1]) &&
                           (at + 1 == formula.size() || formula[at + 1] == ' ')) {
                    written += std::string("$^{") + letter + "}$";
                    ++at;
                } else {
                    written += letter;
                    ++at;
                }
            }
            mouth.ingest(parser.arena().copy(written));
            return nullptr;
        });

        Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Bound named characters");
    }

}
