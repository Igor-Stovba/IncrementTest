#include <bits/stdc++.h>
#include <cstdint>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>

using namespace std;
template<class Key, class Value> using um = unordered_map<Key, Value>;
using pi = pair<int, int>;
using pc = pair<char, char>;
using pi6 = pair<int64_t, int64_t>;
using pll = pair<long long, long long>;
using ull = unsigned long long;
using ll = long long;
using i6 = int64_t;
const int MOD = 1e9 + 7;

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            std::cerr << "ASSERTION FAILD: " << #expected << " == " << #actual << "\n" \
                      << "  Expected: " << (expected) << "\n" \
                      << "  Actual:   " << (actual) << std::endl; \
            assert((expected) == (actual)); \
        } \
    } while (0)

#define fastio() ios_base::sync_with_stdio(false);cin.tie(NULL);cout.tie(NULL)

namespace {
    template<class T>
    T get_version(T num) {
        return num & 0xFFFFF; // get first 20 bits.
    }
    template<class T>
    T get_value_low(T num) {
        return num >> 21;
    }
    template<class T>
    bool is_overflow(T num) {
        return (num >> 20) & 0x1;
    }
    template<class T>
    T set_overflow(T num) {
        return num | (1 << 20);
    }
    template<class T>
    T unset_overflow(T num) {
        return num & ~(1 << 20);
    }
    template<class T>
    bool equal_version(T val1, T val2) {
        // takes 2 values, extracts their versions
        // true - if versions are same
        return get_version(val1) == get_version(val2);
    }

    template<class T>
    T increment_u64(T num) {
        T version = get_version(num);
        version = (version + 1) & 0xFFFFF;
        num += (1 << 21);
        if (get_value_low(num) == 0x0) {
            num |= (1 << 20); // overflow set
        }
        return (num & ~0xFFFFF) | version;
    }

    template<class T>
    T increment_high_with_ver(T num, T ver) {
        ver = ver & 0xFFFFF;
        num += (1 << 20);
        return (num & ~0xFFFFF) | ver;
    } 

    template<class T>
    bool is_snaps_equal(const T& high1, const T& low1, 
                        const T& high2, const T& low2) {
        return (high1 == high2 && low1 == low2);
    }

    template<class T>
    pair<T,T> get_lf_snapshot(atomic<T>& high, atomic<T>& low) {
        // make lock free snapshot
        T low1, high1, low2, high2;

        high1 = high.load(); low1 = low.load();
        high2 = high.load(); low2 = low.load();

        while (!is_snaps_equal<T>(high1, low1, high2, low2)) {
            std::swap(high1, high2);
            std::swap(low1, low2);
            high2 = high.load();
            low2 = low.load();
        }

        return {high2, low2};
    }

    template<class T>
    bool will_overflow(T a, T b) {
        return b > (numeric_limits<T>::max() - a);
    }
}

template<class T>
uint64_t increment(std::vector<std::atomic<T>>& vec) {
    /*
    * LSB(vec[0]) - bit, which indicates lock
    * All other bits are so called valid
    * It's assumed that type is unsigned
    */

    assert(!vec.empty());
    size_t n = vec.size();
    size_t nbits = sizeof(T) * 8;

    T old_value, new_value;
    do {
        do{
            old_value = vec[0].load();
        } while (old_value & 0x1);
        
        new_value = old_value | 0x1;
    } while (!vec[0].compare_exchange_weak(old_value, new_value));
    // lock is acqurired by our thread

    vec[0].fetch_add(2);

    if (vec[0].load() == 0x1) {
        // overflow of vec[0]
        size_t index = 0;       
        do {
            index++;
            assert(index != n); // General overflow
            vec[index].fetch_add(1);
        }while (vec[index].load() == 0x0);
    }

    uint64_t result = 0;
    uint64_t cur_pow = 1;
    
    for (int i = 0; i < vec.size(); i++) {
        if (i == 0) {
            for (int j = 1; j < nbits; j++) {
                if ((((vec[i] >> j) & 0x1) == 0x1) && will_overflow(result, cur_pow)) 
                    throw runtime_error("The result doesn't fit into uint64_t");
                
                result += ((vec[i] >> j) & 0x1) * cur_pow;
                cur_pow <<= 1;
            }
        } else {
            for (int j = 0; j < nbits; j++) {
                if ((((vec[i] >> j) & 0x1) == 0x1) && will_overflow(result, cur_pow)) 
                    throw runtime_error("The result doesn't fit into uint64_t");
                
                result += ((vec[i] >> j) & 0x1) * cur_pow;
                cur_pow <<= 1;
            }
        }
    }  

    // release lock
    new_value = vec[0].load() & ~(0x1);
    vec[0].store(new_value);

    return result;
}


template<class T>
void thread_func_blocking(std::vector<std::atomic<T>>& vec, uint32_t n, mutex& mtx, vector<uint64_t>& actual_ids) {
    vector<uint64_t> local_ids;

    for (uint64_t i = 0; i < n; i++) {
        uint64_t id = increment<T>(vec);
        local_ids.push_back(id);
    }

    {
        lock_guard<mutex> lock(mtx);
        actual_ids.insert(end(actual_ids),begin(local_ids), end(local_ids));
    }
}


