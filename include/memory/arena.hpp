#pragma once

#include "memory/slice.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace memory {

    /// @brief Region allocator: bump-pointer allocation, released all at once.
    ///
    /// Blocks are mapped directly from the operating system, so pages are
    /// committed on first touch and an unused arena costs nothing. Allocation
    /// is an alignment and an addition; nothing is freed individually and no
    /// destructor runs, so only objects whose destructors do not matter belong
    /// here.
    ///
    /// @par Use
    /// @code
    /// memory::Arena arena(1 << 20);
    /// auto* node = arena.compose<Node>(Node::Type::Text);
    /// memory::Slice<int> values = arena.allocate<int>(16);
    /// std::string_view text = arena.copy("persistent");
    /// @endcode
    class Arena {
    public:
        /// @brief Creates an arena whose first block holds at least @p size bytes.
        /// @param size Size of the first block; later blocks double.
        explicit Arena(std::size_t size = std::size_t{1} << 16);

        /// @brief Returns every block to the operating system.
        ~Arena();

        Arena(const Arena&) = delete("an arena owns its blocks, and two of them would free the same blocks twice");
        Arena& operator=(const Arena&) = delete("an arena owns its blocks, and two of them would free the same blocks twice");

        /// @brief Allocates raw storage.
        /// @param size      Bytes wanted.
        /// @param alignment Required alignment; a power of two.
        /// @return Storage valid for the life of the arena.
        /// @complexity O(1); a new block is mapped when the current one is full.
        [[nodiscard]] void* allocate(const std::size_t size, const std::size_t alignment) {
            const std::uintptr_t start = (reinterpret_cast<std::uintptr_t>(cursor) + alignment - 1) & ~(alignment - 1);
            if (start + size > reinterpret_cast<std::uintptr_t>(limit)) return grow(size, alignment);
            cursor = reinterpret_cast<std::byte*>(start + size);
            return reinterpret_cast<void*>(start);
        }

        /// @brief Allocates uninitialised storage for @p count objects.
        /// @tparam Type Element type.
        /// @param count Number of elements.
        /// @return The storage, or an empty slice when @p count is zero.
        /// @complexity O(1).
        template <typename Type>
        [[nodiscard]] Slice<Type> allocate(const std::size_t count) {
            if (count == 0) return {};
            return Slice<Type>{static_cast<Type*>(allocate(sizeof(Type) * count, alignof(Type))), count};
        }

        /// @brief Constructs one object in the arena.
        /// @tparam Type      Object type.
        /// @tparam Arguments Constructor argument types.
        /// @param arguments Forwarded to the constructor.
        /// @return The object; its destructor never runs.
        /// @complexity O(1) plus the constructor.
        template <typename Type, typename... Arguments>
        [[nodiscard]] Type* compose(Arguments&&... arguments) {
            return ::new (allocate(sizeof(Type), alignof(Type))) Type(std::forward<Arguments>(arguments)...);
        }

        /// @brief Copies text into the arena.
        /// @param text Text to copy.
        /// @return A view of the copy, valid for the life of the arena.
        /// @complexity O(n) in the length of @p text.
        [[nodiscard]] std::string_view copy(std::string_view text);

    private:
        /// @brief Maps a new block large enough for one allocation and serves it.
        /// @param size      Bytes wanted.
        /// @param alignment Required alignment.
        /// @return Storage for the allocation that did not fit.
        void* grow(std::size_t size, std::size_t alignment);

        /// @brief One mapped block.
        struct Block {
            std::byte* data;         ///< Start of the block.
            std::size_t capacity;    ///< Its size in bytes.
        };

        std::vector<Block> blocks{};   ///< Every block mapped so far.
        std::byte* cursor{nullptr};    ///< Next free byte in the newest block.
        std::byte* limit{nullptr};     ///< End of the newest block.
        std::size_t capacity{0};       ///< Size of the next block to map.
    };

}
