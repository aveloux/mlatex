#include "engine.hpp"
#include "memory/arena.hpp"
#include "syntax/cursor.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/modules.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/wrapper.hpp"
#include "syntax/semantics/union.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The modules compiled into the engine: the prelude and every package
// folder beside it, found by name. Every package a LaTeX preamble is
// likely to ask for has to load, and what it defines has to work -- the
// packages written in the expander's own primitives checked through the
// expander alone, the ones that set text through the whole engine.

/// What a document sets, runs of blanks folded to one, and everything it
/// reported: read by the expander and the parser with the syntax primitives
/// bound, and no page in sight.
static std::pair<std::string, std::string> expand(const std::string_view document) {
    memory::Arena arena(1u << 22);
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    syntax::primitives::Wrapper core(lexicon);
    syntax::primitives::Context context{state.registers(), core.conditionals(), core.variables()};
    core(mouth, context);
    mouth.ingest(arena.copy(document));
    syntax::Parser parser(mouth, arena);

    std::string text;
    const auto gather = [&text](this const auto& self, const memory::Slice<syntax::Node*> nodes) -> void {
        for (const syntax::Node* node : nodes) {
            if (node && node->type == syntax::Node::Type::Text) text += node->value;
            if (node && node->type == syntax::Node::Type::Group) self(node->nodes);
        }
    };
    gather(parser.parse(0));

    std::string folded;
    for (const char letter : text) {
        if (letter != ' ' && letter != '\n' && letter != '\t' && letter != '\r') {
            folded += letter;
        } else if (!folded.empty() && folded.back() != ' ') {
            folded += ' ';
        }
    }
    if (!folded.empty() && folded.back() == ' ') folded.pop_back();

    std::string errors;
    for (const auto& list : {parser.tracebacks(), mouth.tracebacks(), core.tracebacks()}) {
        for (const syntax::Traceback& fault : list) errors += fault.format() + '\n';
    }
    return {folded, errors};
}

