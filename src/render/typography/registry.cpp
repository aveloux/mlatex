/// @file
/// @brief Registry implementation: one face per family, one font per size.
///
/// Two caches, not one. get() first resolves a request's face through
/// surface() -- opened once per family and cut, however many sizes are asked
/// for -- and then builds or reuses the Font that wraps it at the size asked.
/// A writer later keys a PDF font resource on the face, so this is also what
/// keeps a document using several sizes of one family from embedding that
/// family's file more than once.
#include "typography/registry.hpp"
#include "logger.hpp"

#include <array>
#include <bit>

namespace render::typography {

    /// @brief Hashes a family and cut, folding the cut into the family's hash.
    /// @complexity O(n) in the family name's length.
    static constexpr std::size_t digest(const std::string_view family, const int weight, const int slant) noexcept {
        std::size_t value = 1469598103934665603ull;
        for (const char letter : family) {
            value ^= static_cast<std::size_t>(static_cast<unsigned char>(letter));
            value *= 1099511628211ull;
        }
        value ^= static_cast<std::size_t>(weight) * 1099511628211ull;
        value ^= static_cast<std::size_t>(slant) << 21;
        return value;
    }

    /// @brief Hashes a whole request: a face's digest with the size folded in.
    /// @complexity O(n) in the family name's length.
    static constexpr std::size_t digest(const Registry::Request& request) noexcept {
        // The size is taken at hundredth-point resolution, which is finer
        // than any document distinguishes and keeps the key an integer.
        std::size_t value = digest(request.family, request.weight, request.slant);
        value ^= static_cast<std::size_t>(request.size * 100.0f) << 32;
        return value;
    }

    Registry::Registry(memory::Arena& arena, Library& library, const std::size_t buckets) noexcept
        : arena(arena), library(library) {
        this->slots = std::bit_ceil(buckets > 0 ? buckets : 128);
        table = arena.allocate<Entry*>(this->slots).data;
        surfaces = arena.allocate<Surface*>(this->slots).data;
        for (std::size_t index = 0; index < this->slots; ++index) {
            table[index] = nullptr;
            surfaces[index] = nullptr;
        }
    }

    Registry::~Registry() noexcept {
        // The entries themselves are arena memory and need no freeing; the
        // HarfBuzz handles inside them are not, and do. Fonts first, since a
        // Font only borrows the Face it points into.
        if (!table) return;
        for (std::size_t index = 0; index < slots; ++index) {
            for (Entry* entry = table[index]; entry; entry = entry->next) {
                entry->font.dispose();
            }
        }
        for (std::size_t index = 0; index < slots; ++index) {
            for (Surface* surface = surfaces[index]; surface; surface = surface->next) {
                surface->face.dispose();
            }
        }
    }

    Font* Registry::get(const Request& request) noexcept {
        if (!table || request.family.empty()) return nullptr;

        const std::size_t slot = digest(request) & (slots - 1);

        for (Entry* entry = table[slot]; entry; entry = entry->next) {
            if (entry->request.family == request.family &&
                entry->request.weight == request.weight &&
                entry->request.slant == request.slant &&
                entry->request.size == request.size) {
                return &entry->font;
            }
        }

        // The face behind it, shared by every size of the family: resolved
        // before its own cache is consulted, so `text` and the file name it
        // stands for land in the same bucket -- whatever alias a caller used
        // to ask for a family, the file behind it is one entry. Library's
        // lookup is O(1) average and free of I/O past the first read, so
        // paying for it ahead of the cache costs nothing on a hit.
        const Library::Entry* file = library.read(request.family);
        if (!file) {
            Logger::log(Logger::Type::Layout, Logger::Level::Error,
                        "No font indexed for family '{}'", request.family);
            return nullptr;
        }

        const std::size_t bucket = digest(file->family, request.weight, request.slant) & (slots - 1);
        Surface* built = nullptr;
        for (Surface* found = surfaces[bucket]; found; found = found->next) {
            if (found->family == file->family && found->weight == request.weight && found->slant == request.slant) {
                built = found;
                break;
            }
        }
        if (!built) {
            built = arena.compose<Surface>();
            built->family = file->family;   // already arena-resident, via Library
            built->weight = request.weight;
            built->slant = request.slant;
            if (!built->face.compose(file->bytes)) {
                built->face.dispose();
                Logger::log(Logger::Type::Layout, Logger::Level::Error,
                            "Font family '{}' will not open", request.family);
                return nullptr;
            }
            built->next = surfaces[bucket];
            surfaces[bucket] = built;
            ++faces;
            Logger::log(Logger::Type::Layout, Logger::Level::Debug, "Opened face '{}'", built->family);
        }

        // Kept under the name it was asked for, since that is what the next
        // request for it will say -- the file's own name would never match
        // an alias, and every request by one would build the font again. The
        // name is copied, because the caller's may not outlive the request.
        Entry* entry = arena.compose<Entry>();
        entry->request = request;
        entry->request.family = arena.copy(request.family);

        // The font itself is named for the file it came from, not for
        // whatever alias it was asked for by, so that a caller holding only
        // the font can ask for the same face at another size, or for another
        // cut of it.
        if (!entry->font.compose(built->face, request.size, built->family)) {
            entry->font.dispose();
            Logger::log(Logger::Type::Layout, Logger::Level::Error,
                        "Font family '{}' will not open at {} points",
                        request.family, request.size);
            return nullptr;
        }

        entry->next = table[slot];
        table[slot] = entry;
        ++fonts;

        Logger::log(Logger::Type::Layout, Logger::Level::Debug,
                    "Built font '{}' at {} points", entry->request.family, request.size);
        return &entry->font;
    }

