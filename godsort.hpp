#ifndef GODSORT_HPP
#define GODSORT_HPP

#include <iostream>
#include <vector>
#include <iterator>
#include <algorithm>
#include <type_traits>
#include <utility>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <memory>
#include <new>

namespace godsort {

// ============================================================================
// 1. CONFIGURATION, TRAITS & POLICIES
// ============================================================================

enum class Engine {
    Auto,            // Automatically choose best engine
    AdaptiveIntro,   // Quick + Heap + Insertion sort (In-place, raw speed, O(1) memory)
    TimSort,         // Natural runs + Galloping merge (Adaptive, Stable, Cache-friendly)
    RadixSort        // Linear-time byte-level sort (O(N), unconditionally Stable)
};

enum class StabilityRequirement {
    Auto,               // Deduces from data type (e.g. primitive -> unstable fast, struct -> stable)
    ForceStable,        // Must preserve relative order of equal elements
    PreferUnstableFast  // Optimize purely for raw speed and minimal memory
};

struct Options {
    Engine engine = Engine::Auto;
    StabilityRequirement stability = StabilityRequirement::Auto;
    bool verbose = false; // Outputs diagnostic information about engine selection
};

// Customization point for user-defined types:
template <typename T>
struct sort_traits {
    static constexpr bool is_complex = !std::is_scalar<T>::value;
    static constexpr bool requires_stability = is_complex;
    static constexpr bool custom_radix_supported = false;
};

// Trait helper to determine if comparator is default std::less
template <typename Comp, typename T>
struct is_default_less : std::false_type {};

template <typename T>
struct is_default_less<std::less<T>, T> : std::true_type {};

template <typename T>
struct is_default_less<std::less<void>, T> : std::true_type {};

// Portable log2 calculation
inline size_t compute_log2_floor(size_t n) noexcept {
    if (n == 0) return 0;
#if defined(__GNUC__) || defined(__clang__)
    return 63 - __builtin_clzll(static_cast<unsigned long long>(n));
#elif defined(_MSC_VER)
    unsigned long index;
    if (_BitScanReverse64(&index, static_cast<unsigned __int64>(n))) return index;
    return 0;
#else
    size_t r = 0;
    while (n >>= 1) ++r;
    return r;
#endif
}

// ============================================================================
// 2. RADIX CONVERTERS (FOR ARITHMETIC TYPES & FLOATS)
// ============================================================================

template <typename T>
struct RadixKeyTraits {
    static constexpr bool is_supported = false;
};

#define REGISTER_RADIX_UNSIGNED(TYPE) \
template <> struct RadixKeyTraits<TYPE> { \
    static constexpr bool is_supported = true; \
    using KeyType = TYPE; \
    static constexpr KeyType to_key(TYPE val) noexcept { return val; } \
    static constexpr TYPE from_key(KeyType key) noexcept { return key; } \
};

#define REGISTER_RADIX_SIGNED(TYPE, UNSIGNED_TYPE) \
template <> struct RadixKeyTraits<TYPE> { \
    static constexpr bool is_supported = true; \
    using KeyType = UNSIGNED_TYPE; \
    static constexpr KeyType to_key(TYPE val) noexcept { \
        return static_cast<KeyType>(val) ^ (KeyType(1) << (sizeof(TYPE) * 8 - 1)); \
    } \
    static constexpr TYPE from_key(KeyType key) noexcept { \
        return static_cast<TYPE>(key ^ (KeyType(1) << (sizeof(TYPE) * 8 - 1))); \
    } \
};

REGISTER_RADIX_UNSIGNED(uint8_t)
REGISTER_RADIX_UNSIGNED(uint16_t)
REGISTER_RADIX_UNSIGNED(uint32_t)
REGISTER_RADIX_UNSIGNED(uint64_t)
REGISTER_RADIX_UNSIGNED(unsigned long)

REGISTER_RADIX_SIGNED(int8_t, uint8_t)
REGISTER_RADIX_SIGNED(int16_t, uint16_t)
REGISTER_RADIX_SIGNED(int32_t, uint32_t)
REGISTER_RADIX_SIGNED(int64_t, uint64_t)
REGISTER_RADIX_SIGNED(char, uint8_t)
#if defined(__SIZEOF_LONG__) && __SIZEOF_LONG__ == 8
REGISTER_RADIX_SIGNED(long, uint64_t)
#endif

// IEEE 754 Floating point radix mapping
template <>
struct RadixKeyTraits<float> {
    static constexpr bool is_supported = true;
    using KeyType = uint32_t;
    static KeyType to_key(float val) noexcept {
        uint32_t u;
        std::memcpy(&u, &val, sizeof(float));
        uint32_t mask = (u & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
        return u ^ mask;
    }
    static float from_key(KeyType key) noexcept {
        uint32_t mask = (key & 0x80000000u) ? 0x80000000u : 0xFFFFFFFFu;
        uint32_t u = key ^ mask;
        float res;
        std::memcpy(&res, &u, sizeof(float));
        return res;
    }
};

template <>
struct RadixKeyTraits<double> {
    static constexpr bool is_supported = true;
    using KeyType = uint64_t;
    static KeyType to_key(double val) noexcept {
        uint64_t u;
        std::memcpy(&u, &val, sizeof(double));
        uint64_t mask = (u & 0x8000000000000000ULL) ? 0xFFFFFFFFFFFFFFFFULL : 0x8000000000000000ULL;
        return u ^ mask;
    }
    static double from_key(KeyType key) noexcept {
        uint64_t mask = (key & 0x8000000000000000ULL) ? 0x8000000000000000ULL : 0xFFFFFFFFFFFFFFFFULL;
        uint64_t u = key ^ mask;
        double res;
        std::memcpy(&res, &u, sizeof(double));
        return res;
    }
};

// ============================================================================
// 3. ENGINE 1: LSD RADIX SORT (O(N) Stable Linear Engine)
// ============================================================================

namespace detail {

template <typename Iter>
void radix_sort_impl(Iter first, Iter last) {
    using ValueType = typename std::iterator_traits<Iter>::value_type;
    using Traits = RadixKeyTraits<ValueType>;
    using KeyType = typename Traits::KeyType;

    const size_t n = static_cast<size_t>(last - first);
    if (n <= 1) return;

    constexpr size_t num_bytes = sizeof(KeyType);
    constexpr size_t BUCKETS = 256;

    std::vector<ValueType> aux(n);
    std::vector<KeyType> keys(n);
    std::vector<KeyType> aux_keys(n);

    for (size_t i = 0; i < n; ++i) {
        keys[i] = Traits::to_key(*(first + i));
    }

    std::vector<ValueType> primary(n);
    for (size_t i = 0; i < n; ++i) {
        primary[i] = std::move(*(first + i));
    }

    ValueType* src_vals = primary.data();
    ValueType* dst_vals = aux.data();
    KeyType* src_keys = keys.data();
    KeyType* dst_keys = aux_keys.data();

    for (size_t pass = 0; pass < num_bytes; ++pass) {
        const size_t shift = pass * 8;
        size_t count[BUCKETS] = {0};

        for (size_t i = 0; i < n; ++i) {
            uint8_t byte_val = static_cast<uint8_t>((src_keys[i] >> shift) & 0xFF);
            count[byte_val]++;
        }

        bool can_skip = false;
        for (size_t b = 0; b < BUCKETS; ++b) {
            if (count[b] == n) {
                can_skip = true;
                break;
            }
        }
        if (can_skip) continue;

        size_t offset[BUCKETS];
        offset[0] = 0;
        for (size_t b = 1; b < BUCKETS; ++b) {
            offset[b] = offset[b - 1] + count[b - 1];
        }

        for (size_t i = 0; i < n; ++i) {
            uint8_t byte_val = static_cast<uint8_t>((src_keys[i] >> shift) & 0xFF);
            size_t pos = offset[byte_val]++;
            dst_vals[pos] = std::move(src_vals[i]);
            dst_keys[pos] = src_keys[i];
        }

        std::swap(src_vals, dst_vals);
        std::swap(src_keys, dst_keys);
    }

    for (size_t i = 0; i < n; ++i) {
        *(first + i) = std::move(src_vals[i]);
    }
}

// ============================================================================
// 4. ENGINE 2: ADAPTIVE INTROSORT (Quicksort + Heapsort + Insertion Sort)
// ============================================================================

constexpr size_t INTRO_INSERTION_THRESHOLD = 20;

template <typename Iter, typename Comp>
void insertion_sort(Iter first, Iter last, Comp& comp) {
    for (Iter i = first + 1; i < last; ++i) {
        auto val = std::move(*i);
        Iter j = i;
        while (j > first && comp(val, *(j - 1))) {
            *j = std::move(*(j - 1));
            --j;
        }
        *j = std::move(val);
    }
}

template <typename Iter, typename Comp>
Iter median_of_3(Iter a, Iter b, Iter c, Comp& comp) {
    if (comp(*a, *b)) {
        if (comp(*b, *c)) return b;
        else if (comp(*a, *c)) return c;
        else return a;
    } else {
        if (comp(*a, *c)) return a;
        else if (comp(*b, *c)) return c;
        else return b;
    }
}

template <typename Iter, typename Comp>
Iter select_pivot(Iter first, Iter last, Comp& comp) {
    size_t n = static_cast<size_t>(last - first);
    Iter mid = first + n / 2;
    if (n < 64) {
        return median_of_3(first, mid, last - 1, comp);
    }
    size_t step = n / 8;
    Iter m1 = median_of_3(first, first + step, first + 2 * step, comp);
    Iter m2 = median_of_3(first + 3 * step, mid, first + 5 * step, comp);
    Iter m3 = median_of_3(first + 6 * step, first + 7 * step, last - 1, comp);
    return median_of_3(m1, m2, m3, comp);
}

template <typename Iter, typename Comp>
void sift_down(Iter first, size_t start, size_t end, Comp& comp) {
    size_t root = start;
    while (root * 2 + 1 <= end) {
        size_t child = root * 2 + 1;
        if (child + 1 <= end && comp(*(first + child), *(first + child + 1))) {
            child++;
        }
        if (comp(*(first + root), *(first + child))) {
            std::iter_swap(first + root, first + child);
            root = child;
        } else {
            return;
        }
    }
}

template <typename Iter, typename Comp>
void heapsort_fallback(Iter first, Iter last, Comp& comp) {
    size_t n = static_cast<size_t>(last - first);
    if (n <= 1) return;

    for (int64_t start = static_cast<int64_t>((n - 2) / 2); start >= 0; --start) {
        sift_down(first, static_cast<size_t>(start), n - 1, comp);
    }

    for (size_t end = n - 1; end > 0; --end) {
        std::iter_swap(first, first + end);
        sift_down(first, 0, end - 1, comp);
    }
}

// 3-Way Partitioning (Dutch National Flag / Fat Pivot)
template <typename Iter, typename Comp>
std::pair<Iter, Iter> partition_3way(Iter first, Iter last, Comp& comp) {
    Iter pivot_iter = select_pivot(first, last, comp);
    std::iter_swap(first, pivot_iter);

    auto pivot = *first;
    Iter lt = first;
    Iter gt = last - 1;
    Iter i = first + 1;

    while (i <= gt) {
        if (comp(*i, pivot)) {
            std::iter_swap(lt, i);
            ++lt;
            ++i;
        } else if (comp(pivot, *i)) {
            std::iter_swap(i, gt);
            --gt;
        } else {
            ++i;
        }
    }
    return std::make_pair(lt, gt + 1);
}

template <typename Iter, typename Comp>
void introsort_loop(Iter first, Iter last, size_t depth_limit, Comp& comp) {
    while (static_cast<size_t>(last - first) > INTRO_INSERTION_THRESHOLD) {
        if (depth_limit == 0) {
            heapsort_fallback(first, last, comp);
            return;
        }
        --depth_limit;

        std::pair<Iter, Iter> mid = partition_3way(first, last, comp);
        Iter mid_start = mid.first;
        Iter mid_end = mid.second;

        if (mid_start - first < last - mid_end) {
            introsort_loop(first, mid_start, depth_limit, comp);
            first = mid_end;
        } else {
            introsort_loop(mid_end, last, depth_limit, comp);
            last = mid_start;
        }
    }
    insertion_sort(first, last, comp);
}

template <typename Iter, typename Comp>
void adaptive_introsort_impl(Iter first, Iter last, Comp comp) {
    const size_t n = static_cast<size_t>(last - first);
    if (n <= 1) return;

    if (n <= INTRO_INSERTION_THRESHOLD) {
        insertion_sort(first, last, comp);
        return;
    }

    size_t depth_limit = 2 * compute_log2_floor(n);
    introsort_loop(first, last, depth_limit, comp);
}

// ============================================================================
// 5. ENGINE 3: TIMSORT (Galloping Adaptive Stable Sorting)
// ============================================================================

constexpr size_t TIMSORT_MIN_MERGE = 32;
constexpr size_t TIMSORT_MIN_GALLOP = 7;

inline size_t compute_minrun(size_t n) noexcept {
    size_t r = 0;
    while (n >= 64) {
        r |= (n & 1);
        n >>= 1;
    }
    return n + r;
}

template <typename T>
class TempStorage {
    T* data_ = nullptr;
    size_t capacity_ = 0;
public:
    TempStorage() = default;
    ~TempStorage() {
        if (data_) {
            std::allocator<T> alloc;
            alloc.deallocate(data_, capacity_);
        }
    }
    TempStorage(const TempStorage&) = delete;
    TempStorage& operator=(const TempStorage&) = delete;

