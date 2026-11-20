#include "allocation_probe.hpp"

#include <cstdlib>
#include <new>

namespace search_allocation_probe {
std::size_t calls = 0;
}

// The standard array and nothrow overloads delegate to the replaceable
// global allocation functions. Count real calls around the production search.
void* operator new(std::size_t bytes) {
    ++search_allocation_probe::calls;
    if (void* ptr = std::malloc(bytes == 0 ? 1 : bytes)) return ptr;
    throw std::bad_alloc{};
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