void test_blocking() {
    std::random_device rd;  
    std::mt19937 gen(rd()); 

    std::uniform_int_distribution<> distrib(5, 8'000'000); 
    const size_t THREADS = 24; 
    size_t counter = 0;

    // for checking id
    std::mutex mtx;
    std::vector<uint64_t> actual_ids;

    while (true) {
        /*
        "sz" causes changes in right bound in distrib!
        */
        const size_t sz = 9; 
        using Type = uint8_t;
        size_t nbits = sizeof(Type) * 8;
        vector<atomic<Type>> arr(sz);
        uint32_t rand_num = distrib(gen);

        assert((nbits * sz) <= (sizeof(uint64_t) * 8));

        std::vector<std::thread> threads;
        actual_ids.clear(); 

        cout << "step: " << counter++ << ", num_inc: " << (THREADS * rand_num) << endl;

        for (int i = 0; i < THREADS; ++i) {
            threads.emplace_back(thread_func_blocking<Type>, ref(arr), rand_num, ref(mtx), ref(actual_ids));
        }

        for (auto& t : threads) t.join();

        // Assert that all ids are present and exaclty one time
        sort(begin(actual_ids), end(actual_ids));
        for (uint64_t i = 1; i <= (THREADS * rand_num); i++) {
            ASSERT_EQ(actual_ids[i-1], i);
        }
    }
}

template<class T>
uint64_t increment_lock_free(atomic<T>& high, atomic<T>& low) {
    /*
    * The lowest 20 bits are so called version. Tagged approach.
    * And "low" has 21-th bit which means is_overflow.
    */
    T new_low, new_high, version;
retry:

    auto [old_high, old_low] = get_lf_snapshot(high, low);
    if (is_overflow(old_low)) {
        // we must help 
        
        if (equal_version(old_low, old_high)) {
            /*
            * If so, that means that some thread has already incremented high 
            * but he hadn't time to reset 21-th bit by low
            */
            low.compare_exchange_weak(old_low, unset_overflow(old_low));
        } else {
            /*
            * Since the version isn't same 
            * we are sure that nobody incremented "high" via overflowing.
            * But by incrementing high we should set "low"'s version to save an invariant
            */
            version = get_version(old_low);
            bool ok = high.compare_exchange_weak(
                old_high, 
                increment_high_with_ver(old_high, version));
            if (ok) {
                /*
                * We can try to unset overflow bit in low
                * if we fails, doesb't matter since thread collaboration
                */
                low.compare_exchange_weak(old_low, unset_overflow(old_low));
            }
        }
        goto retry;
    } else {

        new_low = increment_u64(old_low);
        if (!low.compare_exchange_weak(old_low, new_low)) 
            goto retry; // fail
    }
    // if we are here - success

    if (is_overflow(new_low)) old_high += (1ULL << 20);
    uint64_t result = new_low >> 21;

    result |= ((static_cast<uint64_t>(old_high) >> 20) << 11);
    return result;
}

template<class T>
void thread_func_lf(atomic<T>& high, atomic<T>& low, uint32_t n, mutex& mtx, vector<uint64_t>& actual_ids){
    vector<uint64_t> local_ids;

    for (uint64_t i = 0; i < n; i++) {
        uint64_t id = increment_lock_free(high, low);
        local_ids.push_back(id);
    }

    {
        lock_guard<mutex> lock(mtx);
        actual_ids.insert(end(actual_ids),begin(local_ids), end(local_ids));
    }
}

void test_lf() {
    std::random_device rd;  
    std::mt19937 gen(rd()); 

    std::uniform_int_distribution<> distrib(42, 345'000);
    const size_t THREADS = 24; 
    size_t counter = 0;

    // for checking id
    std::mutex mtx;
    std::vector<uint64_t> actual_ids;

    while (true) { 
        atomic<uint32_t> low = 0;
        atomic<uint32_t> high = 0;
        uint32_t rand_num = distrib(gen);
        actual_ids.clear();

        cout << "step: " << counter++ << ", num_inc: " << (THREADS * rand_num) << endl;

        vector<thread> threads;
        for (int i = 0; i < THREADS; ++i) {
            threads.emplace_back(thread_func_lf<uint32_t>, ref(high), ref(low), rand_num, ref(mtx), ref(actual_ids));
        }
        for (auto& t : threads) t.join();

        // Assert that all ids are present and exaclty one time
        sort(begin(actual_ids), end(actual_ids));
        for (uint64_t i = 1; i <= (THREADS * rand_num); i++) {
            ASSERT_EQ(actual_ids[i-1], i);
        }
    }
}

int main() {

/*
    increment(...) - blocking version
    increment_lock_free() - lock-free version (at least I hope)
How to test?
    Uncomment desired version below and "g++ main.cpp -o main && ./main"
*/
    
/*
* Start testing blocking version -----------------------
*/
    // test_blocking();
/*
* End testing blocking version -----------------------
*/


/*
* Start testing lock free version -----------------------
*/
    // test_lf();
/*
* End testing lock free version -----------------------
*/
    return 0;
}