#pragma once

#include "syntax/mouth.hpp"
#include "syntax/primitives/context.hpp"
#include "syntax/traceback.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace syntax::primitives {

    /// @brief Tables of data in a document: csvsimple's CSV files and
    ///        datatool's databases, under their packages' own names.
    ///
    /// A document made from a program's data -- a price list, a mail merge,
    /// a table of results -- reads its rows from a CSV file it was handed,
    /// or builds a small database itself, and sets a line of text, a table's
    /// row or a whole table from each. Both packages are this one module: a
    /// CSV file loaded into a database is the same table a document built
    /// row by row.
    ///
    /// @par Language
    /// @code
    /// \csvautotabular{prices.csv}                    % the file as a ruled table
    /// \csvautobooktabular{prices.csv}                % the same in booktabs' rules
    /// \csvreader[head to column names]{prices.csv}{}{\Item: \Price. }
    /// \csvreader[tabular=lr, table head=Item & Price\\]{prices.csv}{Item=\item, Price=\price}{\item & \price}
    ///
    /// \DTLnewdb{scores}
    /// \DTLnewrow{scores}\DTLnewdbentry{scores}{Name}{Ada}\DTLnewdbentry{scores}{Score}{90}
    /// \DTLloaddb{prices}{prices.csv}                 % a CSV file as a database
    /// \DTLforeach{scores}{\name=Name,\score=Score}{\name: \score. }
    /// \DTLdisplaydb{scores}                          % every row, as a table
    /// \DTLrowcount{scores} \DTLcolumncount{scores}
    /// \DTLgetvalue{\first}{scores}{1}{2}             % \first is 90
    /// \DTLfetch{scores}{Name}{Ada}{Score}            % 90
    /// \DTLsort{Score=descending}{scores}
    /// \DTLsumcolumn{scores}{Score}{\total}
    /// @endcode
    ///
    /// A CSV file's first line names its columns. Fields are parted by
    /// commas -- or semicolons or tabs, as `separator=` or `\\DTLsetseparator`
    /// says -- and a field in double quotes may hold the separator and a
    /// doubled quote. Every value is text until it is read.
    class Records {
    public:
        /// @brief Interns the control sequences this module binds.
        /// @param lexicon Interning table, shared with the expander.
        explicit Records(Lexicon& lexicon) noexcept;

        /// @brief Installs csvsimple's `\\csvautotabular`,
        ///        `\\csvautobooktabular` and `\\csvreader`, and datatool's
        ///        `\\DTLnewdb`, `\\DTLnewrow`, `\\DTLnewdbentry`, `\\DTLloaddb`,
        ///        `\\DTLforeach`, `\\DTLdisplaydb`, `\\DTLrowcount`,
        ///        `\\DTLcolumncount`, `\\DTLgetvalue`, `\\DTLfetch`, `\\DTLsort`,
        ///        `\\DTLsumcolumn`, `\\DTLmeanforcolumn`, `\\DTLifdbexists`,
        ///        `\\DTLcleardb`, `\\DTLsetseparator` and `\\DTLsettabseparator`.
        /// @param mouth   Expander to bind into.
        /// @param context Engine services: the files a table is read from.
        void operator()(Mouth& mouth, Context& context) const;

        /// @brief Every error this module has recorded: a file not found, a
        ///        database never made. Wrapper::traceback() gathers them.
        [[nodiscard]] const std::vector<Traceback>& traceback() const noexcept { return tracebacks; }

        /// @brief A CSV file's text as rows of fields.
        /// @param text      The file.
        /// @param separator The character between fields.
        /// @return Its lines, each its fields, quotes taken off; blank lines left out.
        /// @complexity O(n) in the text.
        [[nodiscard]] static std::vector<std::vector<std::string>> parse(std::string_view text, char separator);

    private:
        /// @brief One table: its columns' names and its rows, each row a
        ///        value for each name, empty where none was given.
        struct Table {
            std::vector<std::string> keys{};                 ///< The columns, in order.
            std::vector<std::vector<std::string>> rows{};    ///< The rows, each as wide as #keys.
        };

        mutable std::map<std::string, Table, std::less<>> tables{};   ///< datatool's databases, by name.
        mutable char separator{','};                                   ///< Between a CSV file's fields.
        mutable std::vector<Traceback> tracebacks{};                  ///< Errors this module found.
    };

}
