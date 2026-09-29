/// @file
/// @brief Http implementation: one request, any method, through the client
///        the platform already has, and a plain file read for whichever
///        sources are not a URL at all.
///
/// Requests go through WinHTTP on Windows, which every Windows machine
/// already has, and through libcurl on Linux and macOS when the build found
/// it. Without either -- a WebAssembly module above all, whose page fetches
/// for it -- a request is refused the way one that could not be sent is, and
/// everything that reads a file, which is most of what `\\includegraphics`
/// does, works the same on every platform.
#include "network/http.hpp"

#include <filesystem>
#include <fstream>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <winhttp.h>
#elif defined(LATEX_CURL)
    #include <curl/curl.h>
    #include <string>
#endif

namespace network {

#if defined(_WIN32)
    /// @brief Closes a WinHTTP handle when it goes out of scope.
    struct Handle {
        explicit Handle(const HINTERNET handle) noexcept : value(handle) {}
        Handle(const Handle&) = delete("a handle closes its connection when it is destroyed, once");
        Handle& operator=(const Handle&) = delete("a handle closes its connection when it is destroyed, once");
        ~Handle() { if (value) WinHttpCloseHandle(value); }

        operator HINTERNET() const noexcept { return value; }
        [[nodiscard]] bool ok() const noexcept { return value != nullptr; }

        HINTERNET value;
    };
#endif

    /// @brief Would this text let a caller splice in a second header or
    ///        a second request?
    /// @param text Text about to become one field of a request line.
    /// @return True when it holds a carriage return or a line feed.
    [[nodiscard]] static bool unsafe(const std::string_view text) noexcept {
        return text.find('\r') != std::string_view::npos ||
               text.find('\n') != std::string_view::npos;
    }

    std::optional<Response> compose(
        const std::string_view method,
        const std::string_view url,
        const std::string_view body
    ) {
        if (unsafe(method) || unsafe(url)) return std::nullopt;

        // Only the two schemes this sends: anything else is refused here, on
        // every platform, rather than left to whichever client would try it.
        if (!url.starts_with("http://") && !url.starts_with("https://")) return std::nullopt;

    #if defined(_WIN32)
        // WinHTTP is wide throughout; a URL and a method are both pure
        // ASCII, so widening them byte for byte is exact rather than a real
        // transcoding.
        const std::wstring wide(url.begin(), url.end());
        const std::wstring verb(method.begin(), method.end());

        wchar_t host[256]{};
        wchar_t path[2048]{};
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.lpszHostName = host;
        parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));

        if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) {
            return std::nullopt;
        }

        const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

        const Handle session{WinHttpOpen(
            L"latex-engine/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
        if (!session.ok()) return std::nullopt;

        const Handle connection{WinHttpConnect(session, host, parts.nPort, 0)};
        if (!connection.ok()) return std::nullopt;

        const Handle request{WinHttpOpenRequest(
            connection, verb.c_str(), path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
        if (!request.ok()) return std::nullopt;

        // A body is sent as raw bytes exactly as given; a document asking
        // for JSON or form encoding writes the header itself, as \\httppost
        // does with its own Content-Type.
        const auto sent = body.empty()
            ? WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            : WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 const_cast<void*>(static_cast<const void*>(body.data())),
                                 static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);

        if (!sent || !WinHttpReceiveResponse(request, nullptr)) return std::nullopt;

        Response response;

        DWORD status = 0;
        DWORD size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            response.status = static_cast<int>(status);
        }

        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            const std::size_t start = response.body.size();
            response.body.resize(start + available);

            DWORD read_ = 0;
            if (!WinHttpReadData(request, response.body.data() + start, available, &read_)) break;
            response.body.resize(start + read_);
        }

        return response;
    #elif defined(LATEX_CURL)
        // Handed the method, the URL and the body as separate options, as
        // WinHTTP is; libcurl is what puts them on the wire.
        CURL* handle = curl_easy_init();
        if (!handle) return std::nullopt;

        const std::string address(url);
        const std::string verb(method);
        Response response;

        // Each piece of the body as it arrives, onto the end of the last.
        const curl_write_callback keep = [](char* data, const std::size_t size, const std::size_t count,
                                            void* target) -> std::size_t {
            auto* received = static_cast<std::vector<std::uint8_t>*>(target);
            const auto* start = reinterpret_cast<const std::uint8_t*>(data);
            received->insert(received->end(), start, start + size * count);
            return size * count;
        };

        curl_easy_setopt(handle, CURLOPT_URL, address.c_str());
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, verb.c_str());
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "latex-engine/1.0");
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, keep);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);
        if (!body.empty()) {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        }

        const CURLcode code = curl_easy_perform(handle);
        long status = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(handle);

        if (code != CURLE_OK) return std::nullopt;
        response.status = static_cast<int>(status);
        return response;
    #else
        // No HTTP client on this platform: refused as a request that could
        // not be sent would be.
        static_cast<void>(body);
        return std::nullopt;
    #endif
    }

    std::optional<std::vector<std::uint8_t>> get(
        const std::string_view source,
        const std::string_view base
    ) {
        if (source.starts_with("http://") || source.starts_with("https://")) {
            const std::optional<Response> response = compose("GET", source);
            if (!response) return std::nullopt;
            return response->body;
        }

        std::filesystem::path path(source);
        if (!path.is_absolute() && !base.empty()) path = std::filesystem::path(base) / path;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return std::nullopt;

        const std::streamsize size = file.tellg();
        if (size < 0) return std::nullopt;
        file.seekg(0);

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) return std::nullopt;
        return bytes;
    }

}
