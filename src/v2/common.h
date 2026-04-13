#pragma once
#include <algorithm>
#include <cstddef>

namespace mp {

constexpr size_t ALIGNMENT = 8;
constexpr size_t MAX_BYTES = 256 * 1024; // 256KB
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT;
constexpr size_t THREAD_THRES_HOLD = 64;
constexpr size_t PAGE_SIZE = 4096; // 4k页大小

struct BlockHeader {
    size_t size;
    bool is_use;
    BlockHeader *next;
};

class SizeClass {
public:
    static constexpr size_t round_up(size_t bytes) {
        return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    static constexpr size_t get_index(size_t bytes) {
        bytes = std::max(bytes, ALIGNMENT);
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
};

} // namespace mp
