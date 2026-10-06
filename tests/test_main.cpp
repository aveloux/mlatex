#include "latex.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// The command line: the executable run as a person runs it -- a document in,
// its PDF beside it out, TeX's options with one dash or two, what it is made
// into, where it reads from and where it writes to -- and a mistake in either
// the arguments or the document ending it with a failing status. BINARY is
// the built executable's path, which the build hands this test.

/// Runs the executable on a document with some options; its exit status.
static int run(const std::filesystem::path& document, const std::string_view options) {
    std::string command = "\"" BINARY "\" \"" + document.string() + "\" " + std::string(options);
#if defined(_WIN32)
    // cmd.exe drops the first and last quote of a command that starts with one.
    command = "\"" + command + " > NUL 2>&1\"";
#else
    command += " > /dev/null 2>&1";
#endif
    return std::system(command.c_str());
}

/// Writes a file where the executable can read it.
static std::filesystem::path write(const std::filesystem::path& path, const std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

int main() {
    const std::filesystem::path temporary = std::filesystem::temp_directory_path();
    const std::filesystem::path document =
        write(temporary / "main-plain.mtex", "\\documentclass{article}\\begin{document}Hello.\\end{document}");
    std::filesystem::path pdf = document;
    pdf.replace_extension(".pdf");
    std::filesystem::remove(pdf);
    assert((run(document, "") == 0) && "a document sets, and the run succeeds");
    assert((std::filesystem::file_size(pdf) > 0) && "its PDF beside it, named for it");

    assert((run(temporary / "no-such-document.mtex", "") == 1) && "a document that is not there fails the run");

    const std::filesystem::path broken =
        write(temporary / "main-broken.mtex", "\\documentclass{article}\\begin{document}\\nosuchcommand\\end{document}");
    assert((run(broken, "") == 1) && "a document with a mistake fails the run");

    // TeX's options, with one dash or two, and the engine's own.
    assert((run(document, "--version") == 0 && run(document, "-v") == 0) && "--version");
    assert((run(document, "--help") == 0 && run(document, "-h") == 0) && "--help");
    assert((run(document, "--no-such-option") == 2) && "an option nothing knows is the command line's mistake");
    assert((run(document, "--set=customer=Acme") == 2) && "values are the bindings', not the command line's");
    assert((run(document, "-interaction=batchmode") == 0 && run(document, "--interaction nonstopmode") == 0) &&
           "-interaction, written as TeX writes it or with its value after it");
    assert((run(document, "--interaction=batch-mode") == 0 && run(document, "-i error-stop-mode") == 0) &&
           "and in words, with dashes between");
    assert((run(document, "--interaction=sometimes") == 2) && "an interaction TeX has no name for");
    assert((run(document, "--time-statistics -q") == 0) && "--time-statistics");
    assert((run(document, "--time -q") == 0 && run(document, "-T") == 0) && "--time");
    assert((run(document, "--log-level warn") == 2) && "the logger's options take their value after =");
    assert((run(document, "--log-level=warn -q") == 0) && "as they are written");

    // What the document is made into: a PDF ahead of time, or a target not
    // built yet, which is taken and does nothing.
    std::filesystem::remove(pdf);
    assert((run(document, "--target=aot -q") == 0 && std::filesystem::exists(pdf)) && "--target=aot makes the PDF");
    std::filesystem::remove(pdf);
    assert((run(document, "-t jit") == 0 && !std::filesystem::exists(pdf)) && "--target=jit is taken, and does nothing yet");
    assert((run(document, "--target=wasm") == 0 && !std::filesystem::exists(pdf)) &&
           "--target=wasm is taken, and does nothing yet");
    assert((run(document, "--target=fortran") == 2) && "a target there is none of");

    assert((run(document, "--draftmode -q") == 0 && !std::filesystem::exists(pdf)) && "--draftmode writes no PDF");
    assert((run(document, "--draft-mode -q") == 0 && run(document, "-n") == 0 && !std::filesystem::exists(pdf)) &&
           "nor does --draft-mode");

    std::filesystem::path stray = broken;
    stray.replace_extension(".pdf");
    std::filesystem::remove(stray);
    assert((run(broken, "-halt-on-error -q") == 1 && !std::filesystem::exists(stray)) &&
           "-halt-on-error leaves no PDF from a document with a mistake");

    const std::filesystem::path folder = temporary / "main-output";
    std::filesystem::remove_all(folder);
    assert((run(document, "-q --output-directory=\"" + folder.string() + "\" -jobname=final") == 0 &&
            std::filesystem::exists(folder / "final.pdf")) && "--output-directory and --jobname place and name the PDF");
    assert((run(document, "-q -o \"" + folder.string() + "\" --job-name=again") == 0 &&
            std::filesystem::exists(folder / "again.pdf")) && "-o and --job-name the same");

    std::filesystem::path bare = document;
    bare.replace_extension();
    assert((run(bare, "-q") == 0) && "a document named without its extension is found as .mtex");
    const std::filesystem::path entries =
        write(temporary / "main-entries.bib", "@misc{one, author = {Ada Lovelace}, title = {Notes}, year = 1843}\n");
    std::filesystem::path listed = entries;
    std::filesystem::remove(listed.replace_extension(".pdf"));
    assert((run(temporary / "main-entries", "-q") == 0 && std::filesystem::exists(listed)) &&
           "or as .bib, a bibliography set as a list of its entries");

    // What a document inputs, from a folder of its own as well as its own.
    const std::filesystem::path shared = temporary / "main-shared";
    write(shared / "greeting.mtex", "Hello from a shared folder.");
    const std::filesystem::path inputting = write(
        temporary / "main-inputting.mtex", "\\documentclass{article}\\begin{document}\\input{greeting}\\end{document}");
    assert((run(inputting, "-q") == 1) && "an input in no folder read is a mistake");
    assert((run(inputting, "-q -I \"" + shared.string() + "\"") == 0) && "-I reads from the folder it names");
    assert((run(inputting, "-q --include-directory=\"" + (temporary / "no-such-folder").string() + "\"") == 2) &&
           "a folder that is not there is the command line's mistake");

    // The assets named outright, as an installed program would be told.
    const std::filesystem::path assets = latex::locate(__FILE__);
    assert((run(document, "-q --assets=\"" + assets.string() + "\"") == 0) && "--assets names the engine's assets");
    assert((run(document, "-q --assets=\"" + temporary.string() + "\"") == 2) && "and a folder of no fonts is refused");

    return 0;
}
