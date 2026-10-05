#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

// The command line: the executable run as a person runs it -- a document in,
// its PDF beside it out, `--set=name=value` handed to the document as a
// variable -- and a mistake in either the arguments or the document ending
// it with a failing status. BINARY is the built executable's path, which the
// build hands this test.

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

/// Writes a document where the executable can read it.
static std::filesystem::path write(const std::string_view name, const std::string_view text) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

int main() {
    const std::filesystem::path document =
        write("main-plain.mtex", "\\documentclass{article}\\begin{document}Hello.\\end{document}");
    std::filesystem::path pdf = document;
    pdf.replace_extension(".pdf");
    std::filesystem::remove(pdf);
    assert((run(document, "") == 0) && "a document sets, and the run succeeds");
    assert((std::filesystem::file_size(pdf) > 0) && "its PDF beside it, named for it");

    const std::filesystem::path valued =
        write("main-valued.mtex", "\\documentclass{article}\\begin{document}\\variable{customer}\\end{document}");
    assert((run(valued, "--set=customer=Acme") == 0) && "--set hands the document a variable");
    assert((run(valued, "") != 0) && "and without it the variable is missing, and the run fails");
    assert((run(valued, "--set=customer") != 0) && "a --set without a value is refused");

    assert((run(std::filesystem::temp_directory_path() / "no-such-document.mtex", "") != 0) &&
           "a document that is not there fails the run");

    const std::filesystem::path broken =
        write("main-broken.mtex", "\\documentclass{article}\\begin{document}\\nosuchcommand\\end{document}");
    assert((run(broken, "") == 1) && "a document with a mistake fails the run");

    // TeX's options, with one dash or two, and the engine's own.
    assert((run(document, "--version") == 0 && run(document, "-v") == 0) && "--version");
    assert((run(document, "--help") == 0 && run(document, "-h") == 0) && "--help");
    assert((run(document, "--no-such-option") == 2) && "an option nothing knows is the command line's mistake");
    assert((run(document, "-interaction=batchmode") == 0 && run(document, "--interaction nonstopmode") == 0) &&
           "-interaction, written as TeX writes it or with its value after it");
    assert((run(document, "--interaction=sometimes") == 2) && "an interaction TeX has no name for");
    assert((run(document, "--aot=windows -q") == 0 && run(document, "--aot=amiga") == 2) &&
           "--aot for a system, and none it does not know");

    std::filesystem::remove(pdf);
    assert((run(document, "--jit") == 0 && !std::filesystem::exists(pdf)) && "--jit is taken, and does nothing yet");
    assert((run(document, "--draftmode -q") == 0 && !std::filesystem::exists(pdf)) && "--draftmode writes no PDF");

    std::filesystem::path stray = broken;
    stray.replace_extension(".pdf");
    std::filesystem::remove(stray);
    assert((run(broken, "-halt-on-error -q") == 1 && !std::filesystem::exists(stray)) &&
           "-halt-on-error leaves no PDF from a document with a mistake");

    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "main-output";
    std::filesystem::remove_all(folder);
    assert((run(document, "-q --output-directory=\"" + folder.string() + "\" -jobname=final") == 0 &&
            std::filesystem::exists(folder / "final.pdf")) && "--output-directory and --jobname place and name the PDF");

    std::filesystem::path bare = document;
    bare.replace_extension();
    assert((run(bare, "-q") == 0) && "a document named without its extension is found as .mtex");

    return 0;
}
