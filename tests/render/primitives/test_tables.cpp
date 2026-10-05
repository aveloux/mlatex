#include "latex.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The Tables primitive: tabular and its kin -- every column letter, rules
// between rows and columns, booktabs' rules, cells over several columns,
// tabularx and tabular* stretched to a width, a table placed on its [t],
// [c] or [b] row, \newcolumntype -- and plain TeX's \halign.

/// One document as the engine set it: whether it reported nothing, what it
/// reported, its PDF, and each page's text -- and every page's together --
/// with the spaces and line ends taken out, which is what a check compares.
struct Result {
    bool clean{false};
    std::string errors{};
    std::string pdf{};
    std::vector<std::string> pages{};
    std::string text{};
};

/// Typesets a whole document, as written, with what a program hands in.
static Result typeset(const std::string_view document, const latex::Host& host = {}) {
    static const std::filesystem::path assets = latex::locate(__FILE__);
    Result result;
    std::ostringstream errors;
    result.clean = latex::typeset(assets, document, result.pdf, host, errors, &result.pages);
    result.errors = errors.str();
    for (std::string& page : result.pages) {
        std::erase_if(page, [](const char letter) { return letter == ' ' || letter == '\n'; });
        result.text += page;
    }
    return result;
}

/// Typesets a body in an article, after a preamble, and says what was
/// reported when anything was.
static Result article(const std::string_view preamble, const std::string_view body) {
    Result result = typeset("\\documentclass{article}" + std::string(preamble) + "\\begin{document}" +
                            std::string(body) + "\\end{document}");
    if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
    return result;
}