/// A document must set exactly this text, and report nothing.
static void sets(const std::string_view document, const std::string_view expected, const char* what) {
    const auto [text, errors] = expand(document);
    if (text != expected || !errors.empty()) {
        std::fprintf(stderr, "%s\n  document: %.*s\n  expected: [%.*s]\n  got:      [%s]\n%s", what,
                     static_cast<int>(document.size()), document.data(), static_cast<int>(expected.size()),
                     expected.data(), text.c_str(), errors.c_str());
    }
    assert(text == expected && errors.empty());
}

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
static Result typeset(const std::string_view document, const engine::Host& host = {}) {
    static const std::filesystem::path assets = engine::locate(__FILE__);
    Result result;
    std::ostringstream errors;
    result.clean = engine::typeset(assets, document, result.pdf, host, errors, &result.pages);
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

int main() {
    // --- The table --------------------------------------------------------------
    assert((syntax::modules::find("main.mtex").has_value()) && "the prelude is compiled in");
    assert((syntax::modules::find("core/main.mtex").has_value()) && "and the core package");
    assert((!syntax::modules::find("nosuch/main.mtex").has_value()) && "a file that is not there is none");

    // --- Every package loads ------------------------------------------------------
    static constexpr std::string_view packages[] = {
        "amsmath", "amssymb", "amsthm", "amsfonts", "mathtools", "graphicx", "xcolor", "color", "hyperref", "url",
        "cleveref", "natbib", "biblatex", "booktabs", "multirow", "array", "tabularx", "longtable", "tabulary",
        "dcolumn", "colortbl", "hhline", "makecell", "threeparttable", "bigstrut", "diagbox", "caption",
        "subcaption", "subfig", "float", "wrapfig", "rotating", "lscape", "pdflscape", "sidecap", "placeins",
        "afterpage", "needspace", "enumitem", "paralist", "enumerate", "listings", "verbatim", "fancyvrb", "minted",
        "algorithm", "algorithmic", "algpseudocode", "algorithm2e", "geometry", "fullpage", "a4wide", "setspace",
        "parskip", "indentfirst", "titlesec", "titling", "fancyhdr", "lastpage", "footmisc", "xspace", "ragged2e",
        "microtype", "babel", "inputenc", "fontenc", "lmodern", "times", "textcomp", "gensymb", "eurosym",
        "siunitx", "units", "nicefrac", "xfrac", "cancel", "bm", "mathrsfs", "bbm", "dsfont", "upgreek", "stmaryrd",
        "esint", "mhchem", "physics", "braket", "tikz", "todonotes", "marginnote", "soul", "ulem", "csquotes",
        "lipsum", "blindtext", "datetime", "datetime2", "etoolbox", "ifthen", "xparse", "calc", "comment",
        "appendix", "authblk", "abstract", "epigraph", "framed", "mdframed", "tcolorbox", "fancybox", "glossaries",
        "pifont", "fontspec", "unicode-math", "pgfplots", "tikz-cd", "tocloft", "ctable", "xurl", "soulutf8",
        "subfigure", "mwe", "iftex", "stfloats", "balance", "sectsty", "newtxmath", "libertine", "nicematrix",
    };
    for (const std::string_view name : packages) {
        const Result result = typeset("\\documentclass{article}\\usepackage{" + std::string(name) +
                                      "}\\begin{document}x\\end{document}");
        if (!result.clean) std::fprintf(stderr, "  %.*s: %s", static_cast<int>(name.size()), name.data(),
                                        result.errors.c_str());
        assert((result.clean && !holds(result.errors, "not found")) &&
               "a package loads, known by name, with no error reported");
    }
    {
        const Result result = typeset("\\documentclass{article}\\usepackage{nosuch}\\begin{document}x\\end{document}");
        assert((result.clean && holds(result.errors, "warning: File `nosuch.sty' not found")) &&
               "an unknown package is a warning that names it, and the document is still made");
    }
    {
        const Result result = typeset("\\documentclass{article}\\usepackage{xurl}\\begin{document}"
                                      "\\ifpackageloaded{url}{url}{none} \\ifpackageloaded{xurl}{xurl}{none}"
                                      "\\end{document}");
        assert((result.clean && result.errors.empty() && holds(result.text, "urlxurl")) &&
               "a package under another's name loads that one, and both are loaded");
    }

    // --- The core ---------------------------------------------------------------
    sets("\\include{core/compatibility}\\usepackage{xcolor,lmodern}\\ifpackageloaded{xcolor}{yes}{no}", "yes",
         "the packages the engine does itself are provided by the core");
    sets("\\include{core/shorthand}\\eg the rest", "e.g.~the rest", "the core shorthands");

    // --- etoolbox and ifthen -------------------------------------------------------
    sets("\\usepackage{etoolbox}\\newtoggle{draft}\\iftoggle{draft}{on}{off} \\toggletrue{draft}"
         "\\iftoggle{draft}{on}{off} \\nottoggle{draft}{on}{off}",
         "off on off", "toggles");
    sets("\\usepackage{etoolbox}\\setvariable{toggle.final}{true}\\iftoggle{final}{final}{draft}", "final",
         "a toggle a program set before the document runs");
    sets("\\usepackage{etoolbox}\\ifnumcomp{12}{>}{10}{bulk}{single} \\ifnumequal{3}{3}{equal}{not}"
         " \\ifnumodd{7}{odd}{even}",
         "bulk equal odd", "comparing numbers");
    sets("\\usepackage{etoolbox}\\define\\a{x}\\ifdef{\\a}{defined}{undefined} \\ifundef{\\zz}{undefined}{defined}",
         "defined undefined", "whether a command is defined");
    sets("\\usepackage{etoolbox}\\ifblank{}{blank}{text} \\notblank{a}{text}{blank}", "blank text",
         "whether text is there at all");
    sets("\\usepackage{ifthen}\\newboolean{final}\\ifthenelse{\\boolean{final}}{signed}{draft} "
         "\\setboolean{final}{true}\\ifthenelse{\\boolean{final}}{signed}{draft}",
         "draft signed", "\\ifthenelse with a boolean");
    sets("\\usepackage{ifthen}\\setvariable{currency}{EUR}\\ifthenelse{\\equal{\\variable{currency}}{EUR}}{euro}{other}"
         " \\ifthenelse{\\isodd{4}}{odd}{even}",
         "euro even", "\\ifthenelse with \\equal and \\isodd");
    sets("\\usepackage{ifthen}\\ifpackageloaded{etoolbox}{yes}{no}", "yes", "ifthen brings etoolbox with it");

    // --- acronym, xfp, csquotes and lipsum ------------------------------------------
    // The kernel's loops, \@for over commas and \@tfor over tokens.
    {
        const Result result = article("", "\\@for\\x:=a,b\\do{<\\x>} \\@tfor\\x:=cd\\do{[\\x]}");
        assert((result.clean && holds(result.text, "<a><b>[c][d]")) && "the kernel's \\@for and \\@tfor");
    }

    // acronym's forms, set through the whole engine, which writes one used
    // before its list defines it in when it does.
    {
        const Result result = article("\\usepackage{acronym}",
                                      "\\newacronym{pdf}{PDF}{Portable Document Format}\\ac{pdf}, \\ac{pdf}");
        assert((result.clean && holds(result.text, "PortableDocumentFormat(PDF),PDF")) &&
               "an acronym spelled out once, short after");
    }
    {
        const Result result = article("\\usepackage{acronym}", "\\acrodef{API}{application programming interface}"
                                                               "\\acs{API} \\acl{API} \\acf{API}");
        assert((result.clean && holds(result.text, "APIapplicationprogramminginterfaceapplicationprogramminginterface(API)")) &&
               "every form");
    }
    {
        const Result result = article("\\usepackage{acronym}", "\\newacronym{os}{OS}{operating system}\\acp{os}, \\acp{os}");
        assert((result.clean && holds(result.text, "operatingsystems(OSs),OSs")) && "plurals");
    }
    {
        const Result result = article("\\usepackage{acronym}", "\\newacronym{pdf}{PDF}{Portable Document Format}"
                                                               "\\ac{pdf} \\acreset{pdf}\\ac{pdf}");
        assert((result.clean && holds(result.text, "PortableDocumentFormat(PDF)PortableDocumentFormat(PDF)")) &&
               "\\acreset spells it out again");
    }
    {
        const Result result = article("\\usepackage{acronym}", "\\ac{GUI} and \\ac{GUI}.\n\n"
                                                               "\\begin{acronym}\\acro{GUI}{graphical user interface}"
                                                               "\\end{acronym}");
        assert((result.clean && holds(result.text, "graphicaluserinterface(GUI)andGUI.") &&
                holds(result.text, "GUIgraphicaluserinterface")) &&
               "an acronym used before its list defines it, and the list");
    }
    {
        const Result result = article("\\usepackage{acronym}", "\\ac{nosuch}");
        assert(holds(result.text, "??") && "an acronym never defined reads ??");
    }
    sets("\\usepackage{xfp}\\fpeval{1.5*4}", "6", "xfp's \\fpeval");
    sets("\\usepackage{csquotes}\\enquote{hi}", "\xE2\x80\x9Chi\xE2\x80\x9D", "csquotes' \\enquote");
    {
        const auto [one, quiet] = expand("\\usepackage{lipsum}\\lipsum[1]");
        const auto [all, calm] = expand("\\usepackage{lipsum}\\lipsum end");
        assert((quiet.empty() && one.starts_with("Lorem ipsum dolor sit amet")) &&
               "\\lipsum[1] is the first paragraph");
        assert((calm.empty() && all.size() > 4 * one.size()) && "\\lipsum is all five");
        assert((expand("\\usepackage{lipsum}\\lipsum[5]").first.starts_with("Nam libero")) &&
               "\\lipsum[5] is the fifth");
    }

    // --- siunitx ----------------------------------------------------------------
    sets("\\usepackage{siunitx}\\variable{siunitx.per-mode}", "power", "units after \\per raised by default");
    sets("\\usepackage[per-mode=symbol]{siunitx}\\variable{siunitx.per-mode}", "symbol",
         "a package option chooses a solidus");
    sets("\\usepackage{siunitx}\\sisetup{per-mode=symbol}\\variable{siunitx.per-mode}", "symbol",
         "\\sisetup chooses it later");
    {
        const Result result = article("\\usepackage{siunitx}",
                                      "\\num{100000} \\num{3.21e-3} \\qty{9.81}{\\metre\\per\\second\\squared} "
                                      "\\numlist{1;2;3} \\qty{5}{\\percent} \\SI{1.0e-6}{\\square\\metre\\per\\second} "
                                      "\\qtyrange{1}{5}{\\kilo\\metre} \\ang{45}");
        assert((result.clean) && "siunitx typesets without a report");
        assert((holds(result.text, "100000")) &&
               "a number's digits, grouped by thin spaces the text layer leaves out");
        assert((holds(result.text, "3.21\xC3\x97" "10\xE2\x88\x92" "3")) &&
               "an exponent written out as a power of ten");
        assert((holds(result.text, "9.81ms\xE2\x88\x92" "2")) && "a unit after \\per raised to a negative power");
        assert((holds(result.text, "1,2and3")) && "a list with and before its last number");
        assert((holds(result.text, "5%")) && "a percentage");
        assert((holds(result.text, "1kmto5km")) && "a range of quantities, the unit on both");
        assert((holds(result.text, "45\xC2\xB0")) && "an angle");
        const Result symbol = article("\\usepackage[per-mode=symbol]{siunitx}", "\\qty{3}{\\metre\\per\\second}");
        assert((symbol.clean && holds(symbol.text, "3m/s")) && "per-mode=symbol sets a solidus");
    }

    // --- datetime ---------------------------------------------------------------
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        static constexpr const char* months[] = {"January", "February", "March", "April", "May", "June", "July",
                                                 "August", "September", "October", "November", "December"};
        const int year = local.tm_year + 1900;
        char iso[16];
        std::snprintf(iso, sizeof iso, "%04d-%02d-%02d", year, local.tm_mon + 1, local.tm_mday);
        const std::string today = std::string(months[local.tm_mon]) + " " + std::to_string(local.tm_mday) + ", " +
                                  std::to_string(year);
        sets("\\include{core/dates}\\today", today, "\\today is LaTeX's own");
        sets("\\include{core/dates}\\usepackage{datetime}\\isodate", iso, "\\isodate");
        sets("\\include{core/dates}\\monthname{2} \\usepackage{datetime}\\shortmonthname{12}", "February Dec",
             "month names");
    }

    // --- Packages that set text -------------------------------------------------------
    {
        const Result result = article(
            "\\usepackage{soul,ulem,gensymb,eurosym,units,nicefrac,xfrac,bbm,upgreek,stmaryrd,glossaries,fancybox}"
            "\\newacronym{gpu}{GPU}{graphics processing unit}",
            "A:\\hl{marked} \\ul{under} \\st{struck} \\caps{Caps}. B:\\uline{one} \\sout{gone}. "
            "C:30\\celsius{} \\ohm{} \\EUR{12} \\unit[5]{m}. D:$\\mathbbm{R}$ $\\upalpha$ $\\llbracket x\\rrbracket$. "
            "E:\\gls{gpu} then \\gls{gpu}, \\acrshort{gpu}. F:\\shadowbox{shadow}. G:\\nicefrac{1}{2} \\sfrac{3}{4}.");
        assert((result.clean) && "the text packages set without a report");
        assert((holds(result.text, "A:markedunderstruck")) && "soul keeps the words it marks");
        assert((holds(result.text, "B:onegone.")) && "and ulem");
        assert((holds(result.text, "C:30\xC2\xB0" "C\xE2\x84\xA6") || holds(result.text, "C:30\xC2\xB0" "C\xCE\xA9")) &&
               "gensymb's units");
        assert((holds(result.text, "12\xE2\x82\xAC")) && "eurosym's \\EUR");
        assert((holds(result.text, "E:graphicsprocessingunit(GPU)thenGPU,GPU.")) &&
               "glossaries' acronym spelled out once, short after");
        assert((holds(result.text, "F:shadow.")) && "fancybox's frame keeps its text");
    }
    {
        const Result result = article("\\usepackage{algorithm2e}",
                                      "\\begin{algorithm}\\KwIn{a list}\\ForEach{$x$}{add it}\\Return{$s$}"
                                      "\\caption{Sum}\\end{algorithm}");
        assert((result.clean && holds(result.text, "Input:alist") && holds(result.text, "foreach") &&
               holds(result.text, "Algorithm1Sum")) && "algorithm2e's steps, set as a captioned float");
    }
    {
        const Result result = article("\\usepackage{lastpage}",
                                      "One.\\newpage Page \\thepage{} of \\pageref{LastPage}.");
        assert((result.clean && holds(result.text, "Page2of2.")) && "lastpage labels the last page");
    }
    {
        const Result result = article("\\usepackage{epigraph,authblk}",
                                      "\\epigraph{The purpose of computing is insight.}{Hamming}");
        assert((result.clean && holds(result.text, "Thepurposeofcomputingisinsight.") &&
               holds(result.text, "Hamming")) &&
               "an epigraph and its source");
    }

    // --- The kernel's ways of defining, in the core package ------------------------
    {
        const Result result = article(
            "\\newif\\ifdraft"
            "\\newcommand{\\given}{original}\\providecommand{\\given}{replaced}\\providecommand{\\fresh}{fresh}"
            "\\def\\half{one}\\edef\\frozen{\\half}\\def\\half{two}"
            "\\NewDocumentCommand{\\pair}{s o m}{\\IfBooleanTF{#1}{star}{plain}-\\IfNoValueTF{#2}{none}{#2}-#3}"
            "\\NewDocumentCommand{\\opt}{O{dflt} m}{[#1|#2]}"
            "\\let\\strong\\textbf \\let\\equal=\\half"
            "\\expandafter\\def\\csname built\\endcsname{made}"
            "\\newcommand{\\set}{mine}",
            "A:\\ifdraft on\\else off\\fi. \\drafttrue B:\\ifdraft on\\else off\\fi. "
            "C:\\given. D:\\fresh. E:\\frozen. F:\\pair{x}. G:\\pair*[y]{z}. H:\\opt{q}. I:\\opt[r]{s}. "
            "J:\\strong{bold}. K:\\built. L:\\set. M:\\equal. "
            "N:\\@ifundefined{built}{no}{yes}. O:\\@ifundefined{nothing}{no}{yes}. "
            "P:\\ifdefined\\strong def\\fi. Q:\\ifcsname built\\endcsname cs\\fi. R:\\the\\textwidth.");
        assert((result.clean) && "the kernel's definitions read without a report");
        assert((holds(result.text, "A:off.B:on.")) && "\\newif's switch starts false and \\drafttrue turns it");
        assert((holds(result.text, "C:original.D:fresh.")) &&
               "\\providecommand keeps a definition and makes a missing one");
        assert((holds(result.text, "E:one.")) && "\\edef keeps what its body expanded to when defined");
        assert((holds(result.text, "F:plain-none-x.G:star-y-z.")) && "\\NewDocumentCommand's s, o and m");
        assert((holds(result.text, "H:[dflt|q].I:[r|s].")) && "an O{default} argument");
        assert((holds(result.text, "J:bold.K:made.L:mine.M:two.")) &&
               "\\let of a parser's command, \\csname after \\expandafter, a primitive's name redefined, \\let=");
        assert((holds(result.text, "N:yes.O:no.P:def.Q:cs.")) && "\\@ifundefined, \\ifdefined and \\ifcsname");
        assert((holds(result.text, "R:345.0pt.")) && "\\the prints a length as TeX does");
    }
    {
        const Result result = article(
            "\\NeedsTeXFormat{LaTeX2e}[1995/12/01]\\ProvidesPackage{mine}[2024/01/01 v1.0]"
            "\\RequirePackage{amsmath}\\DeclareOption{draft}{}\\ProcessOptions\\relax"
            "\\makeatletter\\newcommand{\\@inner}{inner}\\makeatother"
            "\\DeclareRobustCommand{\\name}{Name}\\typeout{Loading}\\PackageWarning{mine}{careful}"
            "\\IfFileExists{nothing.tex}{found}{}",
            "\\name, \\@inner, \\textsuperscript{th} and a\\,b\\;c\\quad\\null d.");
        assert((result.clean && holds(result.text, "Name,inner,thandabcd.")) &&
               "a package's opening words read and let go, and what the preamble defined is there");
    }
    {
        const Result result = article("\\title{The Title}\\author{A. Author \\and B. Author}\\date{Today}",
                                      "\\maketitle Body.");
        assert((result.clean && holds(result.text, "TheTitleA.AuthorB.AuthorTodayBody.")) &&
               "a title, its authors and date");
    }
    {
        const Result result = article("\\usepackage{todonotes}", "\\todo{check}\\missingfigure{A plot.}");
        assert((result.clean && holds(result.text, "Todo:check") && holds(result.text, "Missing")) &&
               "todonotes' notes and missing figures");
        const Result quiet = article("\\usepackage[disable]{todonotes}", "Kept.\\todo{gone}\\missingfigure{gone}");
        assert((quiet.clean && quiet.text == "Kept.1") && "[disable] takes every note out");
    }

    return 0;
}
