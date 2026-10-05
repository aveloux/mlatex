#include "memory/arena.hpp"
#include "syntax/cursor.hpp"
#include "syntax/lexicon.hpp"
#include "syntax/mouth.hpp"
#include "syntax/node.hpp"
#include "syntax/parser.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/primitives/records.hpp"
#include "syntax/primitives/wrapper.hpp"
#include "syntax/semantics/union.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

// The Records primitive: a CSV file handed in, read as csvsimple and
// datatool read one, and a database built in the document itself.

/// The file every document here may read.
static const char* const prices = "Item,Price\nTea,2\n\"Cake, lemon\",3.5\nScone,1\n";

/// What a document sets, runs of blanks folded to one, and everything it
/// reported: read by the expander and the parser with the syntax primitives
/// bound, prices.csv handed in, and no page in sight.
static std::pair<std::string, std::string> expand(const std::string_view document) {
    memory::Arena arena(1u << 22);
    syntax::semantics::Union state{};
    syntax::Lexicon lexicon(arena);
    syntax::Mouth mouth(syntax::Cursor{}, state, lexicon, arena);
    syntax::primitives::Wrapper core(lexicon);
    syntax::primitives::Files files;
    files.emplace("prices.csv", prices);
    files.emplace("semicolons.csv", "a;b\n1;2\n");
    syntax::primitives::Context context{state.registers, core.relay, core.variables};
    context.files = &files;
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
    for (const auto& list : {parser.traceback(), mouth.traceback(), core.traceback()}) {
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

/// A document must report an error naming this.
static void reports(const std::string_view document, const std::string_view needle, const char* what) {
    const auto [text, errors] = expand(document);
    if (errors.find(needle) == std::string::npos) {
        std::fprintf(stderr, "%s\n  document: %.*s\n  expected an error naming: %.*s\n  got: %s\n", what,
                     static_cast<int>(document.size()), document.data(), static_cast<int>(needle.size()),
                     needle.data(), errors.c_str());
    }
    assert(errors.find(needle) != std::string::npos);
}

int main() {
    // --- A CSV file's text ----------------------------------------------------------------
    const auto rows = syntax::primitives::Records::parse(prices, ',');
    assert((rows.size() == 4 && rows[0][0] == "Item" && rows[2][0] == "Cake, lemon" && rows[2][1] == "3.5") &&
           "a quoted field keeps its comma");
    const auto doubled = syntax::primitives::Records::parse("\"say \"\"hi\"\"\",x\r\n\r\n", ',');
    assert((doubled.size() == 1 && doubled[0][0] == "say \"hi\"" && doubled[0][1] == "x") &&
           "a doubled quote is one, a line end of two characters one, and a blank line none");

    // --- csvsimple ------------------------------------------------------------------------
    sets("\\csvreader[head to column names]{prices.csv}{}{\\Item: \\Price. }", "Tea: 2. Cake, lemon: 3.5. Scone: 1.",
         "a row at a time, each column a macro of its name");
    sets("\\csvreader{prices.csv}{Item=\\thing, 2=\\price}{(\\thecsvrow) \\thing=\\price; }",
         "(1) Tea=2; (2) Cake, lemon=3.5; (3) Scone=1;", "columns by name and by number, and the row's own number");
    sets("\\csvreader[separator=semicolon]{semicolons.csv}{}{\\csvcoli+\\csvcolii}", "1+2", "another separator");
    reports("\\csvreader{nowhere.csv}{}{x}", "No data file named 'nowhere.csv'", "a file that is not there");

    // --- datatool -------------------------------------------------------------------------
    sets("\\DTLnewdb{s}\\DTLnewrow{s}\\DTLnewdbentry{s}{Name}{Ada}\\DTLnewdbentry{s}{Score}{90}"
         "\\DTLnewrow{s}\\DTLnewdbentry{s}{Name}{Alan}\\DTLnewdbentry{s}{Score}{85}"
         "\\DTLforeach{s}{\\name=Name,\\score=Score}{\\name: \\score\\DTLiflastrow{.}{, }}",
         "Ada: 90, Alan: 85.", "a database built row by row, and each row read");
    sets("\\DTLloaddb{p}{prices.csv}\\DTLrowcount{p} rows of \\DTLcolumncount{p}", "3 rows of 2", "a CSV file loaded");
    sets("\\DTLloaddb{p}{prices.csv}\\DTLfetch{p}{Item}{Scone}{Price} \\DTLgetvalue{\\v}{p}{1}{1}\\v", "1 Tea",
         "a value found by another, and one by its place");
    sets("\\DTLloaddb{p}{prices.csv}\\DTLsort{Price=descending}{p}\\DTLforeach{p}{\\i=Item}{\\i;}",
         "Cake, lemon;Tea;Scone;", "rows ordered by a column of numbers");
    sets("\\DTLloaddb{p}{prices.csv}\\DTLsumcolumn{p}{Price}{\\t}\\DTLmeanforcolumn{p}{Price}{\\m}(\\t) (\\m)", "(6.5) (2.1666666666666665)",
         "a column's total and its mean");
    sets("\\DTLnewdb{e}\\DTLifdbexists{e}{yes}{no} \\DTLifdbexists{f}{yes}{no}", "yes no", "whether a database exists");
    reports("\\DTLrowcount{none}", "No database named 'none'", "a database never made");
    return 0;
}
