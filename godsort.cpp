#include "godsort.hpp"
#include <iostream>
#include <vector>
#include <deque>
#include <array>
#include <string>
#include <random>
#include <chrono>
#include <cassert>
#include <iomanip>

// ============================================================================
// TEST FIXTURES & USER-DEFINED TYPES
// ============================================================================

// A complex struct representing UI table rows or multi-criteria database records
struct Record {
    int key;
    std::string tag;
    int original_index; // Used to strictly verify stability

    bool operator<(const Record& other) const {
        return key < other.key;
    }
    bool operator==(const Record& other) const {
        return key == other.key && tag == other.tag && original_index == other.original_index;
    }
};

// Custom class without default operator< to test custom comparator lambda
class Product {
private:
    std::string m_name;
    double m_price;
    int m_rating;

public:
    Product(std::string name, double price, int rating)
        : m_name(std::move(name)), m_price(price), m_rating(rating) {}

    const std::string& name() const { return m_name; }
    double price() const { return m_price; }
    int rating() const { return m_rating; }
};

// ============================================================================
// VERIFICATION HELPERS
// ============================================================================

template <typename Range, typename Comp = std::less<typename std::iterator_traits<decltype(std::begin(std::declval<Range&>()))>::value_type>>
bool is_correctly_sorted(const Range& r, Comp comp = Comp{}) {
    auto first = std::begin(r);
    auto last = std::end(r);
    if (first == last) return true;
    for (auto it = first; it + 1 != last; ++it) {
        if (comp(*(it + 1), *it)) return false;
    }
    return true;
}

// ============================================================================
// DEMO 1: LINEAR CONTAINERS (vector, deque, array, C-style raw array)
// ============================================================================
void test_linear_containers() {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 1: Linear Container Compatibility\n";
    std::cout << "========================================\n";

    // 1. std::vector
    std::vector<int> vec = {42, -5, 100, 0, 7, 23, -50, 12};
    godsort::sort(vec);
    assert(is_correctly_sorted(vec));
    std::cout << "[PASS] std::vector<int> sorted successfully.\n";

    // 2. std::deque
    std::deque<double> deq = {3.14, -0.5, 2.718, -100.2, 0.0, 1.414};
    godsort::sort(deq);
    assert(is_correctly_sorted(deq));
    std::cout << "[PASS] std::deque<double> sorted successfully.\n";

    // 3. std::array
    std::array<std::string, 5> arr = {{"gamma", "alpha", "epsilon", "beta", "delta"}};
    godsort::sort(arr);
    assert(is_correctly_sorted(arr));
    std::cout << "[PASS] std::array<string> sorted successfully.\n";

    // 4. Raw C-style array
    int raw_arr[] = {9, 3, 7, 1, 8, 2, 5, 4, 6, 0};
    godsort::sort(raw_arr);
    assert(is_correctly_sorted(raw_arr));
    std::cout << "[PASS] Raw C-style array sorted successfully.\n";
}

// ============================================================================
// DEMO 2: CUSTOM STRUCTS & TIMSORT STABILITY (Preserving Initial State)
// ============================================================================
void test_custom_struct_stability() {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 2: Stability on Complex Structs (TimSort)\n";
    std::cout << "========================================\n";

    std::vector<Record> records;
    records.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        Record r;
        r.key = i % 10;
        r.tag = "Row_" + std::to_string(i);
        r.original_index = i;
        records.push_back(r);
    }

    auto comp = [](const Record& a, const Record& b) {
        return a.key < b.key;
    };

    godsort::Options opts;
    opts.verbose = true;
    opts.stability = godsort::StabilityRequirement::ForceStable;

    godsort::sort(records, comp, opts);

    bool stable = true;
    for (size_t i = 1; i < records.size(); ++i) {
        if (records[i].key < records[i - 1].key) {
            std::cout << "[FAIL] Incorrect ordering!\n";
            assert(false);
        }
        if (records[i].key == records[i - 1].key) {
            if (records[i].original_index < records[i - 1].original_index) {
                stable = false;
                break;
            }
        }
    }

    assert(stable);
    std::cout << "[PASS] Multi-criteria Record sort preserved exact initial order for equal keys!\n";
}

