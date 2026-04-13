#pragma once

#include "v1/memory_pool.h"
#include "v2/central_cache.hpp"
#include "v2/thread_cache.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>
inline uint64_t volatile sink;
constexpr int ITERATIONS = 5'000'000;
using namespace mp;

// ========== 情形1：直接使用原始内存池（无模板） ==========
inline void test_raw_pool() {
    std::cout << "=== 情形1：直接使用 MemoryPool（无模板封装） ==="
              << std::endl;

    // 获取8字节槽的池子
    mp::MemoryPool &pool = mp::HashBucket::get_memory_pool(0);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        void *p = pool.allocate();
        *reinterpret_cast<uint64_t *>(p) = 20;
        sink = *reinterpret_cast<uint64_t *>(p);
        pool.deallocate(p);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto raw_pool_time
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::cout << "raw pool:    " << raw_pool_time << "ms\n";
}

// ========== 情形2：使用 HashBucket 封装（运行时计算索引） ==========
inline void test_hashbucket_runtime() {
    std::cout << "=== 情形2：HashBucket 封装（运行时计算索引） ==="
              << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        void *p = mp::HashBucket::use_memory(sizeof(uint64_t));
        *reinterpret_cast<uint64_t *>(p) = 20;
        sink = *reinterpret_cast<uint64_t *>(p);
        mp::HashBucket::free_memory(p, sizeof(uint64_t));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto hashbucket_time
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::cout << "HashBucket:  " << hashbucket_time << "ms\n";
}

// ========== 情形3：模板封装（编译期计算索引） ==========
inline void test_template_compiletime() {
    std::cout << "=== 情形3：模板封装（编译期计算索引） ===" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        auto ps = mp::new_element<uint64_t>(20);
        sink = *ps;
        mp::delete_element(ps);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto template_time
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::cout << "template:    " << template_time << "ms\n";
}

// ========== 情形4：标准 new/delete（基准） ==========
inline void test_new_delete() {
    std::cout << "=== 情形4：标准 new/delete（基准） ===" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        auto ps = new uint64_t(20);
        sink = *ps;
        delete ps;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto new_delete_time
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::cout << "new/delete:  " << new_delete_time << "ms\n";
}

// ========== 详细性能分析（分配和释放分开测） ==========
inline void test_detailed_analysis() {
    std::cout << "\n========== 详细性能分析 ==========\n" << std::endl;

    mp::MemoryPool &pool = mp::HashBucket::get_memory_pool(0);

    // 1. 纯分配测试
    std::cout << "--- 纯分配（不释放） ---" << std::endl;

    std::vector<void *> pool_ptrs;
    pool_ptrs.reserve(ITERATIONS);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        pool_ptrs.push_back(pool.allocate());
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto pool_alloc
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::vector<uint64_t *> new_ptrs;
    new_ptrs.reserve(ITERATIONS);

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        new_ptrs.push_back(new uint64_t(20));
    }
    end = std::chrono::high_resolution_clock::now();
    auto new_alloc
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::cout << "  memory_pool: " << pool_alloc << "ms\n";
    std::cout << "  new:         " << new_alloc << "ms\n";

    // 2. 纯释放测试
    std::cout << "\n--- 纯释放（已分配完成） ---" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    for (auto p: pool_ptrs) {
        pool.deallocate(p);
    }
    end = std::chrono::high_resolution_clock::now();
    auto pool_free
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    start = std::chrono::high_resolution_clock::now();
    for (auto p: new_ptrs) {
        delete p;
    }
    end = std::chrono::high_resolution_clock::now();
    auto new_free
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::cout << "  memory_pool: " << pool_free << "ms\n";
    std::cout << "  delete:      " << new_free << "ms\n";

    // 3. 总体统计
    std::cout << "\n--- 总体（分配+释放） ---" << std::endl;
    std::cout << "  memory_pool: " << pool_alloc + pool_free << "ms\n";
    std::cout << "  new/delete:  " << new_alloc + new_free << "ms\n";
}

