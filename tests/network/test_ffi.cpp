#include "network/ffi.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

// The engine the way a program embedding it sees it: through the one
// function the shared library exports and the struct it returns, and
// nothing else. A session is composed with no path, so the library has to
// find its own assets from where it was loaded.

/// @brief What the balance command saw, for the checks to read.
struct Ledger {
    int calls{0};             ///< How many times the document asked.
    std::string account{};    ///< The last account it asked about.
};

/// @brief A command implemented by the program: the balance of an account.
static void balance(void* data, const char* const* arguments, const size_t count, network::Output* output) {
    auto* ledger = static_cast<Ledger*>(data);
    ++ledger->calls;
    if (count == 1) ledger->account = arguments[0];

    // Written in two parts, as a program building its answer might.
    network::latex()->write(output, "\\textbf{", 8);
    network::latex()->write(output, "12.50}", 6);
}

/// @brief A document, typeset through the library.
struct Result {
    int clean{0};                         ///< What typeset returned.
    const unsigned char* pdf{nullptr};    ///< The PDF, if one was made.
    size_t size{0};                       ///< How long it is.
    std::string error{};                  ///< What went wrong.
};

/// @brief Typesets one document with a session.
static Result typeset(network::Session* session, const std::string_view document) {
    const network::Latex* engine = network::latex();
    Result result;
    result.clean = engine->typeset(session, document.data(), document.size(), &result.pdf, &result.size);
    result.error = engine->error(session);
    return result;
}

int main() {
    const network::Latex* engine = network::latex();
    assert((engine != nullptr) && "the library hands out its operations");
    assert((engine == network::latex()) && "the same struct every time");
    if (!engine) return 1;

    assert((engine->compose("no such folder") == nullptr) && "a session needs an assets folder");
    network::Session* session = engine->compose(nullptr);
    assert((session != nullptr) && "the library finds its own assets");
    if (!session) return 1;

    // Values.
    assert((engine->set(session, "account", "A-1042") == 1) && "a value is set");
    assert((engine->set(session, "customer", "Acme Ltd.") == 1) && "a second value is set");
    assert((engine->set(session, nullptr, "x") == 0) && "a value needs a name");
    assert((engine->set(session, "", "x") == 0) && "a value needs a name that is not empty");
    assert((engine->set(nullptr, "x", "x") == 0) && "a value needs a session");

    // Commands.
    Ledger ledger;
    assert((engine->define(session, "\\owing", 1, balance, &ledger) == 1) && "a command is defined");
    assert((engine->define(session, "many", 10, balance, &ledger) == 0) && "a command takes at most nine");
    assert((engine->define(session, "\\", 0, balance, &ledger) == 0) && "a command needs a name");
    assert((engine->define(session, "none", 0, nullptr, nullptr) == 0) && "a command needs a handler");

    // Files.
    const std::string_view style = "\\define\\company{Acme Ltd.}\n";
    assert((engine->provide(session, "letterhead/main.mtex", style.data(), style.size()) == 1) &&
           "a package is handed in");
    assert((engine->provide(session, "empty.mtex", nullptr, 0) == 1) && "an empty file is handed in");
    assert((engine->provide(session, "broken.mtex", nullptr, 4) == 0) && "bytes that are not there are refused");

    const std::string_view document =
        "\\usepackage{letterhead}\n\\begin{document}\n"
        "Billed to \\variable{customer}, \\company{}: \\owing{\\variable{account}}.\n"
        "\\input{empty}\n\\end{document}\n";

    const Result made = typeset(session, document);
    assert((made.clean == 1) && "the document is made with nothing reported");
    if (!made.clean) std::fprintf(stderr, "%s", made.error.c_str());
    assert((made.pdf != nullptr && made.size > 5 && std::memcmp(made.pdf, "%PDF-", 5) == 0) &&
           "the PDF comes back in memory");
    assert((ledger.calls == 1) && "the command is called once");
    assert((ledger.account == "A-1042") && "the command's argument arrives expanded");
    assert((made.error.empty()) && "no error is kept");

    // The same session again: everything handed in is still there.
    const Result again = typeset(session, document);
    assert((again.clean == 1 && ledger.calls == 2) && "a session keeps what it was handed");

    // Taken back, each is missed by name.
    assert((engine->withdraw(session, "letterhead/main.mtex") == 1) && "a package is taken back");
    assert((engine->forget(session, "\\owing") == 1) && "a command is taken back");
    assert((engine->unset(session, "customer") == 1) && "a value is taken back");
    const Result missing = typeset(session, document);
    assert((missing.clean == 0) && "a document missing its package and command is not clean");
    assert((missing.error.find("File `letterhead.sty' not found") != std::string::npos) &&
           "the missing package is named");
    assert((missing.error.find("\\owing") != std::string::npos) && "the missing command is named");
    assert((ledger.calls == 2) && "a command taken back is not called");

    // Nothing to typeset is refused without touching the session.
    Result nothing;
    nothing.clean = engine->typeset(session, nullptr, 0, &nothing.pdf, &nothing.size);
    assert((nothing.clean == 0 && nothing.pdf == nullptr && nothing.size == 0) && "no document, no PDF");
    assert((std::string_view(engine->error(nullptr)).empty()) && "no session, no error, and never NULL");

    engine->dispose(session);
    engine->dispose(nullptr);

    return 0;
}
