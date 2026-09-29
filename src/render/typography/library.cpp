/// @file
/// @brief Library implementation: index the font tree, keep its bytes resident.
///
/// Two things happen here and nothing else. survey() walks a directory and
/// writes one table entry per font file, holding the path and no contents.
/// read() turns an entry's path into bytes the first time it is asked -- by
/// mapping the file, or reading it where it will not map -- and keeps them,
/// so every later request for that family is a table lookup. WebAssembly is
/// always read: its file system is memory already, and a mapping there would
/// only be a copy made a slower way.
#include "typography/library.hpp"
#include "logger.hpp"

#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace render::typography {

    /// @brief Folds one byte to lower case, ASCII only.
    ///
    /// Deliberately not std::tolower: that consults the current locale,
    /// which can map bytes differently between runs of the same program.
    /// A font index has to fold the same way every time.
    ///
    /// @param letter Byte to fold.
    /// @return The folded byte.
    static constexpr char fold(const char letter) noexcept {
        return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + ('a' - 'A')) : letter;
    }

    /// @brief Hashes a name, folding it as it goes.
    ///
    /// FNV-1a: one exclusive-or and one multiply per byte, no table, and a
    /// good enough spread for the few hundred names a font tree holds.
    ///
    /// @param name Name to hash; folded here rather than by the caller.
    /// @return Its hash.
    /// @complexity O(n) in the name's length, which is a file name.
    static constexpr std::size_t digest(const std::string_view name) noexcept {
        std::size_t value = 1469598103934665603ull;
        for (const char letter : name) {
            value ^= static_cast<std::size_t>(static_cast<unsigned char>(fold(letter)));
            value *= 1099511628211ull;
        }
        return value;
    }

    /// @brief Compares a name as written against a family already folded.
    /// @param query  Name as written; folded here.
    /// @param family Family as stored, already folded.
    /// @return True when they name the same face.
    static constexpr bool same(const std::string_view query, const std::string_view family) noexcept {
        if (query.size() != family.size()) return false;
        for (std::size_t index = 0; index < query.size(); ++index) {
            if (fold(query[index]) != family[index]) return false;
        }
        return true;
    }

    Library::Library(memory::Arena& arena, const std::size_t buckets) noexcept : arena(arena) {
        // Rounded to a power of two so that a lookup masks instead of dividing.
        // A modulo on the hot path costs more than the comparison after it.
        this->slots = std::bit_ceil(buckets > 0 ? buckets : 512);
        table = arena.allocate<Entry*>(this->slots).data;
        for (std::size_t index = 0; index < this->slots; ++index) table[index] = nullptr;
    }

    Library::~Library() noexcept {
        // The views, not the entries: an entry an alias took over no longer
        // names the bytes it once held, and a Face may be reading them still
        // right up to this point.
        for (const View& view : views) {
        #if defined(_WIN32)
            UnmapViewOfFile(view.data);
        #elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
            munmap(const_cast<void*>(view.data), view.size);
        #endif
        }
    }

    std::size_t Library::survey(const std::string_view directory, const std::span<const std::string_view> files) {
        if (directory.empty() || !table) return 0;

        // A listing handed in is taken as the tree's own, and nothing on the
        // disk is asked about until a face is read.
        std::error_code failure;
        const std::filesystem::path root(directory);
        if (files.empty() && (!std::filesystem::is_directory(root, failure) || failure)) {
            Logger::log(Logger::Type::Layout, Logger::Level::Warning,
                        "Font directory missing: {}", directory);
            return 0;
        }

        std::size_t indexed = 0;

        // One file, by its full path. Everything it is registered under --
        // the stem, the folder it sits in -- is cut out of that one string,
        // because on a system whose paths are not bytes each conversion is a
        // transcode, and three of them per file would be most of the cost of
        // surveying a tree.
        const auto enter = [&](const std::string_view name) {
            const std::size_t dot = name.find_last_of('.');
            if (dot == std::string_view::npos) return;
            // A font file, judging by its extension: the container formats
            // a face is read from here.
            if (const std::string_view extension = name.substr(dot);
                extension.size() != 4 || !(same(extension, ".otf") || same(extension, ".ttf") ||
                                           same(extension, ".ttc") || same(extension, ".pfb"))) {
                return;
            }

            const std::size_t cut = name.find_last_of("/\\");
            const std::size_t opening = cut == std::string_view::npos ? 0 : cut + 1;
            const std::string_view stem(name.data() + opening, dot - opening);
            if (stem.empty()) return;

            const std::size_t slot = digest(stem) & (slots - 1);

            // First name wins. Two files sharing a stem is a collision the
            // tree's author has to resolve; preferring the later one silently
            // would make the choice depend on the order the walk happened to
            // hand them over, which is not the same on two machines.
            for (const Entry* entry = table[slot]; entry; entry = entry->next) {
                if (same(stem, entry->family)) return;
            }

            // Stored folded, so a lookup has only its own query to fold.
            const memory::Slice<char> family = arena.allocate<char>(stem.size());
            for (std::size_t index = 0; index < stem.size(); ++index) {
                family[index] = fold(stem[index]);
            }

            // Kept with a terminating zero after it, so read() can hand the
            // path to the system as it stands rather than copying it first.
            const memory::Slice<char> path = arena.allocate<char>(name.size() + 1);
            std::memcpy(path.data, name.data(), name.size());
            path[name.size()] = '\0';

            Entry* entry = arena.compose<Entry>();
            entry->family = std::string_view(family.data, family.count);
            entry->path = std::string_view(path.data, name.size());
            entry->next = table[slot];
            table[slot] = entry;

            ++faces;
            ++indexed;

            // The directory a face sits in becomes a family of its own, so a
            // tree laid out as `fonts/text` answers `text` without anyone
            // writing that down. It resolves to the first face in the folder
            // by name, which is the same one on every machine however the
            // walk ordered them.
            if (cut == std::string_view::npos) return;

            const std::size_t parent = name.find_last_of("/\\", cut - 1);
            const std::size_t head = parent == std::string_view::npos ? 0 : parent + 1;
            const std::string_view folder(name.data() + head, cut - head);
            if (folder.empty() || same(folder, entry->family)) return;

            const Entry* named = nullptr;
            for (const Entry* step = table[digest(folder) & (slots - 1)]; step; step = step->next) {
                if (same(folder, step->family)) {
                    named = step;
                    break;
                }
            }

            // Earlier by name wins, so the choice does not depend on order.
            if (!named) {
                alias(folder, stem);
            } else if (named->target && entry->family < named->target->family) {
                alias(folder, stem);
            }
        };

        if (!files.empty()) {
            std::string path(directory);
            while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.pop_back();
            path += '/';
            const std::size_t base = path.size();
            for (const std::string_view file : files) {
                path.resize(base);
                path += file;
                enter(path);
            }
            Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                        "Indexed {} font files under {} from the build's listing", indexed, directory);
            return indexed;
        }

    #if defined(_WIN32)
        // The tree as the system lists it: one search per directory, asked
        // for its whole listing in one go and for names and attributes alone.
        // The path is kept twice while the walk runs -- wide for the search,
        // UTF-8 for the index -- and each name is converted once, onto the
        // end of the UTF-8 one, rather than the whole path per file.
        std::wstring search = root.wstring();
        while (!search.empty() && (search.back() == L'\\' || search.back() == L'/')) search.pop_back();

        std::string text;
        const auto append = [&text](const std::wstring_view name) {
            const std::size_t length = text.size();
            text.resize(length + name.size() * 3);
            const int written = WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()),
                                                    text.data() + length, static_cast<int>(name.size() * 3),
                                                    nullptr, nullptr);
            text.resize(length + static_cast<std::size_t>(written > 0 ? written : 0));
        };
        append(search);

        // Depth first, each directory entered where the listing reaches it,
        // which is the order std::filesystem's recursive walk hands them over
        // in. A link to a directory elsewhere is not followed, as it is not
        // there either.
        const auto descend = [&](const auto& self) -> void {
            const std::size_t wide = search.size();
            const std::size_t narrow = text.size();

            search += L"\\*";
            WIN32_FIND_DATAW found{};
            const HANDLE listing = FindFirstFileExW(search.c_str(), FindExInfoBasic, &found,
                                                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            search.resize(wide);
            if (listing == INVALID_HANDLE_VALUE) return;

            do {
                const std::wstring_view name(found.cFileName);
                const bool folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (folder && (name == L"." || name == L".." ||
                               (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)) {
                    continue;
                }

                text += '\\';
                append(name);
                if (folder) {
                    search += L'\\';
                    search += name;
                    self(self);
                    search.resize(wide);
                } else {
                    enter(text);
                }
                text.resize(narrow);
            } while (FindNextFileW(listing, &found));

            FindClose(listing);
        };
        descend(descend);
    #else
        for (std::filesystem::recursive_directory_iterator walk(root, failure), stop;
             !failure && walk != stop; walk.increment(failure)) {
            if (!walk->is_regular_file(failure) || failure) continue;
            enter(walk->path().string());
        }
    #endif

        Logger::log(Logger::Type::Layout, Logger::Level::Informative,
                    "Indexed {} font files under {}", indexed, directory);
        return indexed;
    }

    std::size_t Library::system() {
        const std::scoped_lock lock(guard);
        if (searched) return 0;
        searched = true;

        // Where each system keeps the faces it was installed with, and those
        // its user added since. A folder that is not there is passed over
        // without a word: most of these exist on one system in three.
        std::vector<std::filesystem::path> folders;
    #if defined(_WIN32)
        if (wchar_t windows[MAX_PATH]; GetWindowsDirectoryW(windows, MAX_PATH) > 0) {
            folders.emplace_back(std::filesystem::path(windows) / "Fonts");
        }
        if (wchar_t local[MAX_PATH]; GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) > 0) {
            folders.emplace_back(std::filesystem::path(local) / "Microsoft" / "Windows" / "Fonts");
        }
    #elif defined(__APPLE__)
        folders = {"/System/Library/Fonts", "/Library/Fonts"};
        if (const char* home = std::getenv("HOME")) folders.emplace_back(std::filesystem::path(home) / "Library" / "Fonts");
    #elif defined(__unix__) && !defined(__EMSCRIPTEN__)
        folders = {"/usr/share/fonts", "/usr/local/share/fonts"};
        if (const char* home = std::getenv("HOME")) {
            folders.emplace_back(std::filesystem::path(home) / ".local" / "share" / "fonts");
            folders.emplace_back(std::filesystem::path(home) / ".fonts");
        }
    #endif

        std::size_t indexed = 0;
        std::error_code failure;
        for (const std::filesystem::path& folder : folders) {
            if (!std::filesystem::is_directory(folder, failure) || failure) continue;
            const std::u8string written = folder.u8string();
            indexed += survey(std::string_view(reinterpret_cast<const char*>(written.data()), written.size()));
        }
        return indexed;
    }

    void Library::alias(const std::string_view name, const std::string_view family) {
        if (name.empty() || family.empty() || !table) return;

        const std::size_t slot = digest(name) & (slots - 1);

        Entry* entry = nullptr;
        for (Entry* step = table[slot]; step; step = step->next) {
            if (same(name, step->family)) {
                entry = step;
                break;
            }
        }

        if (!entry) {
            const memory::Slice<char> folded = arena.allocate<char>(name.size());
            for (std::size_t index = 0; index < name.size(); ++index) {
                folded[index] = fold(name[index]);
            }
            entry = arena.compose<Entry>();
            entry->family = std::string_view(folded.data, folded.count);
            entry->next = table[slot];
            table[slot] = entry;
        }

        // An alias owns no file of its own, so anything it held as a face is
        // dropped. Naming a target that is not indexed yet leaves the alias
        // dangling rather than failing, which is what lets a driver express a
        // preference before the tree is surveyed.
        entry->path = {};
        entry->bytes = {};
        entry->target = nullptr;

        for (Entry* step = table[digest(family) & (slots - 1)]; step; step = step->next) {
            if (same(family, step->family) && step != entry) {
                entry->target = step;
                break;
            }
        }
    }

    const Library::Entry* Library::read(const std::string_view family) const {
        const std::scoped_lock lock(guard);
        if (family.empty() || !table) return nullptr;

        // The family's entry, and through an alias the entry it names: the
        // chain is one link deep in practice, but a document that aliases in
        // a circle must not spin, so the walk is bounded rather than trusted.
        Entry* entry = table[digest(family) & (slots - 1)];
        while (entry && !same(family, entry->family)) entry = entry->next;
        for (int hops = 0; entry && entry->target && hops < 8; ++hops) entry = entry->target;
        if (!entry || entry->path.empty()) return nullptr;
        if (!entry->bytes.empty()) return entry;

        // Mapped wherever the system will map it; see the class notes for
        // why. A mapping outlives the handles that made it, so they are
        // closed at once and only the view is kept.
        const void* view = nullptr;
        std::size_t size = 0;

    #if defined(_WIN32)
        std::wstring wide(entry->path.size(), L'\0');
        const int length = MultiByteToWideChar(CP_UTF8, 0, entry->path.data(), static_cast<int>(entry->path.size()),
                                               wide.data(), static_cast<int>(wide.size()));
        wide.resize(static_cast<std::size_t>(length > 0 ? length : 0));

        if (const HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
            file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER extent{};
            if (GetFileSizeEx(file, &extent) && extent.QuadPart > 0) {
                if (const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)) {
                    if ((view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0))) {
                        size = static_cast<std::size_t>(extent.QuadPart);
                    }
                    CloseHandle(mapping);
                }
            }
            CloseHandle(file);
        }
    #elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
        if (const int file = open(entry->path.data(), O_RDONLY | O_CLOEXEC); file >= 0) {
            struct stat status{};
            if (fstat(file, &status) == 0 && status.st_size > 0) {
                if (void* mapped = mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ, MAP_PRIVATE, file, 0);
                    mapped != MAP_FAILED) {
                    view = mapped;
                    size = static_cast<std::size_t>(status.st_size);
                }
            }
            close(file);
        }
    #endif

        if (view) {
            views.push_back(View{.data = view, .size = size});

            entry->bytes = std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(view), size);
            bytes += size;

            Logger::log(Logger::Type::Layout, Logger::Level::Debug,
                        "Mapped {} bytes for family '{}'", size, entry->family);
            return entry;
        }

        // Read, where it would not map: the path is UTF-8, which is what the
        // stream is told it is rather than left to guess.
        std::ifstream file(std::filesystem::path(std::u8string_view(
                               reinterpret_cast<const char8_t*>(entry->path.data()), entry->path.size())),
                           std::ios::binary | std::ios::ate);
        if (!file) {
            Logger::log(Logger::Type::Layout, Logger::Level::Error,
                        "Font file unreadable: {}", entry->path);
            return nullptr;
        }

        const std::streamsize extent = file.tellg();
        if (extent <= 0) return nullptr;
        file.seekg(0, std::ios::beg);

        // Into storage the library keeps: a face borrows this span rather than
        // taking a copy, so a second size of the same family costs nothing.
        const auto count = static_cast<std::size_t>(extent);
        auto storage = std::make_unique_for_overwrite<std::uint8_t[]>(count);
        if (!file.read(reinterpret_cast<char*>(storage.get()), extent)) return nullptr;

        entry->bytes = std::span<const std::uint8_t>(storage.get(), count);
        bytes += count;
        copies.push_back(std::move(storage));

        Logger::log(Logger::Type::Layout, Logger::Level::Debug,
                    "Loaded {} bytes for family '{}'", count, entry->family);
        return entry;
    }

}
