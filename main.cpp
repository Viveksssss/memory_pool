#include <chrono>
#include <iostream>
#include <vector>
#include <v1/memory_pool.h>

volatile uint64_t sink;
constexpr int ITERATIONS = 5000000;

// ========== 情形1：直接使用原始内存池（无模板） ==========
void test_raw_pool() {
    std::cout << "=== 情形1：直接使用 MemoryPool（无模板封装） ===" << std::endl;
    
    // 获取8字节槽的池子
    mp::MemoryPool& pool = mp::HashBucket::get_memory_pool(0);
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        void* p = pool.allocate();
        *reinterpret_cast<uint64_t*>(p) = 20;
        sink = *reinterpret_cast<uint64_t*>(p);
        pool.deallocate(p);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto raw_pool_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "raw pool:    " << raw_pool_time << "ms\n";
}

// ========== 情形2：使用 HashBucket 封装（运行时计算索引） ==========
void test_hashbucket_runtime() {
    std::cout << "=== 情形2：HashBucket 封装（运行时计算索引） ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        void* p = mp::HashBucket::use_memory(sizeof(uint64_t));
        *reinterpret_cast<uint64_t*>(p) = 20;
        sink = *reinterpret_cast<uint64_t*>(p);
        mp::HashBucket::free_memory(p, sizeof(uint64_t));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto hashbucket_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "HashBucket:  " << hashbucket_time << "ms\n";
}

// ========== 情形3：模板封装（编译期计算索引） ==========
void test_template_compiletime() {
    std::cout << "=== 情形3：模板封装（编译期计算索引） ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        auto ps = mp::new_element<uint64_t>(20);
        sink = *ps;
        mp::delete_element(ps);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto template_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "template:    " << template_time << "ms\n";
}

// ========== 情形4：标准 new/delete（基准） ==========
void test_new_delete() {
    std::cout << "=== 情形4：标准 new/delete（基准） ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        auto ps = new uint64_t(20);
        sink = *ps;
        delete ps;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto new_delete_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "new/delete:  " << new_delete_time << "ms\n";
}

// ========== 详细性能分析（分配和释放分开测） ==========
void test_detailed_analysis() {
    std::cout << "\n========== 详细性能分析 ==========\n" << std::endl;
    
    mp::MemoryPool& pool = mp::HashBucket::get_memory_pool(0);
    
    // 1. 纯分配测试
    std::cout << "--- 纯分配（不释放） ---" << std::endl;
    
    std::vector<void*> pool_ptrs;
    pool_ptrs.reserve(ITERATIONS);
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        pool_ptrs.push_back(pool.allocate());
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto pool_alloc = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::vector<uint64_t*> new_ptrs;
    new_ptrs.reserve(ITERATIONS);
    
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        new_ptrs.push_back(new uint64_t(20));
    }
    end = std::chrono::high_resolution_clock::now();
    auto new_alloc = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "  memory_pool: " << pool_alloc << "ms\n";
    std::cout << "  new:         " << new_alloc << "ms\n";
    
    // 2. 纯释放测试
    std::cout << "\n--- 纯释放（已分配完成） ---" << std::endl;
    
    start = std::chrono::high_resolution_clock::now();
    for (auto p : pool_ptrs) {
        pool.deallocate(p);
    }
    end = std::chrono::high_resolution_clock::now();
    auto pool_free = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    start = std::chrono::high_resolution_clock::now();
    for (auto p : new_ptrs) {
        delete p;
    }
    end = std::chrono::high_resolution_clock::now();
    auto new_free = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "  memory_pool: " << pool_free << "ms\n";
    std::cout << "  delete:      " << new_free << "ms\n";
    
    // 3. 总体统计
    std::cout << "\n--- 总体（分配+释放） ---" << std::endl;
    std::cout << "  memory_pool: " << pool_alloc + pool_free << "ms\n";
    std::cout << "  new/delete:  " << new_alloc + new_free << "ms\n";
}

// ========== 不同大小的分配测试 ==========
template<typename T>
void test_different_sizes(const char* type_name) {
    std::cout << "\n--- 分配 " << type_name << " (size=" << sizeof(T) << ") ---" << std::endl;
    
    // 模板版本
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS / 10; ++i) {  // 减少迭代次数避免太久
        auto p = mp::new_element<T>();
        sink = reinterpret_cast<uint64_t>(p);
        mp::delete_element(p);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto template_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // new/delete 版本
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS / 10; ++i) {
        auto p = new T();
        sink = reinterpret_cast<uint64_t>(p);
        delete p;
    }
    end = std::chrono::high_resolution_clock::now();
    auto new_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "  template: " << template_time << "ms\n";
    std::cout << "  new:      " << new_time << "ms\n";
    std::cout << "  ratio:    " << static_cast<double>(new_time) / template_time << "x\n";
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

int main() {
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
    
    std::cout << "\nsink: " << sink << std::endl;  // 防止优化
    
    return 0;
}