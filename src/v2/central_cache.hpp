#pragma once

#include "v2/common.h"
#include <array>
#include <atomic>
#include <thread>
#include <v2/PageCache.hpp>

#define private public

namespace mp {

static size_t const SPAN_PAGES = 8;

/**
 * @brief 作为ThreadCache和PageCache的中间层
 *
 */
class CentralCache {
public:
    static CentralCache &get_instance() {
        static CentralCache instance;
        return instance;
    }

    void *fetch_range(size_t index, size_t batch_num) {
        if (index >= FREE_LIST_SIZE || batch_num == 0) {
            return nullptr;
        }

        // 1. 加锁
        while (_locks[index].test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        void *result = nullptr;

        try {
            // 2. 先从中心缓存现有链表拿
            result = _central_free_list[index].load(std::memory_order_relaxed);
            if (!result) {
                // 3. 中心缓存为空，向 PageCache 申请新内存块（一个 Span）
                size_t size = (index + 1) * ALIGNMENT;
                result = fetch_from_page_cache(size);

                // 4. 将这块连续内存切成 size 大小的槽，串成链表
                if (!result) {
                    _locks[index].clear(std::memory_order_release);
                    return nullptr;
                }

                char *start = static_cast<char *>(result);
                size_t total_blocks = (SPAN_PAGES * PAGE_SIZE) / size;
                size_t alloca_blocks = std::min(batch_num, total_blocks);

                /* 5.将前alloca_block个槽返回给局部缓存 */
                if (alloca_blocks > 1) {
                    char *cur = start;
                    for (size_t i = 0; i < alloca_blocks - 1; ++i) {
                        char *next = cur + size;
                        *reinterpret_cast<void **>(cur) = next;
                        cur = next;
                    }
                    *reinterpret_cast<void **>(cur) = nullptr;
                }
                /* 6.剩下的槽留挂在中心缓存 */
                if (total_blocks > alloca_blocks) {
                    void *remain_start = start + alloca_blocks * size;
                    char *cur = static_cast<char *>(remain_start);
                    for (size_t i = alloca_blocks; i < total_blocks - 1; ++i) {
                        char *next = cur + size;
                        *reinterpret_cast<void **>(cur) = next;
                        cur = next;
                    }
                    *reinterpret_cast<void **>(cur) = nullptr;
                    _central_free_list[index].store(
                        remain_start, std::memory_order_release);
                }
            } else {
                // 7.中心缓存非空，直接摘取第一个节点
                void *current = result;
                void *prev = nullptr;
                size_t count = 0;
                while (current && count < batch_num) {
                    prev = current;
                    current = *reinterpret_cast<void **>(current);
                    count++;
                }
                batch_num = count;
                if (prev) {
                    *reinterpret_cast<void **>(prev) = nullptr;
                }
                _central_free_list[index].store(
                    current, std::memory_order_release);
            }
        } catch (...) {
            _locks[index].clear(std::memory_order_release);
            throw;
        }
        _locks[index].clear(std::memory_order_release);
        // 返回一个槽位给调用者
        return result;
    }

    void return_range(void *start, size_t size, size_t index) {
        if (!start || index >= FREE_LIST_SIZE) {
            return;
        }

        while (_locks[index].test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        try {
            void *end = start;
            size_t count = 1;
            void *p = nullptr;
            while ((p = *reinterpret_cast<void **>(end)) && count < size) {
                end = p;
                count++;
            }
            *reinterpret_cast<void **>(end)
                = _central_free_list[index].load(std::memory_order_acquire);
            _central_free_list[index].store(start, std::memory_order_release);

        } catch (...) {
            _locks[index].clear(std::memory_order_release);
            throw;
        }
        _locks[index].clear(std::memory_order_release);
    }

private:
    CentralCache() {
        for (auto &ptr: _central_free_list) {
            ptr.store(nullptr, std::memory_order_relaxed);
        }
        for (auto &lock: _locks) {
            lock.clear();
        }
    }

    /* 从页缓存获取内存以span为单位 */
    void *fetch_from_page_cache(size_t size) {
        // 计算拉取多少页
        size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        if (size <= SPAN_PAGES * PAGE_SIZE) {
            return PageCache::get_instance().allocate_span(SPAN_PAGES);
        } else {
            return PageCache::get_instance().allocate_span(num_pages);
        }
    }

private:
    std::array<std::atomic<void *>, FREE_LIST_SIZE>
        _central_free_list;                              // 中心缓存的自由链表
    std::array<std::atomic_flag, FREE_LIST_SIZE> _locks; // 用于同步的自旋锁
};

} // namespace mp
