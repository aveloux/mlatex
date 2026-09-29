#include "network/http.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

// What a document reads by name: a file on disk, found against a base when it
// is relative, and a request refused before it is sent when its text could
// smuggle a second header or a second request into the first.

int main() {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "latex_http_test";
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / "bytes.bin";

    const std::vector<std::uint8_t> written{0x00, 0x01, 0xFF, 'a', '\r', '\n', 'b'};
    {
        std::ofstream output(file, std::ios::binary);
        output.write(reinterpret_cast<const char*>(written.data()), static_cast<std::streamsize>(written.size()));
    }

    const auto whole = network::get(file.string());
    assert((whole.has_value() && *whole == written) && "a file is read byte for byte");

    const auto relative = network::get("bytes.bin", folder.string());
    assert((relative.has_value() && *relative == written) && "a relative name is found against its base");

    assert((!network::get((folder / "missing.bin").string()).has_value()) && "a file that is not there is nothing");

    // Refused before anything could be sent, so these need no network.
    assert((!network::compose("GET", "http://example.org/\r\nX-Injected: yes").has_value()) &&
           "a line break in the address is refused");
    assert((!network::compose("GE\nT", "http://example.org/").has_value()) &&
           "a line break in the method is refused");
    assert((!network::compose("GET", "ftp://example.org/").has_value()) && "only HTTP and HTTPS are asked for");

    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);

    return 0;
}
