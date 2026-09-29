#include "allocator.hpp"

#include <cassert>

// The sandbox's allocator holds a run to a ceiling: it hands out memory until
// the ceiling, refuses past it, and counts back what is returned.

int main() {
    sandbox::Allocator allocator(1024);
    assert((allocator.capacity() == 1024 && allocator.available() == 1024) && "a fresh allocator has it all to give");

    void* block = allocator.allocate(256);
    assert((block != nullptr) && "within the ceiling, memory is given");
    assert((allocator.allocated() == 256 && allocator.available() == 768) && "and counted");

    void* refused = nullptr;
    try {
        refused = allocator.allocate(4096);
    } catch (...) {
        refused = nullptr;
    }
    assert((refused == nullptr) && "past the ceiling, it is refused");
    assert((allocator.allocated() == 256) && "and a refusal costs nothing");

    allocator.deallocate(block, 256);
    assert((allocator.allocated() == 0 && allocator.available() == 1024) && "returned memory is counted back");

    return 0;
}
