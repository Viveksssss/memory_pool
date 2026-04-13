#pragma once

#include <cstdint>
#include <mutex>
#include <new>

namespace mp {

static constexpr int16_t MEMORY_POOL_NUM = 64;
static constexpr int16_t SLOT_BASE_SIZE = 8;
static constexpr int16_t MAX_SLOT_SIZE = 512;

struct Slot {
    Slot *next;
};

class MemoryPool {
public:
    MemoryPool(size_t block_size = 64 * 1024); // 默认 64KB
    ~MemoryPool();

    void init(size_t size);
    void *allocate();
    void deallocate(void *ptr);

private:
    void allocate_new_block();
    size_t pad_pointer(char *p, size_t align);

private:
    size_t _block_size; // 内存块大小
    size_t _slot_size;  // 槽大小
    Slot *_first_block; // 块链表头（用于析构时释放所有块）
    Slot *_free_list;   // 空闲槽链表头
    char *_cur_pos;     // 当前未分配内存的起始位置
    char *_end_pos;     // 当前内存块的末尾位置
    std::mutex _mutex_of_freelist;
    std::mutex _mutex_of_block;
};

class HashBucket {
public:
    static void init();
    static MemoryPool &get_memory_pool(int index);

    static void *use_memory(size_t size) {
        if (size <= 0) {
            return nullptr;
        }
        if (size > MAX_SLOT_SIZE) {
            return ::operator new(size);
        }
        return get_memory_pool(((size + 7) / SLOT_BASE_SIZE) - 1).allocate();
    }

    static void free_memory(void *ptr, size_t size) {
        if (!ptr) {
            return;
        }
        if (size > MAX_SLOT_SIZE) {
            ::operator delete(ptr);
            return;
        }
        get_memory_pool(((size + 7) / SLOT_BASE_SIZE) - 1).deallocate(ptr);
    }

    template <typename T, typename... Args>
    friend T *new_element(Args &&...args);

    template <typename T>
    friend void delete_element(T *p);
};

template <typename T, typename... Args>
inline T *new_element(Args &&...args) {
    // 编译期确定池子索引
    constexpr size_t size = sizeof(T);
    constexpr int index = ((size + 7) / SLOT_BASE_SIZE) - 1;

    // 直接获取池子（static 确保只初始化一次）
    static auto &pool = HashBucket::get_memory_pool(index);

    T *p = reinterpret_cast<T *>(pool.allocate());
    if (p) {
        new (p) T(std::forward<Args>(args)...);
    }
    return p;
}

template <typename T>
inline void delete_element(T *p) {
    if (p) {
        constexpr size_t size = sizeof(T);
        constexpr int index = ((size + 7) / SLOT_BASE_SIZE) - 1;
        static auto &pool = HashBucket::get_memory_pool(index);

        p->~T();
        pool.deallocate(reinterpret_cast<void *>(p));
    }
}

// template <typename T, typename... Args>
// inline T *new_element(Args &&...args) {
//     T *p = reinterpret_cast<T *>(HashBucket::use_memory(sizeof(T)));
//     if (p) {
//         new (p) T(std::forward<Args>(args)...);
//     }
//     return p;
// }

// template <typename T>
// inline void delete_element(T *p) {
//     if (p) {
//         p->~T();
//         HashBucket::free_memory(reinterpret_cast<void *>(p), sizeof(T));
//     }
// }

} // namespace mp