// ========== 不同大小的分配测试 ==========
template <typename T>
inline void test_different_sizes(char const *type_name) {
    std::cout << "\n--- 分配 " << type_name << " (size=" << sizeof(T) << ") ---"
              << std::endl;

    // 模板版本
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS / 10; ++i) { // 减少迭代次数避免太久
        auto p = mp::new_element<T>();
        sink = reinterpret_cast<uint64_t>(p);
        mp::delete_element(p);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto template_time
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    // new/delete 版本
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS / 10; ++i) {
        auto p = new T();
        sink = reinterpret_cast<uint64_t>(p);
        delete p;
    }
    end = std::chrono::high_resolution_clock::now();
    auto new_time
        = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();

    std::cout << "  template: " << template_time << "ms\n";
    std::cout << "  new:      " << new_time << "ms\n";
    std::cout << "  ratio:    " << static_cast<double>(new_time) / template_time
              << "x\n";
}

// 测试用的结构体
struct SmallStruct {
    int a;
    double b;
};

struct MediumStruct {
    int a[10];
    double b[5];
    char c[32];
};

struct LargeStruct {
    int a[100];
    double b[50];
    char c[128];
};

inline void test_v1() {
    std::cout << "迭代次数: " << ITERATIONS << "\n" << std::endl;

    // 初始化内存池
    mp::HashBucket::init();

    // 预热（避免首次分配的开销影响测试）
    for (int i = 0; i < 1000; ++i) {
        auto p = mp::new_element<uint64_t>(20);
        sink = *p;
        mp::delete_element(p);
    }
    for (int i = 0; i < 1000; ++i) {
        auto p = new uint64_t(20);
        sink = *p;
        delete p;
    }

    std::cout << "========== 三种情形对比 ==========\n" << std::endl;

    // 运行主要测试
    test_new_delete();
    test_hashbucket_runtime();
    test_template_compiletime();
    test_raw_pool();

    // 详细分析
    test_detailed_analysis();

    // 不同大小的测试
    std::cout << "\n========== 不同大小类型测试 ==========" << std::endl;
    test_different_sizes<uint32_t>("uint32_t");
    test_different_sizes<uint64_t>("uint64_t");
    test_different_sizes<SmallStruct>("SmallStruct");
    test_different_sizes<MediumStruct>("MediumStruct");

    std::cout << "\nsink: " << sink << std::endl; // 防止优化
}

inline void fill_memory(void *ptr, size_t size, unsigned char pattern) {
    std::memset(ptr, pattern, size);
}

inline bool check_memory(void *ptr, size_t size, unsigned char pattern) {
    unsigned char *p = static_cast<unsigned char *>(ptr);
    for (size_t i = 0; i < size; ++i) {
        if (p[i] != pattern) {
            return false;
        }
    }
    return true;
}

// 单线程基本分配释放测试
inline void test_basic_alloc_free() {
    std::cout << "[Test] Basic allocation and deallocation...\n";
    ThreadCache *tc = ThreadCache::get_instance();

    // 分配不同大小的内存
    std::vector<void *> ptrs;
    for (size_t size = 8; size <= 256; size += 8) {
        void *p = tc->allocate(size);
        assert(p != nullptr);
        fill_memory(p, size, 0xAB);
        ptrs.push_back(p);
    }

    // 检查并释放
    size_t idx = 0;
    for (size_t size = 8; size <= 256; size += 8) {
        void *p = ptrs[idx++];
        assert(check_memory(p, size, 0xAB));
        tc->deallocate(p, size);
    }

    // 再次分配应该能重用刚刚释放的内存
    void *p2 = tc->allocate(16);
    assert(p2 != nullptr);
    tc->deallocate(p2, 16);

    std::cout << "  -> PASS\n";
}

// 测试批量分配（触发 CentralCache 和 PageCache）
inline void test_batch_alloc() {
    std::cout << "[Test] Batch allocation (force central/page cache)...\n";
    ThreadCache *tc = ThreadCache::get_instance();

    // 分配大量小块，迫使 ThreadCache 从 CentralCache 取多批
    int const N = 1000;
    std::vector<void *> ptrs;
    for (int i = 0; i < N; ++i) {
        void *p = tc->allocate(32);
        assert(p != nullptr);
        fill_memory(p, 32, static_cast<unsigned char>(i & 0xFF));
        ptrs.push_back(p);
    }

    // 验证并释放
    for (int i = 0; i < N; ++i) {
        assert(check_memory(ptrs[i], 32, static_cast<unsigned char>(i & 0xFF)));
        tc->deallocate(ptrs[i], 32);
    }
    std::cout << "  -> PASS\n";
}