// ============================================================================
// DEMO 3: CUSTOM COMPARATOR LAMBDA (Product Class)
// ============================================================================
void test_custom_comparator() {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 3: Custom Class with Custom Lambda\n";
    std::cout << "========================================\n";

    std::vector<Product> products;
    products.push_back(Product("Keyboard", 79.99, 4));
    products.push_back(Product("Mouse", 49.99, 5));
    products.push_back(Product("Monitor", 299.99, 4));
    products.push_back(Product("Desk Lamp", 29.99, 5));
    products.push_back(Product("USB Hub", 19.99, 3));

    auto multi_comp = [](const Product& a, const Product& b) {
        if (a.rating() != b.rating()) {
            return a.rating() > b.rating();
        }
        return a.price() < b.price();
    };

    godsort::sort(products, multi_comp);

    std::cout << "Sorted Products (Rating Desc, Price Asc):\n";
    for (const auto& p : products) {
        std::cout << "  - " << std::left << std::setw(12) << p.name()
                  << " Rating: " << p.rating()
                  << "  Price: $" << p.price() << "\n";
    }

    assert(products[0].name() == "Desk Lamp");
    assert(products[1].name() == "Mouse");
    assert(products[2].name() == "Keyboard");
    assert(products[3].name() == "Monitor");
    assert(products[4].name() == "USB Hub");
    std::cout << "[PASS] Custom multi-field comparator lambda executed cleanly.\n";
}

// ============================================================================
// DEMO 4: RADIX SORT (Integers and Negative Floats)
// ============================================================================
void test_radix_sort() {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 4: High-Performance LSD Radix Sort\n";
    std::cout << "========================================\n";

    std::mt19937 rng(1337);
    std::uniform_int_distribution<int32_t> dist(-1000000, 1000000);

    std::vector<int32_t> int_data(50000);
    for (auto& x : int_data) x = dist(rng);

    godsort::Options opts;
    opts.engine = godsort::Engine::RadixSort;
    opts.verbose = true;
    godsort::sort(int_data, opts);
    assert(is_correctly_sorted(int_data));
    std::cout << "[PASS] 50,000 signed 32-bit integers sorted via Radix Sort.\n";

    std::uniform_real_distribution<float> fdist(-10000.0f, 10000.0f);
    std::vector<float> float_data(50000);
    for (auto& f : float_data) f = fdist(rng);
    float_data[10] = -0.0f;
    float_data[11] = 0.0f;

    godsort::sort(float_data, opts);
    assert(is_correctly_sorted(float_data));
    std::cout << "[PASS] 50,000 floats (including negative & zeros) sorted via IEEE 754 Radix Sort.\n";
}

// ============================================================================
// DEMO 5: ADAPTIVE INTROSORT (Sorted, Reverse, and Duplicate Heavy Data)
// ============================================================================
void test_adaptive_introsort() {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 5: Adaptive Introsort (Quick + Heap + Insertion)\n";
    std::cout << "========================================\n";

    std::vector<int> sorted_arr(100000);
    for (int i = 0; i < 100000; ++i) sorted_arr[i] = i;

    godsort::Options opts;
    opts.engine = godsort::Engine::AdaptiveIntro;
    opts.verbose = true;

    auto t0 = std::chrono::high_resolution_clock::now();
    godsort::sort(sorted_arr, opts);
    auto t1 = std::chrono::high_resolution_clock::now();
    double time_sorted = std::chrono::duration<double, std::milli>(t1 - t0).count();
    assert(is_correctly_sorted(sorted_arr));
    std::cout << "Sorted array (100k items) adaptively checked in: " << time_sorted << " ms\n";

    std::vector<int> rev_arr(100000);
    for (int i = 0; i < 100000; ++i) rev_arr[i] = 100000 - i;

    t0 = std::chrono::high_resolution_clock::now();
    godsort::sort(rev_arr, opts);
    t1 = std::chrono::high_resolution_clock::now();
    double time_rev = std::chrono::duration<double, std::milli>(t1 - t0).count();
    assert(is_correctly_sorted(rev_arr));
    std::cout << "Reverse-sorted array (100k items) adaptively inverted in: " << time_rev << " ms\n";

    std::vector<int> duplicates(100000);
    std::mt19937 rng(42);
    for (auto& x : duplicates) x = rng() % 5;

    t0 = std::chrono::high_resolution_clock::now();
    godsort::sort(duplicates, opts);
    t1 = std::chrono::high_resolution_clock::now();
    double time_dup = std::chrono::duration<double, std::milli>(t1 - t0).count();
    assert(is_correctly_sorted(duplicates));
    std::cout << "Duplicate-heavy array (100k items, 5 distinct keys) sorted in: " << time_dup << " ms\n";
    std::cout << "[PASS] Adaptive IntroSort passed all structural stress cases.\n";
}

