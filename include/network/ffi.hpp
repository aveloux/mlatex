#pragma once

/// @file
/// @brief The engine as a C library, for a program that makes documents.
///
/// The library exports one function, engine(), and it returns a struct of
/// every other: latex::Session's own operations, under latex::Session's own
/// names -- compose a session, set a value, define a command, provide a file,
/// typeset a document, dispose of the session. A C program calls them through
/// the struct the way a C++ one calls them on the class:
///
/// @code
/// static void balance(void* data, const char* const* arguments, size_t count, Output* output) {
///     const Ledger* ledger = data;
///     char text[64];
///     int length = snprintf(text, sizeof text, "%.2f", ledger_balance(ledger, arguments[0]));
///     engine()->write(output, text, (size_t)length);
/// }
///
/// const Latex* table = engine();
/// Session* session = table->compose(NULL);             // assets found beside the library
/// table->set(session, "customer", "Acme Ltd.");
/// table->define(session, "balance", 1, balance, &ledger);
/// table->provide(session, "letterhead/main.mtex", style, strlen(style));
/// table->provide(session, "chart.png", png, png_size);
///
/// const unsigned char* pdf;
/// size_t size;
/// if (!table->typeset(session, source, strlen(source), &pdf, &size)) {
///     fprintf(stderr, "%s", table->error(session));
/// }
/// if (pdf) send(pdf, size);                             // the session owns the bytes
/// table->dispose(session);
/// @endcode
///
/// with a document that asks for what it needs:
///
/// @code
/// \usepackage{letterhead}
/// Billed to \variable{customer}. Balance on account A-1042: \$\balance{A-1042}.
/// \includegraphics[width=\textwidth]{chart.png}
/// @endcode
///
/// What this adds over formatting text in the program and pasting it into a
/// template is the parts only the engine can do or only the program knows: a
/// command the document calls and the program answers, with arguments the
/// document chose; files -- a house style, a clause, a chart drawn a moment
/// ago -- found by name without ever being written to disk; and the PDF back
/// as bytes, to attach, store or stream.
///
/// @par Portability
/// The header is plain C as well as C++, so it can be included from either
/// and bound from any language with a C foreign-function interface: Python's
/// ctypes, C#'s P/Invoke, Rust's `extern "C"`, Go's cgo. Every string is
/// UTF-8 and zero-terminated, paths included, on every platform; every
/// function returns a plain int or pointer. It builds as a DLL on Windows and
/// as a shared library with default visibility on Linux and macOS, and
/// either way exports engine() and nothing else.
///
/// @par Threads
/// A session is used by one thread at a time, and a command is called back
/// on the thread that asked for the document, while it is being made.
/// Separate sessions share nothing but the engine's read-only tables and may
/// run side by side.

#include <stddef.h>

// How engine() is seen from outside the library: exported while the library
// itself is built -- its build defines EXPORTING -- imported by a program on
// Windows, visible by default elsewhere. Undefined again below, so a program
// that includes this keeps the name free.
#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(EXPORTING)
        #define VISIBLE __declspec(dllexport)
    #else
        #define VISIBLE __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define VISIBLE __attribute__((visibility("default")))
#else
    #define VISIBLE
#endif

