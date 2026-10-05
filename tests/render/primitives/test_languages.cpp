#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Languages primitive, through babel and polyglossia: a language's
// captions and date, the language a list of them means, a block in another
// language and what it goes back to, how its words break, which way its
// paragraphs read, and the faces its script is drawn in.

/// One document as the engine set it: whether it reported nothing, what it
/// reported, its PDF, and its text with the spaces and line ends taken out.
struct Result {
    bool clean{false};
    std::string errors{};
    std::string pdf{};
    std::string text{};
};

/// Typesets a whole document, as written.
static Result typeset(const std::string_view document) {
    static const std::filesystem::path assets = latex::locate(__FILE__);
    Result result;
    std::ostringstream errors;
    std::vector<std::string> pages;
    result.clean = latex::typeset(assets, document, result.pdf, {}, errors, &pages);
    result.errors = errors.str();
    for (std::string& page : pages) {
        std::erase_if(page, [](const char letter) { return letter == ' ' || letter == '\n'; });
        result.text += page;
    }
    if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
    return result;
}

/// An article with a preamble and a body.
static Result article(const std::string_view preamble, const std::string_view body) {
    return typeset("\\documentclass{article}" + std::string(preamble) + "\\begin{document}" + std::string(body) +
                   "\\end{document}");
}

/// Whether a text holds another.
static bool holds(const std::string_view text, const std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

int main() {
    // --- Captions and dates ------------------------------------------------------------
    {
        const Result russian = article("\\usepackage[russian]{babel}",
                                       "\\figurename,\\tablename,\\abstractname,\\languagename:\\today");
        assert((russian.clean && holds(russian.text, "Рис.,Таблица,Аннотация,russian:") &&
                holds(russian.text, "г.")) && "babel's language says its captions and its date its own way");
        const Result last = article("\\usepackage[russian,english]{babel}", "\\figurename");
        assert((last.clean && holds(last.text, "Figure")) && "of several languages, the last is the document's");
        const Result main = article("\\usepackage[main=russian,english]{babel}", "\\figurename");
        assert((main.clean && holds(main.text, "Рис.")) && "unless main= names another");
        const Result plain = article("\\usepackage{babel}", "\\figurename");
        assert((plain.clean && holds(plain.text, "Figure")) && "with none named, the captions are English");
    }

    // --- Choosing another, and going back ------------------------------------------------
    {
        const Result chosen = article("\\usepackage[english,german]{babel}",
                                      "\\figurename;\\selectlanguage{english}\\figurename");
        assert((chosen.clean && holds(chosen.text, "Abbildung;Figure")) && "\\selectlanguage chooses another");
        const Result block = article("\\usepackage[french,english]{babel}",
                                     "\\begin{otherlanguage}{french}\\abstractname\\end{otherlanguage};\\abstractname");
        assert((block.clean && holds(block.text, "Résumé;Abstract")) &&
               "a block in another language goes back to the one before it as it closes");
        const Result phrase = article("\\usepackage[english,russian]{babel}",
                                      "\\foreignlanguage{english}{word};\\figurename");
        assert((phrase.clean && holds(phrase.text, "word;Рис.")) &&
               "a phrase in another language changes its words' breaks, not the captions");
        const Result unknown = article("\\usepackage[klingon]{babel}", "x");
        assert((unknown.clean && holds(unknown.errors, "No language known as 'klingon'")) &&
               "a language not known is reported, and the document still made");
    }

    // --- polyglossia ------------------------------------------------------------------
    {
        const Result glossia = article("\\usepackage{polyglossia}\\setmainlanguage{russian}\\setotherlanguage{english}",
                                       "\\figurename;\\textenglish{word};\\begin{english}\\figurename\\end{english}");
        assert((glossia.clean && holds(glossia.text, "Рис.;word;Figure")) &&
               "polyglossia names its languages by command, each with a command and a block of its own");
    }

    // --- How words break ------------------------------------------------------------------
    {
        const std::string narrow = "\\begin{minipage}{2cm}Электрификация государственного\\end{minipage}";
        const Result broken = article("\\usepackage[russian]{babel}", narrow);
        const Result whole = article("", narrow);
        assert((broken.clean && holds(broken.text, "-") && !holds(whole.text, "-")) &&
               "a Russian word breaks by Russian patterns, and by no others");
    }

    // --- The faces a script is drawn in ------------------------------------------------------
    {
        const Result cyrillic = article("\\usepackage[russian]{babel}", "Слово");
        assert((cyrillic.clean && holds(cyrillic.pdf, "NewCM10-Regular")) &&
               "Cyrillic is Computer Modern's own, from New Computer Modern");
        const Result arabic = article("\\usepackage{polyglossia}\\setmainlanguage{arabic}",
                                      "\\section{مقدمة}العربية لغة. \\figurename");
        assert((arabic.clean) && "an Arabic document is set, its paragraphs right to left");
        if (holds(arabic.pdf, "/CIDFontType2")) {
            assert((holds(arabic.pdf, "/FontFile2") && holds(arabic.pdf, "/CIDToGIDMap /Identity")) &&
                   "a system's TrueType face is embedded as TrueType");
        }
    }

    // --- A language's digits and spaces ------------------------------------------------------
    {
        const Result eastern = article("\\usepackage{polyglossia}\\setmainlanguage{arabic}", "عام 2024");
        assert((eastern.clean && holds(eastern.text, "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA4")) &&
               "Arabic's own digits, as polyglossia sets them, a number read left to right");
        const Result western = article("\\usepackage{polyglossia}\\setmainlanguage[numerals=maghrib]{arabic}", "عام 2024");
        assert((western.clean && holds(western.text, "2024")) && "and the digits as typed when told numerals=maghrib");
        const Result persian = article("\\usepackage{polyglossia}\\setmainlanguage{persian}", "سال 1403");
        assert((persian.clean && holds(persian.text, "\xDB\xB1\xDB\xB4\xDB\xB0\xDB\xB3")) && "Persian's digits");
        const Result quoted = article("\\usepackage{polyglossia}\\setmainlanguage{english}\\setotherlanguage{arabic}",
                                      "Year 2024 \\textarabic{عام 2024}");
        assert((quoted.clean && holds(quoted.text, "Year2024") && holds(quoted.text, "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA4")) &&
               "an Arabic phrase in English text, its number in Arabic's digits and the rest as typed");
        const Result french = article("\\usepackage[french]{babel}", "Vraiment ? Oui : « bien ».");
        assert((french.clean && holds(french.text, "Vraiment?Oui:«bien».")) && "French's marks, set");
    }

    return 0;
}