// 测试大块内存（超过 MAX_BYTES 应直接 malloc/free）
inline void test_large_alloc() {
    std::cout << "[Test] Large allocation (>256KB)...\n";
    ThreadCache *tc = ThreadCache::get_instance();
    size_t large = 512 * 1024; // 512KB
    void *p = tc->allocate(large);
    assert(p != nullptr);
    fill_memory(p, large, 0xCC);
    assert(check_memory(p, large, 0xCC));
    tc->deallocate(p, large);
    std::cout << "  -> PASS\n";
}

// 测试多线程并发分配释放
inline void test_multithreading() {
    std::cout << "[Test] Multi-threaded allocation/deallocation...\n";
    int const THREAD_COUNT = 4;
    int const ALLOCS_PER_THREAD = 10000;

    auto worker = [](int tid) {
        ThreadCache *tc = ThreadCache::get_instance();
        std::vector<void *> my_ptrs;
        my_ptrs.reserve(ALLOCS_PER_THREAD);
        for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
            // 随机大小 8~256 字节
            size_t size = 8 + (rand() % 32) * 8;
            void *p = tc->allocate(size);
            if (!p) {
                std::cerr << "Thread " << tid << " allocation failed\n";
                return false;
            }
            fill_memory(p, size, static_cast<unsigned char>(tid));
            my_ptrs.push_back(p);
        }
        // 验证并释放
        for (void *p: my_ptrs) {
            // 需要知道大小？这里简化：我们只知道 p
            // 的大小在分配时没有记录，无法直接验证。
            // 所以这里只释放，不验证内容（因为大小丢失）。实际使用中需要用户自己记录大小。
            // 为简单，我们只做释放，验证放在另一个测试中。
            // 但为了检查内存池稳定性，只做释放。
            // 注意：代码中 deallocate
            // 需要大小，但我们没保存，所以需要改进测试。
            // 为了测试正确性，我们改为保存大小。
        }
        // 改进：保存大小对
        return true;
    };

    // 更完善的多线程测试：保存 (ptr, size) 对
    std::vector<std::thread> threads;
    std::atomic<bool> success{true};

    for (int t = 0; t < THREAD_COUNT; ++t) {
        threads.emplace_back([&success, t]() {
            ThreadCache *tc = ThreadCache::get_instance();
            std::vector<std::pair<void *, size_t>> allocs;
            allocs.reserve(ALLOCS_PER_THREAD);
            for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
                size_t size = 8 + (rand() % 32) * 8;
                void *p = tc->allocate(size);
                if (!p) {
                    success = false;
                    return;
                }
                fill_memory(p, size, static_cast<unsigned char>(t));
                allocs.emplace_back(p, size);
            }
            // 验证内存并释放
            for (auto &[p, sz]: allocs) {
                if (!check_memory(p, sz, static_cast<unsigned char>(t))) {
                    success = false;
                    return;
                }
                tc->deallocate(p, sz);
            }
        });
    }

    for (auto &th: threads) {
        th.join();
    }
    assert(success);
    std::cout << "  -> PASS\n";
}

// 压力测试：大量分配释放，观察是否崩溃或死锁
inline void test_stress() {
    std::cout << "[Test] Stress test (allocate/free many objects)...\n";
    int const ITER = 50000;
    ThreadCache *tc = ThreadCache::get_instance();
    for (int i = 0; i < ITER; ++i) {
        size_t size = 8 + (rand() % 64) * 8; // 8~512字节
        void *p = tc->allocate(size);
        if (!p) {
            std::cerr << "Stress allocation failed at iteration " << i << "\n";
            assert(false);
        }
        // 随便写点数据
        memset(p, 0x55, size);
        tc->deallocate(p, size);
    }
    std::cout << "  -> PASS\n";
}

