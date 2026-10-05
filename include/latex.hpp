#pragma once

#include "syntax/primitives/context.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @brief The whole pipeline, callable by more than `main()`.
///
/// `main()` wants fonts indexed, a prelude read, a document parsed and
/// composed, a PDF written; this is that one path, with the CLI's own
/// argument parsing and self-location left to `main.cpp`. The foreign-function
/// library in `network/ffi.hpp` calls the same path, which is why it can make
/// the same PDF from the same document without running a second program.
namespace latex {

    /// @brief Values handed to a document before it runs, name then text, as
    ///        `\\variable{name}` reads them.
    using Variables = std::vector<std::pair<std::string, std::string>>;

    /// @brief A control sequence the calling program implements itself.
    ///
    /// When the document reaches it, its arguments are read -- expanded, so
    /// `\\balance{\\variable{account}}` hands over the account's number and
    /// not the macro -- the handler is called with their text, and whatever
    /// it returns is read in the command's place, exactly as if the document
    /// had said it. The returned text may itself be markup.
    ///
    /// This is what a template cannot do on its own: ask the program for
    /// something only the program knows, at the point the document needs it,
    /// with arguments the document chose -- a customer's history looked up
    /// by the account number the page is about, a figure computed from a
    /// database the engine never sees.
    struct Command {
        std::string name{};     ///< Without the backslash, as `balance`.
        std::size_t arity{0};   ///< How many brace groups it reads; at most nine.

        /// Called with the arguments' text; returns what the command stands for.
        std::function<std::string(std::span<const std::string>)> handler{};
    };

    /// @brief Everything a calling program hands a document besides its text.
    struct Host {
        Variables variables{};                    ///< Read with `\\variable`.
        std::vector<Command> commands{};          ///< Implemented by the program.
        syntax::primitives::Files files{};        ///< Packages, inputs and pictures, from memory.

        /// Folders read after the document's own for what it inputs --
        /// `\\input`, `\\include`, `\\usepackage`, `\\includegraphics`, a
        /// bibliography -- in order: TeX's `-include-directory`.
        std::vector<std::filesystem::path> directories{};
    };

    /// @brief The engine as a program keeps it: where its assets are, what it
    ///        hands every document, and what the last document made.
    ///
    /// This is the one interface every binding shares. The C library in
    /// `network/ffi.hpp` is these same functions in a struct, under the same
    /// names; the WebAssembly module in `render/wasm.hpp` is this class, bound
    /// for JavaScript under the same names again. Whatever a document can be
    /// handed from one language, it can be handed from all three.
    ///
    /// @code
    /// latex::Session session(latex::locate(program));
    /// session.set("customer", "Acme Ltd.");
    /// session.define({.name = "balance", .arity = 1, .handler = [&](auto arguments) {
    ///     return ledger.balance(arguments[0]);
    /// }});
    /// session.provide("chart.png", png);
    ///
    /// if (!session.typeset(document)) std::cerr << session.error;
    /// send(session.pdf);
    /// @endcode
    ///
    /// Values, commands and files persist from one document to the next until
    /// they are replaced or taken back, so a program that makes a thousand
    /// invoices sets its letterhead once. Each pair of verbs is the language's
    /// own where it has one -- \\define and \\forget.
    class Session {
    public:
        /// @brief A session over an assets directory.
        /// @param assets The engine's assets directory: fonts, and the
        ///               hyphenation patterns not compiled in.
        explicit Session(std::filesystem::path assets);

        /// @brief Sets a value the document reads with `\\variable{name}`,
        ///        replacing one set before under the same name.
        void set(std::string_view name, std::string_view value);

        /// @brief Takes back a value set before; nothing when there was none.
        void unset(std::string_view name);

        /// @brief Makes a command the program implements, replacing one
        ///        defined before under the same name.
        /// @param command The command; its name may be written with or
        ///                without its backslash.
        void define(Command command);

        /// @brief Takes back a command defined before; nothing when there was none.
        /// @param name Its name, with or without its backslash.
        void forget(std::string_view name);