#if defined(__cplusplus)
namespace network {
extern "C" {
#endif

/// @brief One latex::Session: where the engine's assets are, and the
///        values, commands and files every document it makes is handed.
///        Opaque; made by Latex::compose and ended by Latex::dispose.
typedef struct Session Session;

/// @brief Where a command writes what it stands for. Opaque; handed to a
///        Handler for the length of one call and written with Latex::write.
typedef struct Output Output;

/// @brief A command the calling program implements.
///
/// @param data      What was given to Latex::define, handed back unchanged.
/// @param arguments The arguments the document wrote, each expanded to its
///                  text, UTF-8 and zero-terminated; valid for this call.
/// @param count     How many there are: the arity it was defined with.
/// @param output    Where to write what the command stands for; writing
///                  nothing makes it stand for nothing.
typedef void (*Handler)(void* data, const char* const* arguments, size_t count, Output* output);

/// @brief Every operation the library offers: latex::Session's own, under
///        its own names.
typedef struct Latex {
    /// @brief Makes a session.
    /// @param assets The engine's assets directory, UTF-8; or NULL to find it
    ///               the way the command line does, walking up from wherever
    ///               this library itself was loaded from.
    /// @return The session, or NULL when no assets directory could be found.
    Session* (*compose)(const char* assets);

    /// @brief Ends a session. NULL is ignored.
    /// @param session The session; not to be used again.
    void (*dispose)(Session* session);

    /// @brief Sets a value the document reads with `\\variable{name}`,
    ///        replacing one set before under the same name.
    /// @return 1 when set; 0 when an argument was NULL or the name empty.
    int (*set)(Session* session, const char* name, const char* value);

    /// @brief Takes back a value set before.
    /// @return 1 when the session and name were given; 0 otherwise.
    int (*unset)(Session* session, const char* name);

    /// @brief Makes a control sequence the document can call, implemented by
    ///        the calling program.
    ///
    /// When the document reaches `\\name`, the engine reads @p arity brace
    /// groups after it, expands each, and calls @p handler with their text;
    /// what the handler writes to its output is read in the call's place.
    /// Defining a name again replaces it, and a name the engine already
    /// binds is replaced too, for this session's documents.
    ///
    /// @param session The session.
    /// @param name    The command's name, with or without its backslash, UTF-8.
    /// @param arity   How many arguments it reads, from 0 to 9.
    /// @param handler The function to call.
    /// @param data    Handed back to @p handler on every call; may be NULL.
    /// @return 1 when defined; 0 when an argument was NULL, the name empty or
    ///         the arity above 9.
    int (*define)(Session* session, const char* name, size_t arity, Handler handler, void* data);

    /// @brief Takes back a command defined before.
    /// @return 1 when the session and name were given; 0 otherwise.
    int (*forget)(Session* session, const char* name);

    /// @brief Writes part of what a command stands for; each call adds to
    ///        what came before.
    /// @param output Where to write, as the handler was handed it.
    /// @param text   The text, UTF-8: plain words or markup. Need not be
    ///               zero-terminated.
    /// @param length How many bytes of it there are.
    void (*write)(Output* output, const char* text, size_t length);

    /// @brief Hands the engine a file from memory, under the name a document
    ///        uses for it.
    ///
    /// `letterhead/main.mtex` is a package for `\\usepackage{letterhead}`,
    /// `terms.mtex` a file for `\\input{terms}`, and `chart.png` a picture
    /// for `\\includegraphics{chart.png}`. A file handed in is found before
    /// one on disk and before a built-in package of the same name. The bytes
    /// are copied, and may be freed as soon as this returns.
    ///
    /// @return 1 when kept; 0 when an argument was NULL or the name empty.
    int (*provide)(Session* session, const char* name, const void* bytes, size_t length);

    /// @brief Takes back a file handed in before.
    /// @return 1 when the session and name were given; 0 otherwise.
    int (*withdraw)(Session* session, const char* name);

    /// @brief Makes a PDF from a document held in memory, into memory.
    /// @param session  The session.
    /// @param document The document's text, UTF-8; need not be zero-terminated.
    /// @param length   How many bytes of it there are.
    /// @param pdf      Receives the PDF's bytes, owned by the session and
    ///                 valid until it next typesets or is disposed of; NULL
    ///                 when no PDF could be made at all.
    /// @param size     Receives how many bytes the PDF is; 0 when there is none.
    /// @return 1 when the document compiled with nothing left broken; 0
    ///         otherwise, with the reasons in Latex::error. A PDF may still
    ///         have been made when some part of the document was in error.
    int (*typeset)(Session* session, const char* document, size_t length, const unsigned char** pdf, size_t* size);

    /// @brief What went wrong the last time a document was typeset.
    /// @return Every error, one per line, UTF-8 and owned by the session;
    ///         empty when there were none. Valid until the session is next
    ///         used. Never NULL.
    const char* (*error)(const Session* session);
} Latex;

/// @brief The library's operations. The one function it exports.
///
/// Named for what it hands out rather than for the library: with C linkage
/// its name is global, and `latex` is the C++ engine's namespace.
///
/// @return The same struct on every call; never NULL.
VISIBLE const Latex* engine(void);

#if defined(__cplusplus)
}
}
#endif

#undef VISIBLE
