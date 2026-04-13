#pragma once

#include "common.h"
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/mman.h>

namespace mp {

class PageCache {
public:
    static PageCache &get_instance() {
        static PageCache instance;
        return instance;
    }

    /* 分配制定页数的span */
    void *allocate_span(size_t num_pages) {
        std::lock_guard<std::mutex> lock(_mutex);

        auto it = _free_spans.lower_bound(num_pages);
        if (it != _free_spans.end()) {
            Span *span = it->second;
            if (span->next) {
                _free_spans[it->first] = span->next;
            } else {
                _free_spans.erase(it);
            }

            if (span->num_pages > num_pages) {
                Span *new_span = new Span;
                new_span->page_addr = static_cast<char *>(span->page_addr)
                                      + num_pages * PAGE_SIZE;
                new_span->num_pages = span->num_pages - num_pages;
                new_span->next = nullptr;

                auto &list = _free_spans[new_span->num_pages];
                new_span->next = list;
                list = new_span;

                span->num_pages = num_pages;
            }

            _span_map[span->page_addr] = span;
            return span->page_addr;
        }

        void *memory = system_alloc(num_pages);
        if (!memory) {
            return nullptr;
        }
        Span *span = new Span;
        span->page_addr = memory;
        span->num_pages = num_pages;
        span->next = nullptr;
        _span_map[memory] = span;
        return memory;
    }

    /* 释放span */
    void deallocate_span(void *ptr, size_t num_pages) {
        std::lock_guard<std::mutex> lock(_mutex);

        // 1.寻找当前span
        auto it = _span_map.find(ptr);
        if (it == _span_map.end()) {
            return;
        }
        Span *span = it->second;
        void *span_start = ptr;
        size_t merge_pages = num_pages;

        // 2.前向合并
        auto prev_it = _span_map.lower_bound(span_start);
        if (prev_it != _span_map.begin()) {
            --prev_it;
            Span *prev_span = prev_it->second;
            char *prev_end = static_cast<char *>(prev_span->page_addr)
                             + prev_span->num_pages * PAGE_SIZE;
            // 前一个末尾刚好和当前地址相连
            if (prev_end == span_start) {
                bool prev_free = false;
                auto &prev_list = _free_spans[prev_span->num_pages];
                if (prev_list == prev_span) {
                    prev_free = true;
                    prev_list = prev_span->next;
                } else if (prev_list) {
                    Span *cur = prev_list;
                    while (cur->next) {
                        if (cur->next == prev_span) {
                            cur->next = prev_span->next;
                            prev_free = true;
                            break;
                        }
                        cur = cur->next;
                    }
                }

                if (prev_free) {
                    prev_span->num_pages += num_pages;
                    _span_map.erase(it);
                    delete span;

                    span = prev_span;
                    span_start = prev_span->page_addr;
                    merge_pages = prev_span->num_pages;
                }
            }
        }
        // 3.后向合并
        void *next_addr
            = static_cast<char *>(span_start) + merge_pages * PAGE_SIZE;
        auto next_it = _span_map.find(next_addr);
        if (next_it != _span_map.end()) {
            Span *next_span = next_it->second;

            bool next_free = false;
            auto &next_list = _free_spans[next_span->num_pages];
            if (next_list == next_span) {
                next_free = true;
                next_list = next_span->next;
            } else if (next_list) {
                Span *cur = next_list;
                while (cur->next) {
                    if (cur->next == next_span) {
                        cur->next = next_span->next;
                        next_free = true;
                        break;
                    }
                    cur = cur->next;
                }
            }

            if (next_free) {
                span->num_pages += next_span->num_pages;
                _span_map.erase(next_addr);
                delete next_span;
            }
        }
        // 4. 加入空闲链表
        auto &list = _free_spans[span->num_pages];
        span->next = list;
        list = span;
        _span_map[span->page_addr] = span;
    }

private:
    PageCache() = default;
    PageCache(PageCache const &) = delete;
    PageCache &operator=(PageCache const &) = delete;

    /* 向系统申请内存 */
    void *system_alloc(size_t num_pages) {
        size_t size = num_pages * PAGE_SIZE;
        void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return nullptr;
        }

        std::memset(ptr, 0, size);
        return ptr;
    }

private:
    struct Span {
        void *page_addr;
        size_t num_pages;
        Span *next;
    };

    std::map<size_t, Span *> _free_spans;
    std::map<void *, Span *> _span_map;
    std::mutex _mutex;
};

} // namespace mp
