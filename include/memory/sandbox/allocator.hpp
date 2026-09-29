#pragma once

#include <cstddef>
#include <new>

namespace sandbox {

    /// @brief A budgeted allocator: ordinary allocation with a hard ceiling.
    ///
    /// The point is not speed but containment. A document that asks for more
    /// than #capacity bytes gets std::bad_alloc rather than being allowed to
    /// exhaust the host, which is what makes running an untrusted document
    /// safe to attempt at all.
    ///
    /// Deallocation returns the exact size that was requested, so the running
    /// total stays accurate without a per-block header.
    class Allocator {
    public:
        /// @brief Builds an allocator with a fixed budget.
        /// @param capacity Most bytes that may be outstanding at once.
        explicit Allocator(std::size_t capacity) noexcept;
        ~Allocator() = default;

        Allocator(const Allocator&) = delete("the sandbox has one byte budget, and a second allocator would count against another");
        Allocator& operator=(const Allocator&) = delete("the sandbox has one byte budget, and a second allocator would count against another");
        Allocator(Allocator&&) noexcept = delete("the sandbox has one byte budget, and a second allocator would count against another");
        Allocator& operator=(Allocator&&) noexcept = delete("the sandbox has one byte budget, and a second allocator would count against another");

        /// @brief Allocates, if the budget allows.
        /// @param size Bytes wanted.
        /// @return A block of that size, suitably aligned.
        /// @throws std::bad_alloc when the request would exceed #capacity.
        /// @complexity O(1) plus the underlying allocation.
        [[nodiscard]] void* allocate(std::size_t size);

        /// @brief Returns a block and its size to the budget.
        /// @param pointer Block previously returned by allocate(); null is ignored.
        /// @param size    The exact size that was requested for it.
        /// @complexity O(1).
        void deallocate(void* pointer, std::size_t size) noexcept;

        /// @brief Bytes currently outstanding.
        [[nodiscard]] std::size_t allocated() const noexcept { return used; }

        /// @brief The budget this allocator was built with.
        [[nodiscard]] std::size_t capacity() const noexcept { return limit; }

        /// @brief Bytes still available.
        [[nodiscard]] std::size_t available() const noexcept { return limit - used; }

    private:
        std::size_t limit{0};
        std::size_t used{0};
    };

}