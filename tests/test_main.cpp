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
    assert((run(broken, "") != 0) && "a document with a mistake fails the run");

    return 0;
}