    void ensure_capacity(size_t needed) {
        if (needed <= capacity_) return;
        if (data_) {
            std::allocator<T> alloc;
            alloc.deallocate(data_, capacity_);
        }
        std::allocator<T> alloc;
        data_ = alloc.allocate(needed);
        capacity_ = needed;
    }

    T* data() noexcept { return data_; }
};

template <typename Iter, typename Comp>
void binary_insertion_sort(Iter first, Iter last, Iter start, Comp& comp) {
    if (start == first) ++start;
    for (; start < last; ++start) {
        auto val = std::move(*start);
        Iter left = first;
        Iter right = start;
        while (left < right) {
            Iter mid = left + (right - left) / 2;
            if (comp(val, *mid)) {
                right = mid;
            } else {
                left = mid + 1;
            }
        }
        for (Iter p = start; p > left; --p) {
            *p = std::move(*(p - 1));
        }
        *left = std::move(val);
    }
}

template <typename Iter, typename Comp>
size_t count_run_and_make_ascending(Iter first, Iter last, Comp& comp) {
    if (last - first <= 1) return static_cast<size_t>(last - first);

    Iter run_end = first + 2;
    if (comp(*(first + 1), *first)) {
        while (run_end < last && comp(*run_end, *(run_end - 1))) {
            ++run_end;
        }
        std::reverse(first, run_end);
    } else {
        while (run_end < last && !comp(*run_end, *(run_end - 1))) {
            ++run_end;
        }
    }
    return static_cast<size_t>(run_end - first);
}

template <typename RanIt, typename T, typename Comp>
size_t gallop_left(const T& key, RanIt base, size_t len, size_t hint, Comp& comp) {
    size_t last_ofs = 0;
    size_t ofs = 1;
    if (comp(*(base + hint), key)) {
        size_t max_ofs = len - hint;
        while (ofs < max_ofs && comp(*(base + hint + ofs), key)) {
            last_ofs = ofs;
            ofs = (ofs << 1) + 1;
        }
        if (ofs > max_ofs) ofs = max_ofs;
        last_ofs += hint;
        ofs += hint;
    } else {
        size_t max_ofs = hint + 1;
        while (ofs < max_ofs && !comp(*(base + hint - ofs), key)) {
            last_ofs = ofs;
            ofs = (ofs << 1) + 1;
        }
        if (ofs > max_ofs) ofs = max_ofs;
        size_t tmp = last_ofs;
        last_ofs = hint - ofs;
        ofs = hint - tmp;
    }
    ++last_ofs;
    while (last_ofs < ofs) {
        size_t m = last_ofs + (ofs - last_ofs) / 2;
        if (comp(*(base + m), key)) {
            last_ofs = m + 1;
        } else {
            ofs = m;
        }
    }
    return ofs;
}

template <typename RanIt, typename T, typename Comp>
size_t gallop_right(const T& key, RanIt base, size_t len, size_t hint, Comp& comp) {
    size_t last_ofs = 0;
    size_t ofs = 1;
    if (comp(key, *(base + hint))) {
        size_t max_ofs = hint + 1;
        while (ofs < max_ofs && comp(key, *(base + hint - ofs))) {
            last_ofs = ofs;
            ofs = (ofs << 1) + 1;
        }
        if (ofs > max_ofs) ofs = max_ofs;
        size_t tmp = last_ofs;
        last_ofs = hint - ofs;
        ofs = hint - tmp;
    } else {
        size_t max_ofs = len - hint;
        while (ofs < max_ofs && !comp(key, *(base + hint + ofs))) {
            last_ofs = ofs;
            ofs = (ofs << 1) + 1;
        }
        if (ofs > max_ofs) ofs = max_ofs;
        last_ofs += hint;
        ofs += hint;
    }
    ++last_ofs;
    while (last_ofs < ofs) {
        size_t m = last_ofs + (ofs - last_ofs) / 2;
        if (comp(key, *(base + m))) {
            ofs = m;
        } else {
            last_ofs = m + 1;
        }
    }
    return ofs;
}

template <typename Iter, typename Comp>
class TimSortContext {
public:
    using ValueType = typename std::iterator_traits<Iter>::value_type;