        /// @brief Hands the engine a file from memory under the name a
        ///        document uses for it, replacing one handed in before.
        /// @param name  As `letterhead/main.mtex`, `terms.mtex` or `chart.png`.
        /// @param bytes The file's contents, text or a picture's bytes.
        void provide(std::string_view name, std::string bytes);

        /// @brief Takes back a file handed in before; nothing when there was none.
        void withdraw(std::string_view name);

        /// @brief Makes a PDF from a document held in memory.
        /// @param document The document's text.
        /// @return True when the document compiled with nothing left broken.
        ///         A PDF may still have been made when some part of it was not;
        ///         #pdf is empty when none could be made at all.
        bool typeset(std::string_view document);

        std::filesystem::path assets{};    ///< The assets directory the session reads.
        Host host{};                       ///< Everything the next document will be handed.
        std::string pdf{};                 ///< The last PDF made, or nothing when none could be.
        std::string error{};               ///< What went wrong the last time, one error per line; empty when nothing did.

        /// Each page of the last PDF made, as its text: what a reader copying
        /// it would get, a line of the page to a line.
        std::vector<std::string> pages{};
    };

    /// @brief Finds the engine's assets directory from where a binary lives.
    ///
    /// Walked up from the binary rather than assumed to be one level above
    /// it, because where the build directory sits is the builder's choice and
    /// not the engine's. A build beside the source, inside it, or several
    /// directories away all find the same tree. The fonts are what identify
    /// it: a build directory may well have an `assets` folder of its own for
    /// whatever it generates, and taking the first one found would stop at
    /// that.
    ///
    /// @param binary Path to the running executable or library.
    /// @return The assets directory, or an empty path when none was found.
    /// @complexity O(d) in the depth of the binary's path.
    [[nodiscard]] std::filesystem::path locate(const std::filesystem::path& binary);

    /// @brief Composes one document from disk into a PDF on disk, start to
    ///        finish, with whatever the command line hands it.
    ///
    /// The files the document inputs are found beside it.
    ///
    /// @param assets      The engine's own assets directory (fonts, hyphenation patterns).
    /// @param source      The document to read.
    /// @param destination Where to write the PDF; empty to read and set the
    ///                    document and write none, as TeX's draft mode does.
    /// @param host        Values, commands and files handed to the document.
    /// @param report      Where what it did and how long each step took is
    ///                    written, or null for nowhere.
    /// @param errors      Where every error is written, one per line.
    /// @param texts       Receives each page's text, or null for none; empty
    ///                    for a draft, which draws no page.
    /// @return True when the document compiled with nothing left broken.
    [[nodiscard]] bool compose(
        const std::filesystem::path& assets,
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        const Host& host,
        std::ostream* report,
        std::ostream& errors,
        std::vector<std::string>* texts = nullptr
    );

    /// @brief Composes a document held in memory into a PDF held in memory,
    ///        quietly.
    ///
    /// What a program embedding the engine calls: nothing is printed and
    /// nothing is written to disk. Every error goes to the stream it is given
    /// rather than to the console, where a host program would have no way to
    /// see it, and the PDF is handed back to be sent or stored however the
    /// program likes.
    ///
    /// @param assets   The engine's own assets directory.
    /// @param document The document's text.
    /// @param pdf      Receives the PDF; left empty when none could be made.
    /// @param host     Values, commands and files handed to the document.
    /// @param errors   Where every error is written, one per line.
    /// @param texts    Receives each page's text, as a reader copying it would
    ///                 get it, or null for none: for a program to search, index
    ///                 or check what it made without reading the PDF back.
    /// @return True when the document compiled with nothing left broken. A
    ///         PDF may still have been made when some part of it was not.
    [[nodiscard]] bool typeset(
        const std::filesystem::path& assets,
        std::string_view document,
        std::string& pdf,
        const Host& host,
        std::ostream& errors,
        std::vector<std::string>* texts = nullptr
    );

}