// ============================================================================
// DEMO 6: PERFORMANCE BENCHMARK (GodSort vs std::sort vs std::stable_sort)
// ============================================================================
void run_benchmarks() {
    std::cout << "\n=======================================================\n";
    std::cout << "BENCHMARK: GodSort vs std::sort vs std::stable_sort\n";
    std::cout << "Size: 200,000 int32_t elements per test case\n";
    std::cout << "=======================================================\n";

    constexpr size_t N = 200000;
    std::mt19937 rng(999);
    std::vector<int32_t> base_random(N);
    for (auto& x : base_random) x = static_cast<int32_t>(rng());

    std::vector<int32_t> base_sorted = base_random;
    std::sort(base_sorted.begin(), base_sorted.end());

    std::vector<int32_t> base_reversed = base_sorted;
    std::reverse(base_reversed.begin(), base_reversed.end());

    std::vector<int32_t> base_dups(N);
    for (auto& x : base_dups) x = static_cast<int32_t>(rng() % 16);

    auto bench = [](const std::string& label, const std::vector<int32_t>& src) {
        std::cout << "\nScenario: " << label << "\n";
        std::cout << "-------------------------------------------------------\n";

        auto v1 = src;
        auto t0 = std::chrono::high_resolution_clock::now();
        std::sort(v1.begin(), v1.end());
        auto t1 = std::chrono::high_resolution_clock::now();
        double d_stdsort = std::chrono::duration<double, std::milli>(t1 - t0).count();

        auto v2 = src;
        t0 = std::chrono::high_resolution_clock::now();
        std::stable_sort(v2.begin(), v2.end());
        t1 = std::chrono::high_resolution_clock::now();
        double d_stablesort = std::chrono::duration<double, std::milli>(t1 - t0).count();

        auto v3 = src;
        t0 = std::chrono::high_resolution_clock::now();
        godsort::sort(v3);
        t1 = std::chrono::high_resolution_clock::now();
        double d_godsort = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "  std::sort        : " << std::setw(8) << std::fixed << std::setprecision(2) << d_stdsort << " ms\n";
        std::cout << "  std::stable_sort : " << std::setw(8) << std::fixed << std::setprecision(2) << d_stablesort << " ms\n";
        std::cout << "  GodSort (Auto)   : " << std::setw(8) << std::fixed << std::setprecision(2) << d_godsort << " ms\n";

        assert(v1 == v3);
    };

    bench("Random Uniform 32-bit Integers", base_random);
    bench("Already Sorted Data", base_sorted);
    bench("Strictly Reverse Sorted Data", base_reversed);
    bench("Duplicate Heavy (16 distinct keys)", base_dups);
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "        GODSORT: ADAPTIVE MULTI-ENGINE SORTER           \n";
    std::cout << "========================================================\n";

    test_linear_containers();
    test_custom_struct_stability();
    test_custom_comparator();
    test_radix_sort();
    test_adaptive_introsort();
    run_benchmarks();

    std::cout << "\n[SUCCESS] All GodSort tests and benchmarks completed successfully!\n";
    return 0;
}
