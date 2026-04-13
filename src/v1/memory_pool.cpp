#include <cassert>
#include <mutex>
#include <new>
#include <v1/memory_pool.h>

namespace mp {

MemoryPool::MemoryPool(size_t block_size)
    : _block_size(block_size)
    , _slot_size(0)
    , _first_block(nullptr)
    , _free_list(nullptr)
    , _cur_pos(nullptr)
    , _end_pos(nullptr) { }

MemoryPool::~MemoryPool() {
    Slot *cur = _first_block;
    while (cur) {
        Slot *next = cur->next;
        ::operator delete(cur);
        cur = next;
    }
}

void MemoryPool::init(size_t size) {
    assert(size > 0);
    _slot_size = size;
    _first_block = nullptr;
    _cur_pos = nullptr;
    _free_list = nullptr;
    _end_pos = nullptr;
}

void *MemoryPool::allocate() {
    // 快速路径：从空闲链表取
    if (_free_list != nullptr) {
        // 可选：加锁（多线程时取消注释）
        // std::lock_guard<std::mutex> lock(_mutex_of_freelist);
        if (_free_list != nullptr) {
            Slot *temp = _free_list;
            _free_list = temp->next;
            return temp;
        }
    }

    // 慢速路径：从连续内存切
    {
        // 可选：加锁（多线程时取消注释）
        // std::lock_guard<std::mutex> lock(_mutex_of_block);

        // 如果当前块用完了，申请新块
        if (_cur_pos >= _end_pos) {
            allocate_new_block();
        }

        void *result = _cur_pos;
        _cur_pos += _slot_size;
        return result;
    }
}

void MemoryPool::deallocate(void *ptr) {
    if (!ptr) {
        return;
    }

    // 可选：加锁（多线程时取消注释）
    // std::lock_guard<std::mutex> lock(_mutex_of_freelist);

    Slot *slot = reinterpret_cast<Slot *>(ptr);
    slot->next = _free_list;
    _free_list = slot;
}

void MemoryPool::allocate_new_block() {
    void *new_block = ::operator new(_block_size);

    // 将新块插入块链表头部（用于析构时统一释放）
    reinterpret_cast<Slot *>(new_block)->next = _first_block;
    _first_block = reinterpret_cast<Slot *>(new_block);

    // 跳过块头部的 next 指针
    char *body = reinterpret_cast<char *>(new_block) + sizeof(Slot *);

    // 对齐校正
    size_t padding_size = pad_pointer(body, _slot_size);

    // 设置当前可用位置和末尾位置
    _cur_pos = body + padding_size;
    _end_pos = reinterpret_cast<char *>(new_block) + _block_size;

    // 新块没有碎片，清空碎片指针
    _free_list = nullptr;
}

size_t MemoryPool::pad_pointer(char *p, size_t align) {
    size_t addr = reinterpret_cast<size_t>(p);
    size_t misalign = addr % align;
    return misalign == 0 ? 0 : align - misalign;
}

// ============== HashBucket 实现 ==============

void HashBucket::init() {
    for (int i = 0; i < MEMORY_POOL_NUM; ++i) {
        get_memory_pool(i).init((i + 1) * SLOT_BASE_SIZE);
    }
}

MemoryPool &HashBucket::get_memory_pool(int index) {
    static MemoryPool memory_pool[MEMORY_POOL_NUM];
    return memory_pool[index];
}

} // namespace mp