    struct Run {
        Iter base;
        size_t len;
    };

    Iter first;
    Comp comp;
    std::vector<Run> stack;
    TempStorage<ValueType> temp_buf;
    size_t min_gallop = TIMSORT_MIN_GALLOP;

    TimSortContext(Iter first, Comp comp) : first(first), comp(comp) {
        stack.reserve(64);
    }

    void push_run(Iter base, size_t len) {
        Run r;
        r.base = base;
        r.len = len;
        stack.push_back(r);
    }

    void merge_collapse() {
        while (stack.size() > 1) {
            size_t n = stack.size() - 2;
            if ((n > 0 && stack[n - 1].len <= stack[n].len + stack[n + 1].len) ||
                (n > 1 && stack[n - 2].len <= stack[n - 1].len + stack[n].len)) {
                if (stack[n - 1].len < stack[n + 1].len) {
                    --n;
                }
                merge_at(n);
            } else if (stack[n].len <= stack[n + 1].len) {
                merge_at(n);
            } else {
                break;
            }
        }
    }

    void merge_force_collapse() {
        while (stack.size() > 1) {
            size_t n = stack.size() - 2;
            if (n > 0 && stack[n - 1].len < stack[n + 1].len) {
                --n;
            }
            merge_at(n);
        }
    }