// 工具函数：打印以 head 开头的链表（最多打印 20 个节点）
inline void print_list(char const *name, void *head, size_t block_size = 0) {
    std::cout << name << ": ";
    if (!head) {
        std::cout << "(empty)\n";
        return;
    }
    void *cur = head;
    int count = 0;
    while (cur && count < 20) {
        std::cout << cur;
        if (block_size) {
            std::cout << "[" << block_size << "B]";
        }
        cur = *reinterpret_cast<void **>(cur);
        if (cur) {
            std::cout << " -> ";
        }
        ++count;
    }
    if (cur) {
        std::cout << " ... (truncated)";
    }
    std::cout << "\n";
}

inline void test_address() {
    std::cout << "========== Memory Pool Detailed Flow Test ==========\n";
    ThreadCache *tc = ThreadCache::get_instance();
    CentralCache &cc = CentralCache::get_instance();

    // 辅助 lambda：打印 ThreadCache 状态
    auto dump_tc = [&]() {
        std::cout << "\n--- ThreadCache State ---\n";
        for (size_t i = 0; i < FREE_LIST_SIZE; ++i) {
            if (tc->_free_list[i] != nullptr) {
                size_t sz = (i + 1) * ALIGNMENT;
                std::cout << "  index " << i << " (size " << sz << "B): ";
                print_list("", tc->_free_list[i]);
                std::cout << "    count = " << tc->_free_list_size[i] << "\n";
            }
        }
    };

    // 辅助 lambda：打印 CentralCache 状态（需要加锁）
    auto dump_cc = [&]() {
        std::cout << "\n--- CentralCache State ---\n";
        for (size_t i = 0; i < FREE_LIST_SIZE; ++i) {
            // 加锁读取
            while (cc._locks[i].test_and_set(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            void *head
                = cc._central_free_list[i].load(std::memory_order_relaxed);
            if (head != nullptr) {
                size_t sz = (i + 1) * ALIGNMENT;
                std::cout << "  index " << i << " (size " << sz << "B): ";
                print_list("", head);
            }
            cc._locks[i].clear(std::memory_order_release);
        }
    };

    // 1. 第一次分配 64 字节（index = 7）
    std::cout << "\n=== Step 1: Allocate 64 bytes ===\n";
    void *p1 = tc->allocate(64);
    std::cout << "Allocated ptr: " << p1 << " (64 bytes)\n";
    dump_tc();
    dump_cc();

    // 2. 第二次分配 64 字节（应该从 ThreadCache 本地链表取）
    std::cout << "\n=== Step 2: Allocate another 64 bytes ===\n";
    void *p2 = tc->allocate(64);
    std::cout << "Allocated ptr: " << p2 << " (64 bytes)\n";
    dump_tc();
    dump_cc();

    // 3. 分配 128 字节（不同大小类）
    std::cout << "\n=== Step 3: Allocate 128 bytes ===\n";
    void *p3 = tc->allocate(128);
    std::cout << "Allocated ptr: " << p3 << " (128 bytes)\n";
    dump_tc();
    dump_cc();

    // 4. 释放 p1
    std::cout << "\n=== Step 4: Deallocate p1 (64 bytes) ===\n";
    tc->deallocate(p1, 64);
    std::cout << "Deallocated " << p1 << "\n";
    dump_tc();

    // 5. 再次分配 64 字节，应该重用刚刚释放的 p1
    std::cout
        << "\n=== Step 5: Allocate 64 bytes again (should reuse p1) ===\n";
    void *p4 = tc->allocate(64);
    std::cout << "Allocated ptr: " << p4 << " (64 bytes) -> ";
    if (p4 == p1) {
        std::cout << "REUSED p1!\n";
    } else {
        std::cout << "different address\n";
    }
    dump_tc();

    // 6. 大量分配 64 字节，耗尽 ThreadCache 本地，迫使从 CentralCache 批量获取
    std::cout << "\n=== Step 6: Allocate many 64-byte blocks to exhaust local "
                 "cache ===\n";
    std::vector<void *> many;
    // 64 字节的 batch_num 通常是 32，我们分配 40 次确保跨越边界
    for (int i = 0; i < 40; ++i) {
        void *ptr = tc->allocate(64);
        many.push_back(ptr);
    }
    std::cout << "Allocated 40 extra blocks. Last allocated: " << many.back()
              << "\n";
    dump_tc();
    dump_cc();

    // 7. 释放所有额外块，观察 ThreadCache 本地链表是否超过阈值 64 并触发归还到
    // CentralCache
    std::cout << "\n=== Step 7: Deallocate many blocks, trigger return to "
                 "CentralCache ===\n";
    for (void *ptr: many) {
        tc->deallocate(ptr, 64);
    }
    std::cout << "Deallocated all extra blocks.\n";
    dump_tc();
    dump_cc();

    // 8. 释放剩余 p2, p3, p4
    std::cout << "\n=== Step 8: Deallocate remaining blocks ===\n";
    tc->deallocate(p2, 64);
    tc->deallocate(p3, 128);
    tc->deallocate(p4, 64);
    std::cout << "All local blocks returned.\n";
    dump_tc();
    dump_cc();

    // 9. 分配大块内存（超过 MAX_BYTES），应直接走 malloc/free
    std::cout << "\n=== Step 9: Large allocation (>256KB) ===\n";
    void *large = tc->allocate(512 * 1024);
    std::cout << "Allocated large ptr: " << large << " (512KB)\n";
    tc->deallocate(large, 512 * 1024);
    std::cout << "Deallocated large ptr.\n";

    std::cout << "\n========== Test Finished ==========\n";
}

inline void test_v2() {
    srand(static_cast<unsigned>(time(nullptr)));

    std::cout << "=== Memory Pool Test Suite ===\n";
    test_basic_alloc_free();
    test_batch_alloc();
    test_large_alloc();
    test_multithreading();
    test_stress();
    test_address();

    std::cout << "All tests passed!\n";
}

using namespace mp;

// 测试配置
constexpr int ALLOC_COUNT = 500000; // 总分配次数
constexpr int THREAD_COUNT = 4;     // 多线程时的线程数
constexpr size_t MIN_SIZE = 8;
constexpr size_t MAX_SIZE = 512;

// 随机大小生成器
static std::mt19937 rng(std::random_device{}());
static std::uniform_int_distribution<size_t> size_dist(MIN_SIZE, MAX_SIZE);

// 对齐到 8 字节（模拟内存池内部对齐）
inline size_t align_up(size_t size) {
    return (size + 7) & ~7;
}

// ------------------- 内存池测试（单线程） -------------------
inline void test_mempool_single() {
    ThreadCache *tc = ThreadCache::get_instance();
    std::vector<std::pair<void *, size_t>> allocations;
    allocations.reserve(ALLOC_COUNT);

    for (int i = 0; i < ALLOC_COUNT; ++i) {
        size_t size = size_dist(rng);
        void *ptr = tc->allocate(size);
        if (!ptr) {
            std::cerr << "Memory pool allocation failed\n";
            return;
        }
        memset(ptr, 0xAA, size);
        allocations.emplace_back(ptr, size);
    }

    for (auto &[ptr, sz]: allocations) {
        tc->deallocate(ptr, sz);
    }
}

// ------------------- new/delete 测试（单线程） -------------------
inline void test_new_single() {
    std::vector<std::pair<void *, size_t>> allocations;
    allocations.reserve(ALLOC_COUNT);

    for (int i = 0; i < ALLOC_COUNT; ++i) {
        size_t size = align_up(size_dist(rng));
        void *ptr = ::operator new(size);
        memset(ptr, 0xAA, size);
        allocations.emplace_back(ptr, size);
    }

    for (auto &[ptr, sz]: allocations) {
        ::operator delete(ptr);
    }
}

// ------------------- 多线程工作函数 -------------------
inline void worker_mempool(int thread_id, int ops_per_thread) {
    ThreadCache *tc = ThreadCache::get_instance();
    std::vector<std::pair<void *, size_t>> allocations;
    allocations.reserve(ops_per_thread);
    for (int i = 0; i < ops_per_thread; ++i) {
        size_t size = size_dist(rng);
        void *ptr = tc->allocate(size);
        memset(ptr, 0xAA, size);
        allocations.emplace_back(ptr, size);
    }
    for (auto &[ptr, sz]: allocations) {
        tc->deallocate(ptr, sz);
    }
}

inline void worker_new(int thread_id, int ops_per_thread) {
    std::vector<std::pair<void *, size_t>> allocations;
    allocations.reserve(ops_per_thread);
    for (int i = 0; i < ops_per_thread; ++i) {
        size_t size = align_up(size_dist(rng));
        void *ptr = ::operator new(size);
        memset(ptr, 0xAA, size);
        allocations.emplace_back(ptr, size);
    }
    for (auto &[ptr, sz]: allocations) {
        ::operator delete(ptr);
    }
}

// ------------------- 计时工具 -------------------
template <typename Func>
double measure_time(Func &&func, int repeat = 3) {
    double total = 0.0;
    for (int r = 0; r < repeat; ++r) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        total += std::chrono::duration<double, std::milli>(end - start).count();
    }
    return total / repeat;
}

inline void test_v22() {
    std::cout << "========== Memory Pool vs new/delete Benchmark ==========\n";
    std::cout << "Operations per test: " << ALLOC_COUNT << "\n";
    std::cout << "Random size range: " << MIN_SIZE << " ~ " << MAX_SIZE
              << " bytes\n\n";

    // 单线程测试
    std::cout << "--- Single Thread ---\n";
    double time_mp = measure_time(test_mempool_single);
    double time_new = measure_time(test_new_single);
    std::cout << "Memory pool: " << time_mp << " ms\n";
    std::cout << "new/delete : " << time_new << " ms\n";
    std::cout << "Speedup    : " << (time_new / time_mp) << "x\n\n";

    // 多线程测试
    std::cout << "--- Multi Thread (" << THREAD_COUNT << " threads) ---\n";
    int ops_per_thread = ALLOC_COUNT / THREAD_COUNT;

    // 直接传递 lambda 给 measure_time，避免变量自引用
    double time_mp_mt = measure_time([&]() {
        std::vector<std::thread> threads;
        for (int i = 0; i < THREAD_COUNT; ++i) {
            threads.emplace_back(worker_mempool, i, ops_per_thread);
        }
        for (auto &t: threads) {
            t.join();
        }
    });

    double time_new_mt = measure_time([&]() {
        std::vector<std::thread> threads;
        for (int i = 0; i < THREAD_COUNT; ++i) {
            threads.emplace_back(worker_new, i, ops_per_thread);
        }
        for (auto &t: threads) {
            t.join();
        }
    });

    std::cout << "Memory pool: " << time_mp_mt << " ms\n";
    std::cout << "new/delete : " << time_new_mt << " ms\n";
    std::cout << "Speedup    : " << (time_new_mt / time_mp_mt) << "x\n";

    // 额外测试固定大小（消除对齐差异影响）
    std::cout << "\n--- Fixed Size 64 bytes, Single Thread ---\n";
    auto test_fixed_mp = []() {
        ThreadCache *tc = ThreadCache::get_instance();
        std::vector<void *> ptrs(ALLOC_COUNT);
        for (int i = 0; i < ALLOC_COUNT; ++i) {
            ptrs[i] = tc->allocate(64);
            memset(ptrs[i], 0xAA, 64);
        }
        for (int i = 0; i < ALLOC_COUNT; ++i) {
            tc->deallocate(ptrs[i], 64);
        }
    };
    auto test_fixed_new = []() {
        std::vector<void *> ptrs(ALLOC_COUNT);
        for (int i = 0; i < ALLOC_COUNT; ++i) {
            ptrs[i] = ::operator new(64);
            memset(ptrs[i], 0xAA, 64);
        }
        for (int i = 0; i < ALLOC_COUNT; ++i) {
            ::operator delete(ptrs[i]);
        }
    };
    time_mp = measure_time(test_fixed_mp);
    time_new = measure_time(test_fixed_new);
    std::cout << "Memory pool: " << time_mp << " ms\n";
    std::cout << "new/delete : " << time_new << " ms\n";
    std::cout << "Speedup    : " << (time_new / time_mp) << "x\n";
}
