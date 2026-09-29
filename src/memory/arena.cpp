/// @file
/// @brief Arena implementation: blocks mapped from the operating system.
///
/// Blocks bypass the C++ heap deliberately. A debugging heap fills every
/// allocation with a guard pattern, which for a multi-megabyte arena costs
/// more than the rest of a run; mapped pages are zero-filled lazily by the
/// kernel instead, and only the pages actually touched are ever committed.
#include "memory/arena.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    #include <sys/mman.h>
#endif

namespace memory {

    Arena::Arena(const std::size_t size) : capacity(std::max<std::size_t>(size, 4096)) {
        grow(0, 1);
    }

    Arena::~Arena() {
        for (const Block& block : blocks) {
        #if defined(_WIN32)
            VirtualFree(block.data, 0, MEM_RELEASE);
        #elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
            munmap(block.data, block.capacity);
        #else
            std::free(block.data);
        #endif
        }
    }

    void* Arena::grow(const std::size_t size, const std::size_t alignment) {
        while (capacity < size + alignment) capacity *= 2;

        // Zeroed, writable memory straight from the system: mapped where it
        // maps, and from the C heap only where nothing else is to be had.
    #if defined(_WIN32)
        auto* data = static_cast<std::byte*>(VirtualAlloc(nullptr, capacity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    #elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
        void* mapped = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        auto* data = mapped == MAP_FAILED ? nullptr : static_cast<std::byte*>(mapped);
    #else
        auto* data = static_cast<std::byte*>(std::calloc(capacity, 1));
    #endif
        if (!data) throw std::bad_alloc{};

        blocks.push_back(Block{data, capacity});
        cursor = data;
        limit = data + capacity;
        capacity *= 2;

        return allocate(size, alignment);
    }

    std::string_view Arena::copy(const std::string_view text) {
        if (text.empty()) return {};
        auto* target = static_cast<char*>(allocate(text.size(), 1));
        std::memcpy(target, text.data(), text.size());
        return {target, text.size()};
    }

}