    void merge_at(size_t i) {
        Iter base1 = stack[i].base;
        size_t len1 = stack[i].len;
        Iter base2 = stack[i + 1].base;
        size_t len2 = stack[i + 1].len;

        stack[i].len = len1 + len2;
        if (i == stack.size() - 3) {
            stack[i + 1] = stack[i + 2];
        }
        stack.pop_back();

        size_t k = gallop_right(*base2, base1, len1, 0, comp);
        base1 += k;
        len1 -= k;
        if (len1 == 0) return;

        len2 = gallop_left(*(base1 + len1 - 1), base2, len2, len2 - 1, comp);
        if (len2 == 0) return;

        if (len1 <= len2) {
            merge_lo(base1, len1, base2, len2);
        } else {
            merge_hi(base1, len1, base2, len2);
        }
    }

    void merge_lo(Iter base1, size_t len1, Iter base2, size_t len2) {
        temp_buf.ensure_capacity(len1);
        ValueType* temp_data = temp_buf.data();

        for (size_t idx = 0; idx < len1; ++idx) {
            ::new (static_cast<void*>(temp_data + idx)) ValueType(std::move(*(base1 + idx)));
        }

        Iter dest = base1;
        ValueType* cursor1 = temp_data;
        ValueType* end1 = temp_data + len1;
        Iter cursor2 = base2;

        *(dest++) = std::move(*(cursor2++));
        if (--len2 == 0) {
            while (cursor1 != end1) {
                *(dest++) = std::move(*(cursor1++));
            }
            for (size_t idx = 0; idx < len1; ++idx) (temp_data + idx)->~ValueType();
            return;
        }
        if (len1 == 1) {
            for (size_t k = 0; k < len2; ++k) {
                *(dest++) = std::move(*(cursor2++));
            }
            *(dest++) = std::move(*cursor1);
            temp_data->~ValueType();
            return;
        }

        while (true) {
            size_t count1 = 0;
            size_t count2 = 0;

            do {
                if (comp(*cursor2, *cursor1)) {
                    *(dest++) = std::move(*(cursor2++));
                    count2++;
                    count1 = 0;
                    if (--len2 == 0) goto outer_lo;
                } else {
                    *(dest++) = std::move(*(cursor1++));
                    count1++;
                    count2 = 0;
                    if (--len1 == 1) goto outer_lo;
                }
            } while ((count1 | count2) < min_gallop);

            do {
                count1 = gallop_right(*cursor2, cursor1, len1, 0, comp);
                if (count1 != 0) {
                    for (size_t k = 0; k < count1; ++k) {
                        *(dest++) = std::move(*(cursor1++));
                    }
                    len1 -= count1;
                    if (len1 <= 1) goto outer_lo;
                }
                *(dest++) = std::move(*(cursor2++));
                if (--len2 == 0) goto outer_lo;

                count2 = gallop_left(*cursor1, cursor2, len2, 0, comp);
                if (count2 != 0) {
                    for (size_t k = 0; k < count2; ++k) {
                        *(dest++) = std::move(*(cursor2++));
                    }
                    len2 -= count2;
                    if (len2 == 0) goto outer_lo;
                }
                *(dest++) = std::move(*(cursor1++));
                if (--len1 == 1) goto outer_lo;

                if (min_gallop > 1) --min_gallop;
            } while (count1 >= TIMSORT_MIN_GALLOP || count2 >= TIMSORT_MIN_GALLOP);

            min_gallop += 2;
        }

    outer_lo:
        if (len1 == 1) {
            for (size_t k = 0; k < len2; ++k) {
                *(dest++) = std::move(*(cursor2++));
            }
            *(dest++) = std::move(*cursor1);
        } else if (len1 > 1) {
            while (cursor1 != end1) {
                *(dest++) = std::move(*(cursor1++));
            }
        }
        for (size_t idx = 0; idx < (size_t)(end1 - temp_data); ++idx) {
            (temp_data + idx)->~ValueType();
        }
    }

