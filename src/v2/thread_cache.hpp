#pragma once

#include "common.h"
#include "v2/central_cache.hpp"
#include <array>

namespace mp {

class ThreadCache {
public:
    static ThreadCache *get_instance() {
        static thread_local ThreadCache instance;
        return &instance;
    }

    void *allocate(size_t size) {
        if (size == 0) {
            size = ALIGNMENT; // 至少一个对齐大小
        }
        if (size > MAX_BYTES) {
            return malloc(size);
        }

        size_t index = SizeClass::get_index(size);
        // _free_list_size[index]--;
        if (void *ptr = _free_list[index]) {
            /*
                Step 1: reinterpret_cast<void **>(ptr)
                ptr 是一个 void*，指向当前链表头部的那块空闲内存。

                把它强制转换成 void**，意思是 "请把这块内存的前 8
               字节（64位系统）当作一个 void* 指针来读"。

                Step 2: *reinterpret_cast<void **>(ptr)
                解引用 void**，读取出那块内存前 8 字节存储的值。

                这个值正是该空闲槽位的 next 指针（即链表的下一个节点）。

                Step 3: 赋值给 _free_list[index]
                把读出的 next 指针设为新的链表头。

                相当于 head = head->next。

                _free_list[index] = 0x1000
                                     │
                                     ▼
                            ┌─────────────────┐
                    0x1000  │ next: 0x2000    │  ← 前8字节存的是下一个节点的地址
                            ├─────────────────┤
                            │ 剩余空闲空间     │
                            └─────────────────┘
                                     │
                                     ▼
                            ┌─────────────────┐
                    0x2000  │ next: nullptr   │
                            ├─────────────────┤
                            │ 剩余空闲空间     │
                            └─────────────────┘
            */
            _free_list[index] = *reinterpret_cast<void **>(ptr);
            _free_list_size[index]--;
            return ptr;
        }

        return fetch_from_central(index);
    }

    void deallocate(void *ptr, size_t size) {
        if (size > MAX_BYTES) {
            free(ptr);
            return;
        }

        size_t index = SizeClass::get_index(size);

        *reinterpret_cast<void **>(ptr) = _free_list[index];
        _free_list[index] = ptr;

        _free_list_size[index]++;

        if (should_return_to_central(index)) {
            return_to_central(_free_list[index], size);
        }
    }

private:
    ThreadCache() {
        _free_list.fill(nullptr);
        _free_list_size.fill(0);
    }

    size_t get_batch_num(size_t size) {
        // 每次批量获取不超过4KB
        constexpr size_t MAX_BATCH_SIZE = 4 * 1024; // 4KB

        size_t base_num;
        if (size <= 32) {
            base_num = 64;
        } else if (size <= 64) {
            base_num = 32;
        } else if (size <= 256) {
            base_num = 8;
        } else if (size <= 512) {
            base_num = 4;
        } else if (size <= 1024) {
            base_num = 2;
        } else {
            base_num = 1;
        }
        size_t max_num = std::max<size_t>(1, MAX_BATCH_SIZE / size);
        return std::min(max_num, base_num);
    }

    void *fetch_from_central(size_t index) {
        size_t size = (index + 1) * ALIGNMENT;
        size_t batch_num = get_batch_num(size);
        void *start
            = CentralCache::get_instance().fetch_range(index, batch_num);
        if (!start) {
            return nullptr;
        }
        _free_list_size[index] += batch_num - 1;
        void *result = start;
        if (batch_num > 1) {
            _free_list[index] = *reinterpret_cast<void **>(start);
        }
        return result;
    }

    void return_to_central(void *start, size_t size) {
        size_t index = SizeClass::get_index(size);
        size_t aligned_size = SizeClass::round_up(size);

        size_t batch_num = _free_list_size[index];
        if (batch_num <= 1) {
            return;
        }

        size_t keep_num = batch_num / 2;
        if (keep_num < 1) {
            keep_num = 1;
        }
        size_t return_num = batch_num - keep_num;

        char *current = static_cast<char *>(start);
        char *split_node = current;
        for (size_t i = 0; i < keep_num - 1; ++i) {
            split_node = reinterpret_cast<char *>(
                *reinterpret_cast<void **>(split_node));
            if (split_node == nullptr) {
                return_num = batch_num - (i + 1);
                break;
            }
        }
        if (split_node) {
            void *next = *reinterpret_cast<void **>(split_node);
            *reinterpret_cast<void **>(split_node) = nullptr;

            _free_list[index] = start;
            _free_list_size[index] = keep_num;

            if (return_num > 0 && next != nullptr) {
                CentralCache::get_instance().return_range(
                    next, return_num * aligned_size, index);
            }
        }
    }

    bool should_return_to_central(size_t index) {
        return _free_list_size[index] > THREAD_THRES_HOLD;
    }

private:
    std::array<void *, FREE_LIST_SIZE> _free_list; // 空闲槽链表头指针数组
    std::array<size_t, FREE_LIST_SIZE>
        _free_list_size;                           // 每个链表当前缓存的槽位数

    // clang-format off
    /*
        假设场景,当前最小内存块大小为64字节
        空闲时前8字节用于next指针
        分配后,全部64用于用户分配

        1.空闲:
            _free_list ──→ ┌─────────────────────────────┐
                           │ 前8字节: next = 0x2000      │  ← 当作指针用
                           ├─────────────────────────────┤
                           │ 后56字节: 空闲 (无意义数据)  │
                           └─────────────────────────────┘
        2.分配:
            取下:
                void* ptr = _free_list;                     // ptr = 0x1000
                _free_list = *reinterpret_cast<void**>(ptr); // 读出 next，更新链表头
                return ptr;                                  // 返回 0x1000 给用户
            写入:
                char* data = static_cast<char*>(ptr);
                for (int i = 0; i < 64; ++i) data[i] = 'A';  // 全部覆盖！

                0x1000 ┌─────────────────────────────┐
                       │ 'A' 'A' 'A' ... (64个'A')   │  ← 前8字节的 next 指针已被彻底覆盖
                       └─────────────────────────────┘
        3.释放:
            void* ptr = ...; // 0x1000
            *reinterpret_cast<void**>(ptr) = _free_list;  // 把当前链表头写入前8字节
            _free_list = ptr;                             // 这块内存成为新链表头

            _free_list ──→ ┌─────────────────────────────┐
                           │ 前8字节: next = 旧链表头     │  ← 用户数据被覆盖！
                           ├─────────────────────────────┤
                           │ 后56字节: 残留的用户数据     │  ← 不管了
                           └─────────────────────────────┘
        效果:
            没有额外的控制块：不需要在旁边另建一个 struct { void* next; void* data; }。
            内存利用率 100%：用户实际可用空间 = 分配出去的块大小。
            时间开销极小：分配 = 读一个指针；释放 = 写一个指针。    
    */
    // clang-format on
};

} // namespace mp
