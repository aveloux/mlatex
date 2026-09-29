/// @file
/// @brief The engine as a C library: see network/ffi.hpp.
///
/// Each operation is engine::Session's own, given a C signature: strings
/// arrive as zero-terminated UTF-8, a command's handler is a C function and a
/// pointer, and the PDF goes back as a pointer into the session. Nothing here
/// decides anything the session does not; it only translates.
#include "network/ffi.hpp"
#include "engine.hpp"

#include <filesystem>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
    #include <dlfcn.h>
#endif

namespace network {

    /// @brief The opaque handle the C side holds: the session itself.
    struct Session : ::engine::Session {
        using ::engine::Session::Session;
    };

    /// @brief What one call of a command has written so far.
    struct Output {
        std::string text{};   ///< Read in the command's place once it returns.
    };

    /// @brief Latex::compose: a session over the assets, or none.
    static Session* compose(const char* assets) {
        // The assets named from the C side, read as UTF-8 on every platform --
        // on Windows a narrow string would otherwise be taken in the local
        // code page, and a name outside it would not survive the trip -- or,
        // when none is named, found from where this library was loaded, the
        // way the command line finds its own.
        std::filesystem::path root;
        if (assets) {
            const std::string_view view(assets);
            root = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(view.data()), view.size()));
        } else {
            const std::filesystem::path library = []() -> std::filesystem::path {
            #if defined(_WIN32)
                HMODULE module = nullptr;
                if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        reinterpret_cast<LPCWSTR>(&latex), &module)) {
                    return std::filesystem::path{};
                }
                std::wstring buffer(MAX_PATH, L'\0');
                for (;;) {
                    const DWORD written = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
                    if (written == 0) return std::filesystem::path{};
                    if (written < buffer.size()) {
                        buffer.resize(written);
                        return std::filesystem::path(buffer);
                    }
                    buffer.resize(buffer.size() * 2);
                }
            #elif defined(__unix__) || defined(__APPLE__)
                Dl_info info{};
                if (dladdr(reinterpret_cast<const void*>(&latex), &info) && info.dli_fname) {
                    std::error_code failure;
                    if (auto resolved = std::filesystem::canonical(info.dli_fname, failure); !failure) return resolved;
                }
                return std::filesystem::path{};
            #else
                return std::filesystem::path{};
            #endif
            }();
            root = ::engine::locate(library);
        }
        std::error_code failure;
        if (root.empty() || !std::filesystem::is_directory(root / "fonts", failure) || failure) return nullptr;
        return new (std::nothrow) Session(std::move(root));
    }

    /// @brief Latex::dispose.
    static void dispose(Session* session) {
        delete session;
    }

    /// @brief Latex::set.
    static int set(Session* session, const char* name, const char* value) {
        if (!session || !name || !*name || !value) return 0;
        session->set(name, value);
        return 1;
    }

    /// @brief Latex::unset.
    static int unset(Session* session, const char* name) {
        if (!session || !name) return 0;
        session->unset(name);
        return 1;
    }

    /// @brief Latex::define: the C function behind the engine's own kind of
    ///        handler, the arguments lent out as C strings for the length of
    ///        the call and whatever it wrote taken back as the command's text.
    static int define(Session* session, const char* name, const size_t arity, const Handler handler, void* data) {
        if (!session || !name || !handler || arity > 9) return 0;

        std::string_view bare(name);
        if (bare.starts_with('\\')) bare.remove_prefix(1);
        if (bare.empty()) return 0;

        session->define({
            .name = std::string(bare),
            .arity = arity,
            .handler = [handler, data](const std::span<const std::string> arguments) {
                std::vector<const char*> texts;
                texts.reserve(arguments.size());
                for (const std::string& argument : arguments) texts.push_back(argument.c_str());

                Output output;
                handler(data, texts.data(), texts.size(), &output);
                return std::move(output.text);
            },
        });
        return 1;
    }

    /// @brief Latex::forget.
    static int forget(Session* session, const char* name) {
        if (!session || !name) return 0;
        session->forget(name);
        return 1;
    }

    /// @brief Latex::write.
    static void write(Output* output, const char* text, const size_t length) {
        if (output && text) output->text.append(text, length);
    }

    /// @brief Latex::provide.
    static int provide(Session* session, const char* name, const void* bytes, const size_t length) {
        if (!session || !name || !*name || (!bytes && length > 0)) return 0;
        session->provide(name, bytes ? std::string(static_cast<const char*>(bytes), length) : std::string());
        return 1;
    }

    /// @brief Latex::withdraw.
    static int withdraw(Session* session, const char* name) {
        if (!session || !name) return 0;
        session->withdraw(name);
        return 1;
    }

    /// @brief Latex::typeset.
    static int typeset(Session* session, const char* document, const size_t length, const unsigned char** pdf,
                       size_t* size) {
        if (pdf) *pdf = nullptr;
        if (size) *size = 0;
        if (!session || !document) return 0;

        const bool made = session->typeset(std::string_view(document, length));
        if (const std::string& bytes = session->pdf(); !bytes.empty()) {
            if (pdf) *pdf = reinterpret_cast<const unsigned char*>(bytes.data());
            if (size) *size = bytes.size();
        }
        return made ? 1 : 0;
    }

    /// @brief Latex::error.
    static const char* error(const Session* session) {
        return session ? session->error().c_str() : "";
    }

    extern "C" {

        const Latex* latex(void) {
            static constexpr Latex table{
                .compose = compose,
                .dispose = dispose,
                .set = set,
                .unset = unset,
                .define = define,
                .forget = forget,
                .write = write,
                .provide = provide,
                .withdraw = withdraw,
                .typeset = typeset,
                .error = error,
            };
            return &table;
        }

    }

}