/// Whether a text holds another.
static bool holds(const std::string_view text, const std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

/// How many times a text says something.
static std::size_t count(const std::string_view text, const std::string_view part) {
    std::size_t found = 0;
    for (std::size_t at = text.find(part); at != std::string_view::npos; at = text.find(part, at + part.size())) {
        ++found;
    }
    return found;
}

int main() {
    {
        const Result result = article("", "\\begin{tabular}{|l|c|r|}\\hline a & b & c \\\\\\hline one & two & three "
                                          "\\\\\\cline{1-2}\\multicolumn{2}{|c|}{span} & x\\\\\\hline\\end{tabular}");
        assert((result.clean && holds(result.text, "abconetwothreespanx")) && "a table's cells, in reading order");
        assert((count(result.pdf, " re f") >= 8) && "its rules");
    }
    {
        const Result result = article("\\usepackage{booktabs}", "\\begin{tabular}{ll}\\toprule A & B\\\\\\midrule "
                                                                "1 & 2\\\\\\cmidrule(lr){1-2}3 & 4\\\\\\bottomrule"
                                                                "\\end{tabular}");
        assert((result.clean && holds(result.text, "AB1234")) && "booktabs' rules");
    }
    {
        const Result result = article("\\usepackage{tabularx,array}",
                                      "\\begin{tabularx}{\\textwidth}{lX}a & a long cell that wraps in the room the "
                                      "table has left over for it once the first column is set\\end{tabularx}"
                                      "\\begin{tabular}{p{2cm}>{\\bfseries}c}wrapped text here & b\\end{tabular}"
                                      "\\newcolumntype{C}{>{\\centering\\arraybackslash}p{2cm}}"
                                      "\\begin{tabular}{C}custom\\end{tabular}");
        assert((result.clean && holds(result.text, "alongcell") && holds(result.text, "custom")) &&
               "tabularx, paragraph columns, a declaration before a column, and a column type of the document's own");
    }
    {
        const Result result = article("\\usepackage{multirow}", "\\begin{tabular}{ll}\\multirow{2}{*}{tall} & a\\\\ & b"
                                                                "\\end{tabular}");
        assert((result.clean && holds(result.text, "tall")) && "a cell over several rows");
    }
    {
        const Result result = article("", "x \\begin{tabular}[t]{c}top\\\\row\\end{tabular} "
                                          "\\begin{tabular}[b]{c}up\\\\bottom\\end{tabular} "
                                          "\\begin{tabular}{c}mid\\\\dle\\end{tabular} y");
        assert((result.clean && holds(result.text, "xtoprowupbottommiddley")) &&
               "tables are boxes in the line, beside the words around them");
        // Each stands on the line's baseline by its first row, its last, or
        // the axis through its middle: `top`, `bottom` and `x` share one.
        assert((count(result.pdf, " 144.9 Tm") == 3) && "on its first row, its last, or its middle");
    }
    {
        const Result result = article("", "\\halign{{a} & {b} & {c} \\cr {one} & {two} & {three} \\cr}");
        assert((result.clean && holds(result.text, "abconetwothree")) && "plain TeX's \\halign");
    }
    {
        const Result result = article("", "\\begin{tabbing}Name \\= Value \\\\ a \\> b \\\\ \\end{tabbing}"
                                          "After \\=o.");
        assert((result.clean && holds(result.text, "NameValueab")) && "tabbing's lines set at its stops");
        assert((holds(result.text, "After\xC5\x8D.")) && "and \\= a macron again once it closes");
    }
    {
        const Result result = typeset("\\documentclass{article}\\begin{document}\\tabular\\end{document}");
        assert((!result.clean && holds(result.errors, "\\tabular outside a tabular block")) &&
               "a table's body outside one");
    }
    {
        // A long table over pages: its rows all set, a page ending between
        // two of them; its head atop every page, its first head on the
        // first; its foot under the last row of each page but the last.
        std::string rows;
        for (int index = 1; index <= 90; ++index) {
            rows += "r" + std::to_string(index) + " & v" + std::to_string(index) + " \\\\\n";
        }
        const Result result = article("\\usepackage{longtable}",
                                      "Before.\n\n\\begin{longtable}{lr}\\caption{Prices}\\\\ First & Head \\\\ "
                                      "\\endfirsthead Item & Value \\\\ \\endhead Continued \\\\ \\endfoot "
                                      "Closing \\\\ \\endlastfoot " + rows + "\\end{longtable}After.");
        assert((result.clean && result.pages.size() >= 2) && "a long table runs over pages");
        assert((holds(result.text, "Table1:Prices") && !holds(result.text, "Figure")) && "its caption a table's");
        for (int index = 1; index <= 90; ++index) {
            const std::string cell = "r" + std::to_string(index) + "v" + std::to_string(index);
            assert((holds(result.text, cell)) && "every row set, none lost past a page's foot");
        }
        assert((count(result.text, "FirstHead") == 1) && "the first head once, on the first page");
        assert((count(result.text, "ItemValue") == result.pages.size() - 1) && "the head atop every other");
        assert((count(result.text, "Continued") == result.pages.size() - 1) && "the foot closing each but the last");
        assert((count(result.text, "Closing") == 1 && holds(result.pages.back(), "Closing")) &&
               "the last foot on the last page");
        assert((!holds(result.pages.front(), "ItemValue")) && "and not the head the first page has its own for");
    }
    {
        // A plain tabular is one box, as LaTeX's is.
        const Result result = article("", "\\begin{tabular}{l}a \\\\ b \\\\ \\end{tabular}");
        assert((result.clean && result.pages.size() == 1 && holds(result.text, "ab")) && "a tabular set whole");
    }
    {
        const Result result = article("\\usepackage{multirow}",
                                      "\\begin{tabular}{|c|c|}\\multirow{2}{*}{Both} & a \\\\ & b \\\\ "
                                      "\\multirow[t]{2}{*}{Top} & c \\\\ & d \\\\ \\end{tabular} and "
                                      "\\multirow{2}{*}{plain}.");
        assert((result.clean && holds(result.text, "Both") && holds(result.text, "Top") &&
                holds(result.text, "plain.")) && "\\multirow in a cell, and its text alone outside one");
    }
    {
        const Result result = article("\\usepackage{tabu}", "\\begin{tabu} to \\linewidth {X[l] X[c] X[r]}"
                                                            "a & b & c \\\\ \\end{tabu}");
        assert((result.clean && holds(result.text, "abc")) && "tabu's X columns, their sides in brackets");
    }
    return 0;
}