    const Font* Registry::cover(const Font& font, const std::uint32_t code, const bool outside) noexcept {
        const std::uint32_t block = code >> 7;
        for (const Cover& known : covers) {
            if (known.font != &font || known.block != block || known.outside != outside) continue;
            if (!known.found || known.found->index(code) != 0) return known.found;
            break;
        }

        // The cut in hand, read from its file's name as the styles read it:
        // regular, bold, italic or bold italic, and roman, sans or mono.
        const std::string_view name = font.family();
        const auto says = [name](const std::string_view part) { return name.find(part) != std::string_view::npos; };
        const bool bold = says("bold") || says("demi");
        const std::size_t cut = (bold ? 1 : 0) + (says("italic") || says("oblique") || says("slant") ? 2 : 0);

        const auto draws = [&](const std::string_view family) -> const Font* {
            if (family.empty() || family == name || !library.read(family)) return nullptr;
            const Font* found = get({.family = family, .size = font.size()});
            return found && found->index(code) != 0 ? found : nullptr;
        };
        const auto remember = [&](const Font* found) {
            covers.push_back({.font = &font, .block = block, .outside = outside, .found = found});
            return found;
        };

        // New Computer Modern, in the cut in hand and then upright, and its
        // Devanagari, which it keeps in faces of their own.
        static constexpr std::array<std::array<std::string_view, 4>, 3> carried{{
            {"newcm10-regular", "newcm10-bold", "newcm10-italic", "newcm10-bolditalic"},
            {"newcmsans10-regular", "newcmsans10-bold", "newcmsans10-oblique", "newcmsans10-boldoblique"},
            {"newcmmono10-regular", "newcmmono10-bold", "newcmmono10-italic", "newcmmono10-boldoblique"},
        }};
        const auto& own = carried[says("sans") ? 1 : says("mono") ? 2 : 0];
        for (const std::string_view family :
             {own[cut], own[0], std::string_view{bold ? "newcm10devanagari-bold" : "newcm10devanagari-regular"}}) {
            if (const Font* found = draws(family)) return remember(found);
        }
        if (!outside) return remember(nullptr);

        // The system's faces, by cut: a naskh or a serif before a sans, and
        // each system's own names for them -- Amiri and Noto's where they
        // are installed, Times New Roman by Windows' name for it and the
        // Mac's, then the faces every system has of some script or other,
        // Chinese, then the symbols' faces, last. Listed now, if they never
        // were.
        static constexpr std::array<std::array<std::string_view, 15>, 4> systems{{
            {"amiri-regular", "notonaskharabic-regular", "times", "times new roman", "notoserif-regular", "freeserif",
             "dejavusans", "geezapro", "arial", "segoeui", "tahoma", "msyh", "notosanscjk-regular", "seguisym",
             "notosanssymbols2-regular"},
            {"amiri-bold", "notonaskharabic-bold", "timesbd", "times new roman bold", "notoserif-bold", "freeserifbold",
             "dejavusans-bold", "geezapro", "arialbd", "segoeuib", "tahomabd", "msyhbd", "notosanscjk-bold", "seguisym",
             "notosanssymbols2-regular"},
            {"amiri-slanted", "notonaskharabic-regular", "timesi", "times new roman italic", "notoserif-italic",
             "freeserifitalic", "dejavusans-oblique", "geezapro", "ariali", "segoeuii", "tahoma", "msyh",
             "notosanscjk-regular", "seguisym", "notosanssymbols2-regular"},
            {"amiri-boldslanted", "notonaskharabic-bold", "timesbi", "times new roman bold italic",
             "notoserif-bolditalic", "freeserifbolditalic", "dejavusans-boldoblique", "geezapro", "arialbi",
             "segoeuiz", "tahomabd", "msyhbd", "notosanscjk-bold", "seguisym", "notosanssymbols2-regular"},
        }};
        library.system();
        for (const std::size_t pass : {cut, std::size_t{0}}) {
            for (const std::string_view family : systems[pass]) {
                if (const Font* found = draws(family)) return remember(found);
            }
        }
        return remember(nullptr);
    }

}
