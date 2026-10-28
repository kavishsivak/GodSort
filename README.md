# GodSort ⚡

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Header-Only](https://img.shields.io/badge/Header--Only-Single%20Include-orange.svg)](#integration-guide)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()

**GodSort** is an ultra-high-performance, adaptive multi-engine meta-sorting library for modern C++ (C++17 and later).

Standard library sorting algorithms (such as `std::sort` or `std::stable_sort`) apply a single, fixed strategy regardless of data characteristics. **GodSort dynamically evaluates data types, container structure, memory budgets, stability requirements, and runtime distribution patterns** to automatically dispatch the optimal sorting engine:

- 🚀 **Linear-Time LSD Radix Sort** for scalar arithmetic and IEEE 754 floating-point keys ($O(N)$).
- 🧬 **Galloping TimSort** for complex objects, multi-criteria structs, and partially ordered/natural runs ($O(N \log N)$ worst-case, $O(N)$ best-case, strictly stable).
- ⚡ **Adaptive 3-Way Introsort** with Tukey’s Ninther and Dutch National Flag partitioning for general comparisons and minimal memory ($O(N \log N)$ worst-case, in-place $O(1)$ auxiliary memory).
- 🔍 **$O(N)$ Fast-Path Pre-Scan** that instantaneously detects already-sorted or reversed arrays and resolves them with zero unnecessary allocations or comparisons.

---

## Table of Contents

1. [Key Features](#key-features)
2. [Architecture & How It Works](#architecture--how-it-works)
   - [1. Fast-Path Adaptive Pre-Scan](#1-fast-path-adaptive-pre-scan)
   - [2. Engine 1: LSD Radix Sort (O(N) Linear Engine)](#2-engine-1-lsd-radix-sort-on-linear-engine)
   - [3. Engine 2: Adaptive Introsort (Speed & In-Place Engine)](#3-engine-2-adaptive-introsort-speed--in-place-engine)
   - [4. Engine 3: Galloping TimSort (Stable Adaptive Engine)](#4-engine-3-galloping-timsort-stable-adaptive-engine)
3. [Decision Matrix & Engine Selection](#decision-matrix--engine-selection)
4. [Performance Benchmarks](#performance-benchmarks)
5. [Integration Guide](#integration-guide)
   - [Prerequisites](#prerequisites)
   - [Installation](#installation)
   - [Compiler Support & Flags](#compiler-support--flags)
   - [CMake Integration](#cmake-integration)
6. [Syntax & Usage Examples](#syntax--usage-examples)
   - [Basic Container Sorting](#basic-container-sorting)
   - [Raw C-Style Arrays & Iterator Ranges](#raw-c-style-arrays--iterator-ranges)
   - [Custom Comparators (Lambdas & Functors)](#custom-comparators-lambdas--functors)
   - [Enforcing Stability (UI Records / Multi-Criteria)](#enforcing-stability-ui-records--multi-criteria)
   - [Manual Engine Override & Diagnostic Output](#manual-engine-override--diagnostic-output)
   - [Type Traits Customization](#type-traits-customization)
7. [API Reference](#api-reference)
8. [Complexity Summary](#complexity-summary)

---

## Key Features

- **Header-Only & Dependency-Free**: Drop `godsort.hpp` directly into your project. Depends purely on standard C++17 headers.
- **Universal Container Support**: Natively sorts `std::vector`, `std::deque`, `std::array`, raw C-style arrays (`T[]`), and any custom random-access range.
- **Full IEEE 754 Floating-Point Support**: Linear Radix sorting handles negative zero (`-0.0`), positive zero (`+0.0`), subnormals, infinities, and negative numbers with bitwise sign-flipping.
- **Duplicate-Heavy Resistance**: 3-Way Dutch National Flag partitioning collapses duplicate-heavy data down to linear $O(N)$ comparisons.
- **Strict Stability Guarantees**: Preserves original relative order for equivalent keys when sorting complex records, table rows, or multi-pass databases.
- **Zero-Overhead Abstractions**: Modern compile-time SFINAE type traits and compiler built-ins (`__builtin_clzll` / `_BitScanReverse64`) ensure minimal runtime overhead.

---

## Architecture & How It Works

GodSort operates through a tiered decision pipeline:

```
                       Input Range / Container
                                  │
                   ┌──────────────┴──────────────┐
                   │   O(N) Adaptive Pre-Scan    │
                   └──────────────┬──────────────┘
                                  │
         ┌────────────────────────┼────────────────────────┐
         ▼                        ▼                        ▼
  [Already Sorted]       [Strictly Reversed]       [General Unsorted]
   Return in O(N)          Invert in O(N)                  │
                                                           ▼
                                               Compile-Time & Runtime
                                                   Engine Resolver
                                                           │
                      ┌────────────────────────────────────┼────────────────────────────────────┐
                      ▼                                    ▼                                    ▼
             [LSD Radix Sort]                     [Adaptive Introsort]                     [TimSort]
             • O(N) Linear Time                   • In-Place O(1) Memory                  • Strictly Stable
             • Primitives & Floats                • 3-Way Dutch Flag Partitioning         • Natural Runs Detection
             • Byte-Level Radix-256               • Tukey's Ninther & Heapsort Guard      • Galloping Mode Merge
```

### 1. Fast-Path Adaptive Pre-Scan

Before invoking any heavy partitioning or allocating scratch memory, GodSort performs a single-pass cache-friendly scan ($O(N)$ time, $0$ allocations):
- **Already Sorted Detection**: If elements are already in non-descending order, sorting terminates immediately.
- **Strictly Reversed Detection**: If elements are in descending order, GodSort reverses them in-place via `std::reverse` in $O(N)$ time (for scalars or where stability is not strictly required).
- **Early Exit**: If the pre-scan determines that the range is neither sorted nor purely inverted within early checks, it breaks immediately to avoid wasting CPU cycles.

### 2. Engine 1: LSD Radix Sort ($O(N)$ Linear Engine)

For scalar keys (integers, characters, IEEE 754 floats) using default ascending order, GodSort deploys a Least Significant Digit (LSD) Radix Sort:
- **8-Bit Radix (256 Buckets)**: Processes keys byte-by-byte (4 passes for 32-bit types, 8 passes for 64-bit types).
- **Pass Skipping**: If all elements in a pass fall into a single bucket ($count[b] == N$), the distribution pass is completely skipped.
- **Signed Integer Transformation**: Converts signed two's-complement integers into unsigned order-preserving keys by flipping the sign bit (`key ^ (1 << (bits - 1))`).
- **IEEE 754 Floating-Point Transformation**: Maps `float` and `double` into unsigned integers while preserving proper ordering across negative numbers, negative zero, and positives:
  ```cpp
  // Positive floats: flip only the sign bit
  // Negative floats: invert all bits
  uint32_t mask = (u & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
  return u ^ mask;
  ```
- **Stability**: Radix Sort is unconditionally stable.

### 3. Engine 2: Adaptive Introsort (Speed & In-Place Engine)

When memory constraints demand $O(1)$ auxiliary storage or when custom comparators are applied to primitives, GodSort uses a modernized Introsort:
- **Tukey’s Ninther Pivot Selection**: For arrays with $N \ge 64$, selects the pivot using the median of three medians-of-three. This drastically reduces susceptibility to worst-case quadratic behavior on pathological inputs.
- **3-Way Dutch National Flag Partitioning**: Separates the input into three distinct regions: $[ < \text{pivot} \mid == \text{pivot} \mid > \text{pivot} ]$. Ranges with high duplicate frequencies (low entropy) are sorted in near-linear $O(N)$ time because elements equal to the pivot are never touched again.
- **HeapSort Safeguard**: Tracks recursion depth ($2 \lfloor \log_2 N \rfloor$). If recursive depth limit is reached, it seamlessly falls back to in-place `heapsort`, guaranteeing an absolute $O(N \log N)$ worst-case upper bound.
- **Insertion Sort Cutoff**: Partitions smaller than 20 elements switch to cache-optimized linear insertion sort.

### 4. Engine 3: Galloping TimSort (Stable Adaptive Engine)

For non-scalar data types (classes, user-defined structs) or when stability is enforced:
- **Natural Run Detection**: Scans the input to identify existing non-descending runs or strictly descending runs. Strictly descending runs are inverted in-place in linear time.
- **Dynamic `minrun`**: Computes an optimal minimum run length ($32 \le \text{minrun} \le 64$) such that $N / \text{minrun}$ is equal to or slightly less than a power of 2, producing perfectly balanced merge trees.
- **Binary Insertion Sort**: Short runs are extended to `minrun` using binary insertion sort ($O(\log N)$ comparisons).
- **Stack Merge Invariants**: Maintains runs on an internal stack adhering to TimSort merge invariants:
  1. $A > B + C$
  2. $B > C$
- **Galloping Mode**: During run merges, if one run consecutively wins elements $\ge \text{min\_gallop}$ (default 7) times, TimSort enters exponential galloping mode. It uses binary search to jump exponentially ($1, 3, 7, 15, \dots$) through the winning run, cutting comparisons from $O(k)$ to $O(\log k)$.

---

## Decision Matrix & Engine Selection

GodSort uses the following decision logic when `godsort::Options::engine` is set to `Engine::Auto` (default):

| Data Type | Comparator | Stability Mode | Size ($N$) | Dispatched Engine | Auxiliary Memory | Time Complexity |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Scalar (int, float, etc.)** | `std::less` | `Auto` or `Unstable` | $N \ge 128$ | **LSD Radix Sort** | $O(N)$ | $O(N)$ |
| **Scalar (int, float, etc.)** | `std::less` | `ForceStable` | $N \ge 64$ | **LSD Radix Sort** | $O(N)$ | $O(N)$ |
| **Scalar (int, float, etc.)** | Custom / Lambda | Any | Any | **Adaptive Introsort** | $O(1)$ | $O(N \log N)$ |
| **Scalar (small)** | `std::less` | Any | $N < 64$ | **Adaptive Introsort** | $O(1)$ | $O(N \log N)$ |
| **Complex Struct / Class** | Any | `Auto` / `ForceStable`| Any | **Galloping TimSort** | $O(N)$ | $O(N \log N)$ |
| **Complex Struct / Class** | Any | `PreferUnstableFast` | Any | **Adaptive Introsort** | $O(1)$ | $O(N \log N)$ |
| **Any (Already Sorted)** | Any | Any | Any | **Pre-Scan Exit** | $0$ | $O(N)$ |
| **Any (Strictly Reversed)** | Any | `Auto` / `Unstable` | Any | **In-Place Reverse** | $0$ | $O(N)$ |

---

## Performance Benchmarks

*Benchmark environment: Windows 11 x86_64, MinGW GCC 15.2.0, `-O3 -std=c++17`. Workload: 200,000 `int32_t` elements.*

| Scenario | `std::sort` | `std::stable_sort` | `godsort::sort` | Speedup vs `std::sort` |
| :--- | :---: | :---: | :---: | :---: |
| **Random Uniform Integers** | `11.77 ms` | `13.34 ms` | **`5.96 ms`** | **~2.0x faster** |
| **Already Sorted Array** | `1.93 ms` | `1.67 ms` | **`0.11 ms`** | **~17.5x faster** |
| **Strictly Reversed Array**| `1.43 ms` | `2.39 ms` | **`0.13 ms`** | **~11.0x faster** |
| **Duplicate Heavy (16 keys)**| `4.76 ms` | `6.28 ms` | **`4.43 ms`** | **~1.1x faster** |

---

## Integration Guide

### Prerequisites

- Any C++ compiler supporting **C++17** or higher:
  - GCC 7.0+
  - Clang 5.0+
  - MSVC 2017+ (Visual Studio 15.3+)
- Standard C++ Library (`<vector>`, `<algorithm>`, `<type_traits>`, etc.)

### Installation

GodSort is **header-only**. No compilation or linking of external libraries is required.

1. Clone the repository:
   ```bash
   git clone https://github.com/kavishsivak/GodSort.git
   ```
2. Copy `godsort.hpp` into your project's include directory:
   ```bash
   cp GodSort/godsort.hpp /path/to/your/project/include/
   ```
3. Include the header in your C++ code:
   ```cpp
   #include "godsort.hpp"
   ```

### Compiler Support & Flags

Enable C++17 optimization flags when compiling:

```bash
# GCC / MinGW
g++ -O3 -std=c++17 main.cpp -o app

# Clang / LLVM
clang++ -O3 -std=c++17 main.cpp -o app

# Microsoft Visual C++ (MSVC)
cl.exe /O2 /std:c++17 main.cpp
```

### CMake Integration

To include GodSort in an existing CMake project:

```cmake
cmake_minimum_required(VERSION 3.12)
project(MyProject CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add GodSort directory to include path
target_include_directories(my_target PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/path/to/GodSort)
```

---

## Syntax & Usage Examples

### Basic Container Sorting

GodSort operates directly on any linear container without needing explicit iterator boilerplate:

```cpp
#include "godsort.hpp"
#include <vector>
#include <deque>
#include <array>
#include <string>

int main() {
    // std::vector
    std::vector<int> numbers = {50, -2, 100, 0, 7, 23};
    godsort::sort(numbers);

    // std::deque
    std::deque<double> decimals = {3.14, -0.5, 2.718, -100.2};
    godsort::sort(decimals);

    // std::array
    std::array<std::string, 4> names = {"Delta", "Alpha", "Gamma", "Beta"};
    godsort::sort(names);

    return 0;
}
```

### Raw C-Style Arrays & Iterator Ranges

GodSort supports both raw arrays and classic iterator pairs `(begin, end)`:

```cpp
#include "godsort.hpp"
#include <vector>
#include <iostream>

int main() {
    // 1. Raw C-style fixed-size array
    int raw_arr[] = {9, 3, 7, 1, 8, 2, 5, 4, 6, 0};
    godsort::sort(raw_arr); // Automatically deduces size

    // 2. Iterator range
    std::vector<int> vec = {10, 40, 20, 50, 30};
    godsort::sort(vec.begin() + 1, vec.end() - 1); // Sorts only {40, 20, 50}

    return 0;
}
```

### Custom Comparators (Lambdas & Functors)

Pass custom comparison predicates just like `std::sort`:

```cpp
#include "godsort.hpp"
#include <vector>
#include <string>
#include <iostream>

struct Product {
    std::string name;
    double price;
    int rating;
};

int main() {
    std::vector<Product> products = {
        {"Keyboard", 79.99, 4},
        {"Mouse", 49.99, 5},
        {"Monitor", 299.99, 4},
        {"Desk Lamp", 29.99, 5}
    };

    // Sort by rating descending; if tied, sort by price ascending
    godsort::sort(products, [](const Product& a, const Product& b) {
        if (a.rating != b.rating) {
            return a.rating > b.rating;
        }
        return a.price < b.price;
    });

    for (const auto& p : products) {
        std::cout << p.name << " (" << p.rating << " stars, $" << p.price << ")\n";
    }

    return 0;
}
```

### Enforcing Stability (UI Records / Multi-Criteria)

For multi-criteria sorting where elements with equal primary keys must preserve their original relative order (e.g., sorting table rows):

```cpp
#include "godsort.hpp"
#include <vector>
#include <string>

struct TableRow {
    int id;
    std::string department;
    double salary;
};

int main() {
    std::vector<TableRow> rows = get_database_rows();

    // Method 1: Using godsort::stable_sort convenience function
    godsort::stable_sort(rows, [](const TableRow& a, const TableRow& b) {
        return a.department < b.department;
    });

    // Method 2: Using godsort::Options
    godsort::Options opts;
    opts.stability = godsort::StabilityRequirement::ForceStable;
    godsort::sort(rows, [](const TableRow& a, const TableRow& b) {
        return a.department < b.department;
    }, opts);

    return 0;
}
```

### Manual Engine Override & Diagnostic Output

You can inspect the engine chosen at runtime or force a specific algorithm:

```cpp
#include "godsort.hpp"
#include <vector>

int main() {
    std::vector<int> data = {9, 4, 1, 7, 3, 8, 2, 5};

    godsort::Options opts;
    opts.engine = godsort::Engine::RadixSort; // Force LSD Radix Sort
    opts.verbose = true;                      // Print diagnostic info to stdout

    godsort::sort(data, opts);
    // Output: [GodSort Engine Dispatched]: LSD Radix Sort (O(N) Stable Linear) for size 8

    return 0;
}
```

### Type Traits Customization

For custom classes, you can inform GodSort about special requirements at compile time using `godsort::sort_traits`:

```cpp
#include "godsort.hpp"

struct Transaction {
    uint64_t timestamp;
    double amount;
};

// Inform GodSort that Transaction requires stability
namespace godsort {
    template <>
    struct sort_traits<Transaction> {
        static constexpr bool is_complex = true;
        static constexpr bool requires_stability = true;
        static constexpr bool custom_radix_supported = false;
    };
}
```

---

## API Reference

### Configuration Structures

#### `enum class godsort::Engine`
- `Engine::Auto`: Automatically select the optimal engine.
- `Engine::AdaptiveIntro`: In-place introsort (Quick + Heap + Insertion sort).
- `Engine::TimSort`: Adaptive galloping merge sort (strictly stable).
- `Engine::RadixSort`: High-throughput byte-level counting sort (linear time, stable).

#### `enum class godsort::StabilityRequirement`
- `StabilityRequirement::Auto`: Automatically deduced from type traits (scalar $\to$ fast unstable, struct $\to$ stable).
- `StabilityRequirement::ForceStable`: Strictly preserve original relative order of equivalent items.
- `StabilityRequirement::PreferUnstableFast`: Optimize purely for maximum speed and $O(1)$ memory.

#### `struct godsort::Options`
- `Engine engine = Engine::Auto`: Selected engine policy.
- `StabilityRequirement stability = StabilityRequirement::Auto`: Stability policy.
- `bool verbose = false`: Enables diagnostic logging to `std::cout`.

### Function Overloads

```cpp
// 1. Container overloads
template <typename Container>
void godsort::sort(Container& c, Options options = {});

template <typename Container, typename Comp>
void godsort::sort(Container& c, Comp comp, Options options = {});

// 2. Iterator range overloads
template <typename Iter>
void godsort::sort(Iter first, Iter last, Options options = {});

template <typename Iter, typename Comp>
void godsort::sort(Iter first, Iter last, Comp comp, Options options = {});

// 3. Convenience shortcuts
template <typename... Args>
void godsort::stable_sort(Args&&... args); // Forces StabilityRequirement::ForceStable

template <typename... Args>
void godsort::fast_sort(Args&&... args);   // Forces StabilityRequirement::PreferUnstableFast
```

---

## Complexity Summary

| Engine | Best Time | Average Time | Worst Time | Space Overhead | Stable | Best Used For |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **Pre-Scan (Early Exit)** | $O(N)$ | — | $O(N)$ | $O(1)$ | Yes | Already sorted or reverse-sorted inputs |
| **LSD Radix Sort** | $O(N \cdot k)$ | $O(N \cdot k)$ | $O(N \cdot k)$ | $O(N)$ | Yes | Integers, chars, IEEE 754 floats ($k = \text{sizeof}(T)$) |
| **Adaptive Introsort** | $O(N)$ | $O(N \log N)$ | $O(N \log N)$ | $O(1)$ | No | General types, custom lambdas, memory-constrained systems |
| **Galloping TimSort** | $O(N)$ | $O(N \log N)$ | $O(N \log N)$ | $O(N)$ | Yes | Complex structs, database records, partially ordered data |

---

## Testing & Verification

A comprehensive test suite and benchmark suite is provided in `godsort.cpp`. To build and run all test cases:

```bash
# Compile with C++17 and optimization
g++ -O3 -std=c++17 godsort.cpp -o godsort_test

# Run the test suite
./godsort_test
```

The test runner validates:
1. Linear container compatibility (`vector`, `deque`, `array`, raw C-arrays).
2. Stability preservation for multi-criteria records.
3. Custom comparator lambda evaluations.
4. LSD Radix sort on signed 32-bit integers and IEEE 754 negative floats.
5. Adaptive introsort under sorted, reversed, and duplicate-heavy stresses.
6. Side-by-side performance benchmarks against `std::sort` and `std::stable_sort`.

---

## License

This project is licensed under the [MIT License](LICENSE).
