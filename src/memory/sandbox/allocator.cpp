/// @file
/// @brief Budgeted allocator: ordinary allocation with a hard ceiling.
#include "memory/sandbox/allocator.hpp"

#include <cstddef>
#include <new>

namespace sandbox {

    Allocator::Allocator(const std::size_t capacity) noexcept : limit(capacity) {}

    void* Allocator::allocate(const std::size_t size) {
        if (size == 0) return nullptr;

        // Checked before the addition, so a size near SIZE_MAX cannot wrap
        // past the ceiling and be let through.
        if (size > limit - used) {
            throw std::bad_alloc{};
        }

        void* block = operator new(size);   // throws bad_alloc of its own accord

        used += size;
        return block;
    }

    void Allocator::deallocate(void* pointer, const std::size_t size) noexcept {
        if (pointer == nullptr) return;

        // Plain delete, not the sized form: Clang leaves sized deallocation
        // off by default, so the two-argument overload is not always declared.
        // `size` is still needed, to return the bytes to the budget.
        ::operator delete(pointer);

        // Defensive: a mismatched size would otherwise underflow the counter
        // and make every later allocation look free.
        used -= size < used ? size : used;
    }

}