    void merge_hi(Iter base1, size_t len1, Iter base2, size_t len2) {
        temp_buf.ensure_capacity(len2);
        ValueType* temp_data = temp_buf.data();

        for (size_t idx = 0; idx < len2; ++idx) {
            ::new (static_cast<void*>(temp_data + idx)) ValueType(std::move(*(base2 + idx)));
        }

        Iter dest = base2 + len2 - 1;
        Iter cursor1 = base1 + len1 - 1;
        ValueType* cursor2 = temp_data + len2 - 1;
        ValueType* base_temp = temp_data;

        *(dest--) = std::move(*(cursor1--));
        if (--len1 == 0) {
            while (cursor2 >= base_temp) {
                *(dest--) = std::move(*(cursor2--));
            }
            for (size_t idx = 0; idx < len2; ++idx) (temp_data + idx)->~ValueType();
            return;
        }
        if (len2 == 1) {
            for (size_t k = 0; k < len1; ++k) {
                *(dest--) = std::move(*(cursor1--));
            }
            *dest = std::move(*cursor2);
            temp_data->~ValueType();
            return;
        }

        while (true) {
            size_t count1 = 0;
            size_t count2 = 0;

            do {
                if (comp(*cursor2, *cursor1)) {
                    *(dest--) = std::move(*(cursor1--));
                    count1++;
                    count2 = 0;
                    if (--len1 == 0) goto outer_hi;
                } else {
                    *(dest--) = std::move(*(cursor2--));
                    count2++;
                    count1 = 0;
                    if (--len2 == 1) goto outer_hi;
                }
            } while ((count1 | count2) < min_gallop);

            do {
                count1 = len1 - gallop_right(*cursor2, base1, len1, len1 - 1, comp);
                if (count1 != 0) {
                    for (size_t k = 0; k < count1; ++k) {
                        *(dest--) = std::move(*(cursor1--));
                    }
                    len1 -= count1;
                    if (len1 == 0) goto outer_hi;
                }
                *(dest--) = std::move(*(cursor2--));
                if (--len2 == 1) goto outer_hi;

                count2 = len2 - gallop_left(*cursor1, base_temp, len2, len2 - 1, comp);
                if (count2 != 0) {
                    for (size_t k = 0; k < count2; ++k) {
                        *(dest--) = std::move(*(cursor2--));
                    }
                    len2 -= count2;
                    if (len2 <= 1) goto outer_hi;
                }
                *(dest--) = std::move(*(cursor1--));
                if (--len1 == 0) goto outer_hi;

                if (min_gallop > 1) --min_gallop;
            } while (count1 >= TIMSORT_MIN_GALLOP || count2 >= TIMSORT_MIN_GALLOP);

            min_gallop += 2;
        }

    outer_hi:
        if (len2 == 1) {
            for (size_t k = 0; k < len1; ++k) {
                *(dest--) = std::move(*(cursor1--));
            }
            *dest = std::move(*cursor2);
        } else if (len2 > 1) {
            while (cursor2 >= base_temp) {
                *(dest--) = std::move(*(cursor2--));
            }
        }
        for (size_t idx = 0; idx < len2; ++idx) {
            (temp_data + idx)->~ValueType();
        }
    }
};

template <typename Iter, typename Comp>
void timsort_impl(Iter first, Iter last, Comp comp) {
    const size_t n = static_cast<size_t>(last - first);
    if (n <= 1) return;

    if (n < TIMSORT_MIN_MERGE) {
        size_t init_run = count_run_and_make_ascending(first, last, comp);
        binary_insertion_sort(first, last, first + init_run, comp);
        return;
    }

    TimSortContext<Iter, Comp> ctx(first, comp);
    size_t minrun = compute_minrun(n);

    Iter cur = first;
    size_t remaining = n;

    while (remaining > 0) {
        size_t run_len = count_run_and_make_ascending(cur, cur + remaining, comp);
        if (run_len < minrun) {
            size_t force_len = std::min(remaining, minrun);
            binary_insertion_sort(cur, cur + force_len, cur + run_len, comp);
            run_len = force_len;
        }

        ctx.push_run(cur, run_len);
        ctx.merge_collapse();

        cur += run_len;
        remaining -= run_len;
    }

    ctx.merge_force_collapse();
}

// Tag dispatching for RadixSort vs TimSort fallback
template <typename Iter, typename Comp>
void dispatch_radix(Iter first, Iter last, Comp /*comp*/, std::true_type) {
    radix_sort_impl(first, last);
}

template <typename Iter, typename Comp>
void dispatch_radix(Iter first, Iter last, Comp comp, std::false_type) {
    timsort_impl(first, last, comp);
}

} // namespace detail

// ============================================================================
// 6. MASTER DISPATCHER & PUBLIC API
// ============================================================================

// Range Overload with explicit comparator
template <typename Iter, typename Comp,
          typename = typename std::iterator_traits<Iter>::value_type>
void sort(Iter first, Iter last, Comp comp, Options options = {}) {
    using ValueType = typename std::iterator_traits<Iter>::value_type;
    const size_t n = static_cast<size_t>(last - first);

    if (n <= 1) return;

    // Fast-path adaptive pre-scan:
    bool is_sorted = true;
    bool is_strictly_reversed = true;
    bool is_non_ascending = true;

    for (Iter it = first; it + 1 != last; ++it) {
        if (comp(*(it + 1), *it)) {
            is_sorted = false;
        }
        if (!comp(*(it + 1), *it)) {
            is_strictly_reversed = false;
        }
        if (comp(*it, *(it + 1))) {
            is_non_ascending = false;
        }
        if (!is_sorted && !is_strictly_reversed && !is_non_ascending) break;
    }

    if (is_sorted) {
        if (options.verbose) {
            std::cout << "[GodSort Adaptive Pre-Scan]: Detected already sorted data. Finished in O(N).\n";
        }
        return;
    }

    constexpr bool is_scalar = std::is_scalar<ValueType>::value;
    if (is_strictly_reversed || (is_non_ascending && (is_scalar || options.stability != StabilityRequirement::ForceStable))) {
        if (options.verbose) {
            std::cout << "[GodSort Adaptive Pre-Scan]: Detected reverse sorted data. Inverted in O(N).\n";
        }
        std::reverse(first, last);
        return;
    }

    Engine chosen_engine = options.engine;

    // Automatic Engine Resolution
    if (chosen_engine == Engine::Auto) {
        constexpr bool can_radix = RadixKeyTraits<ValueType>::is_supported;
        constexpr bool is_std_less = is_default_less<Comp, ValueType>::value;

        if (options.stability == StabilityRequirement::ForceStable) {
            if (can_radix && is_std_less && n >= 64) {
                chosen_engine = Engine::RadixSort;
            } else {
                chosen_engine = Engine::TimSort;
            }
        } else if (options.stability == StabilityRequirement::PreferUnstableFast) {
            if (can_radix && is_std_less && n >= 128) {
                chosen_engine = Engine::RadixSort;
            } else {
                chosen_engine = Engine::AdaptiveIntro;
            }
        } else { // StabilityRequirement::Auto
            if (sort_traits<ValueType>::requires_stability) {
                chosen_engine = Engine::TimSort;
            } else if (can_radix && is_std_less && n >= 128) {
                chosen_engine = Engine::RadixSort;
            } else {
                chosen_engine = Engine::AdaptiveIntro;
            }
        }
    }

    if (options.verbose) {
        std::cout << "[GodSort Engine Dispatched]: ";
        switch (chosen_engine) {
            case Engine::RadixSort: std::cout << "LSD Radix Sort (O(N) Stable Linear)"; break;
            case Engine::TimSort: std::cout << "TimSort (Adaptive Galloping Stable)"; break;
            case Engine::AdaptiveIntro: std::cout << "Adaptive Introsort (Speed In-Place)"; break;
            default: break;
        }
        std::cout << " for size " << n << "\n";
    }

    // Execute Chosen Engine
    switch (chosen_engine) {
        case Engine::RadixSort: {
            detail::dispatch_radix(first, last, comp,
                std::integral_constant<bool, RadixKeyTraits<ValueType>::is_supported>{});
            break;
        }
        case Engine::TimSort: {
            detail::timsort_impl(first, last, comp);
            break;
        }
        case Engine::AdaptiveIntro: {
            detail::adaptive_introsort_impl(first, last, comp);
            break;
        }
        default: {
            detail::adaptive_introsort_impl(first, last, comp);
            break;
        }
    }
}

// Range Overload with default std::less
template <typename Iter,
          typename = typename std::iterator_traits<Iter>::value_type>
void sort(Iter first, Iter last, Options options = {}) {
    using ValueType = typename std::iterator_traits<Iter>::value_type;
    sort(first, last, std::less<ValueType>{}, options);
}

// Container Overload with explicit comparator
template <typename Container, typename Comp,
          typename = decltype(std::begin(std::declval<Container&>())),
          typename = decltype(std::end(std::declval<Container&>())),
          typename = std::enable_if_t<!std::is_same<std::decay_t<Comp>, Options>::value>>
void sort(Container& c, Comp comp, Options options = {}) {
    sort(std::begin(c), std::end(c), comp, options);
}

// Container Overload with default std::less
template <typename Container,
          typename = decltype(std::begin(std::declval<Container&>())),
          typename = decltype(std::end(std::declval<Container&>()))>
void sort(Container& c, Options options = {}) {
    using ValueType = typename std::iterator_traits<decltype(std::begin(c))>::value_type;
    sort(std::begin(c), std::end(c), std::less<ValueType>{}, options);
}

// Specialized entry points:
template <typename... Args>
void stable_sort(Args&&... args) {
    Options opts;
    opts.stability = StabilityRequirement::ForceStable;
    sort(std::forward<Args>(args)..., opts);
}

template <typename... Args>
void fast_sort(Args&&... args) {
    Options opts;
    opts.stability = StabilityRequirement::PreferUnstableFast;
    opts.engine = Engine::AdaptiveIntro;
    sort(std::forward<Args>(args)..., opts);
}

} // namespace godsort

// Global convenience alias
template <typename... Args>
void godsort_fn(Args&&... args) {
    godsort::sort(std::forward<Args>(args)...);
}

#endif // GODSORT_HPP
