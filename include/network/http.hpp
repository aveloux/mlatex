#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

/// @brief Everything the engine sends or reads over a wire, or off disk in
///        its place.
///
/// One namespace, one file: `\includegraphics`'s source, `\httpget` and
/// `\httppost` all reach the same two functions, so a change to how a
/// request is built is made once.
namespace network {

    /// @brief One HTTP response: a status code and a body.
    struct Response {
        int status{0};
        std::vector<std::uint8_t> body{};
    };

    /// @brief Sends an HTTP or HTTPS request and composes the response.
    ///
    /// Sent with the client the platform already has: WinHTTP on Windows,
    /// libcurl on Linux and macOS when it is installed. Without either, as in
    /// a WebAssembly module, nothing is sent and nothing comes back.
    ///
    /// @par Safety
    /// A method or a URL holding a carriage return or a line feed is
    /// refused outright rather than sent, since either would let a caller
    /// splice a second header -- or a second request -- into what was
    /// meant to be one field of the first. Nothing here builds a request
    /// out of concatenated text a document controls; the client is handed
    /// the method, the address and the body separately, and it is what puts
    /// them on the wire.
    ///
    /// @param method Request method: `"GET"`, `"POST"`, and so on.
    /// @param url    Where to send it; must start with `http://` or `https://`.
    /// @param body   Sent as the request body when not empty.
    /// @return The response, or nullopt when it could not be sent at all,
    ///         it failed the check above, or no response came back. A
    ///         non-2xx status is still a response.
    [[nodiscard]] std::optional<Response> compose(
        std::string_view method,
        std::string_view url,
        std::string_view body = {}
    );

    /// @brief Reads the bytes an `\includegraphics` source, or any other
    ///        caller, names.
    ///
    /// A source starting with `http://` or `https://` is read with
    /// compose(); anything else is a path on disk, resolved against
    /// @p base when it is not already absolute.
    ///
    /// @param source Where to read from.
    /// @param base   A directory to resolve a relative path against.
    /// @return The bytes, or nullopt when neither reading nor a request worked.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get(
        std::string_view source,
        std::string_view base = {}
    );

}
