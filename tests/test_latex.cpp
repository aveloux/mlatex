#include "latex.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The engine: latex::typeset, a document in and a PDF out, and
// latex::Session, the one interface every binding shares -- values,
// commands and files a program hands in -- two passes for what only pages
// can say, what a document got wrong reported and a PDF made all the same,
// and the research paper in build/, whole.

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

/// A one-pixel PNG, for a picture handed in from memory.
static std::string dot() {
    static constexpr unsigned char bytes[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x64, 0x60, 0xF8, 0x5F,
        0x0F, 0x00, 0x02, 0x87, 0x01, 0x80, 0xEB, 0x47, 0xBA, 0x92, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    return std::string(reinterpret_cast<const char*>(bytes), sizeof bytes);
}

int main() {
    // --- A session and what it holds -------------------------------------------------
    {
        latex::Session session(latex::locate(__FILE__));
        assert((session.assets == latex::locate(__FILE__)) && "a session reads the assets it was given");

        session.set("customer", "Acme");
        session.set("customer", "Globex");
        session.set("total", "12");
        assert((session.host.variables.size() == 2 && session.host.variables[0].second == "Globex") &&
               "setting a name again replaces it");
        session.unset("total");
        session.unset("never");
        assert((session.host.variables.size() == 1) && "a value is taken back, and taking back none is nothing");

        int calls = 0;
        session.define({.name = "\\shout", .arity = 1, .handler = [&calls](auto arguments) {
            ++calls;
            return arguments[0] + "!";
        }});
        assert((session.host.commands.size() == 1 && session.host.commands[0].name == "shout") &&
               "a command is kept by its bare name");
        session.define({.name = "shout", .arity = 1, .handler = [&calls](auto arguments) {
            calls += 10;
            return arguments[0];
        }});
        assert((session.host.commands.size() == 1) &&
               "defining it again, with or without its backslash, replaces it");

        session.provide("house/main.mtex", "\\define\\motto{Onward}");
        session.provide("house/main.mtex", "\\define\\motto{Upward}");
        assert((session.host.files.size() == 1 && session.host.files.at("house/main.mtex").contains("Upward")) &&
               "a file handed in again replaces it");

        const bool made = session.typeset("\\usepackage{house}\\begin{document}\\motto\\ \\shout{\\variable{customer}}"
                                          "\\end{document}");
        assert((made && session.error.empty() && session.pdf.starts_with("%PDF-")) &&
               "a document uses everything the session holds, and makes a PDF");
        assert((calls == 10) && "the command called is the one defined last");

        session.forget("\\shout");
        session.withdraw("house/main.mtex");
        assert((session.host.commands.empty() && session.host.files.empty()) &&
               "commands and files are taken back");
        assert((session.typeset("\\usepackage{house}\\begin{document}x\\end{document}") &&
               holds(session.error, "warning: File `house.sty' not found")) &&
               "and missed by name once they are, as a warning");

        latex::Session lost(latex::locate(__FILE__).parent_path() / "no-such-assets");
        assert((!lost.typeset("\\begin{document}x\\end{document}") && lost.pdf.empty() && !lost.error.empty()) &&
               "a session with no fonts makes nothing, and says why");
    }

    // --- A program's own command, package and picture ------------------------------------
    {
        std::string asked;
        latex::Host host;
        host.variables.emplace_back("account", "A-1042");
        host.commands.push_back({.name = "balance", .arity = 1, .handler = [&asked](const auto arguments) {
            asked = arguments[0];
            return std::string("\\textbf{12.50}");
        }});
        host.files["letterhead/main.mtex"] = "\\define\\company{Acme Ltd.}\n";
        host.files["dot.png"] = dot();

        const Result result = typeset("\\documentclass{article}\\usepackage{letterhead,graphicx}\\begin{document}"
                                      "\\company{} owes \\balance{\\variable{account}}."
                                      "\\includegraphics[width=1cm]{dot.png}\\end{document}",
                                      host);
        assert((result.clean && holds(result.text, "AcmeLtd.owes12.50.")) && "a program's command and package");
        assert((asked == "A-1042") && "a command's argument arrives expanded");
        assert((holds(result.pdf, "/Subtype /Image")) && "and its picture is placed");
    }

    // --- What only pages can say, from a second pass ------------------------------------
    {
        const Result result = article("", "See page \\pageref{later}.\\newpage Here.\\label{later}");
        assert((result.clean && holds(result.text, "Seepage2.")) &&
               "a forward page reference is filled from a second pass");
    }

    // --- A bibliography set as a document of its own ------------------------------------
    {
        const std::filesystem::path bib = std::filesystem::temp_directory_path() / "latex-entries.bib";
        std::ofstream(bib, std::ios::binary)
            << "@book{lamport94, author = {Leslie Lamport}, title = {LaTeX}, publisher = {Addison-Wesley}, year = 1994}\n"
               "@misc{never, author = {Ada Lovelace}, title = {Notes}, year = 1843}\n";
        std::filesystem::path pdf = bib;
        pdf.replace_extension(".pdf");
        std::ostringstream errors;
        std::vector<std::string> pages;
        const bool clean = latex::compose(latex::locate(__FILE__), bib, pdf, {}, nullptr, errors, &pages);
        std::string text;
        for (const std::string& page : pages) text += page;
        std::erase_if(text, [](const char letter) { return letter == ' ' || letter == '\n'; });
        if (!clean) std::fprintf(stderr, "%s", errors.str().c_str());
        assert((clean && std::filesystem::exists(pdf)) && "a .bib file sets as a document");
        assert((holds(text, "References") && holds(text, "LeslieLamport") && holds(text, "AdaLovelace")) &&
               "every entry it holds, listed though none is cited");
    }

    // --- Mistakes ----------------------------------------------------------------------
    {
        const Result undefined = article("", "\\nosuchcommand");
        assert((!undefined.clean && holds(undefined.errors, "\\nosuchcommand")) && "an undefined command is named");
        const Result open = typeset("\\documentclass{article}\\begin{document}\\begin{quote}a\\end{document}");
        assert((holds(open.errors, "ended by \\end{document}") || holds(open.errors, "no legal \\end found")) && "a block left open is reported");
        const Result wrong = typeset("\\documentclass{article}\\begin{document}\\begin{quote}a\\end{center}"
                                     "\\end{document}");
        assert((holds(wrong.errors, "ended by \\end")) && "a block closed as another");
        const Result partial = article("", "before \\nosuchcommand after");
        assert((partial.pdf.starts_with("%PDF-") && holds(partial.text, "beforeafter")) &&
               "a document with a mistake still makes a PDF of what it could");
    }

    // --- Every package's own document --------------------------------------------------
    // build/packages holds one document for every package and class the engine
    // reads, each using what it is loaded for; every one has to come out with
    // no error and no package or command the engine does not know.
    {
        const std::filesystem::path folder = latex::locate(__FILE__).parent_path() / "build" / "packages";
        std::size_t read = 0;
        std::string failed;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
            if (entry.path().extension() != ".mtex") continue;
            std::ifstream file(entry.path(), std::ios::binary);
            const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            const Result result = typeset(source);
            ++read;
            const bool unknown = holds(result.errors, "not found") || holds(result.errors, "undefined");
            if (!result.clean || unknown || result.pdf.empty()) {
                failed += entry.path().filename().string() + ":\n" + result.errors;
            }
        }
        if (!failed.empty()) std::fprintf(stderr, "%s", failed.c_str());
        assert((read > 500) && "every package and class has its document");
        assert((failed.empty()) && "and every one comes out clean");
    }

    // --- The paper, whole -------------------------------------------------------------
    {
        std::ifstream file(latex::locate(__FILE__).parent_path() / "build" / "main.mtex", std::ios::binary);
        const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        assert((!source.empty()) && "the paper is found");

        const Result result = typeset(source);
        assert((result.clean) && "the paper typesets without a report");
        if (!result.clean) std::fprintf(stderr, "%s", result.errors.c_str());
        assert((result.pages.size() == 11) && "and runs to eleven pages, its appendices of checks with it");
        assert((std::ranges::none_of(result.pages, [](const std::string& page) { return page.size() < 40; })) &&
               "no page left all but empty");
        assert((holds(result.text, "EnergyEstimatesandNumericalEvidence")) && "its title");
        assert((holds(result.text, "Definition2.1(Weaksolution).")) && "a definition, numbered within its section");
        assert((holds(result.text, "Theorem3.1(Energyinequality).")) && "a theorem");
        assert((holds(result.text, "Corollary3.2.If")) && "a corollary, sharing the theorem's counter");
        assert((holds(result.text, "Lemma3.3.")) && "a lemma");
        assert((holds(result.text, "problem[3,6].")) && "a citation of two sources");
        assert((holds(result.text, "U=1ms\xE2\x88\x92" "1")) && "a velocity in siunitx's units");
        assert((holds(result.text, "UL/\xCE\xBD=100000")) &&
               "the Reynolds number, grouped, and no space at a solidus");
        assert((holds(result.text, "3.21\xC3\x97" "10\xE2\x88\x92" "3")) &&
               "a table's number in scientific notation");
        assert((!holds(result.text, "ReferencesReferences") && holds(result.text, "References[1]J.T.Beale")) &&
               "the bibliography under one heading");
        assert((holds(result.text, "Figure1:Observed")) && "the figure's caption");
        const auto algorithm = std::ranges::find_if(result.pages, [](const std::string& page) {
            return holds(page, "Algorithm1Incremental");
        });
        assert((algorithm != result.pages.end() && holds(*algorithm, "endfor")) &&
               "the algorithm is not split across pages");

        // The appendix of typesetting checks.
        assert((holds(result.text, "BTypesettingchecks")) && "the second appendix, lettered");
        assert((holds(result.text, "Courant\xE2\x80\x93" "Friedrichs\xE2\x80\x93"
                                   "Lewy(CFL)condition,andafterthatsimplybytheCFL")) &&
               "an acronym spelled out once, short after");
        assert((holds(result.text, "Listing1isthestepofalgorithm1")) &&
               "cleveref's names for a listing and an algorithm");
        assert((holds(result.text, "fig.2aandfig.2b")) && "and for subfigures");
        assert((holds(result.text, "321,alengthmultipliedgives6.0pt,atokenlistgiveskepttokens")) &&
               "TeX's loops, registers and token lists");
        assert((holds(result.text, "give9and3.5pt")) && "e-TeX's expressions");

        // The appendix of the kernel's own commands.
        assert((holds(result.text, "CKernelchecks")) && "the third appendix, lettered");
        assert((holds(result.text, "Adaengines1843") && !holds(result.text, "NameRoleSince")) &&
               "tabbing's stops, the line \\kill ends measured and not drawn");
        assert((holds(result.text, "itsvalue8,and8asprinted")) && "a counter's value, printed");
        assert((holds(result.text, "keptandkept.")) && "a saved box, used twice");
        assert((holds(result.text, "table3atable,fig.3apicture,item2anitem")) &&
               "every kind of reference, each resolved");
    }

    return 0;
}
