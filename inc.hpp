#pragma once

#include <atomic>
#include <bits/stdc++.h>
#include <cstdint>
#include <stdexcept>

class IncrementBlocking {
public:
    static inline constexpr size_t N = 9;
    IncrementBlocking() {
        for (int i = 0; i < vec.size(); i++) {
            vec[i].store(0);
        }
    }

    uint64_t fetch_add() {
        return increment();
    }

    void reset(uint64_t desired) {
        // thread safe too
        uint8_t old_value, new_value;
        do {
            do{
                old_value = vec[0].load();
            } while (old_value & 0x1);
            
            new_value = old_value | 0x1;
        } while (!vec[0].compare_exchange_weak(old_value, new_value));


        for (int i = 1; i < vec.size(); i++) vec[i].store(0);
        vec[0].store(0x1);
        
        int ptr = 0;
        for (int i = 0; i < N; i++) {
            if (i == 0) {
                for (int j = 1; j < 8; j++) {
                    uint8_t old = vec[i].load();
                    old |= (1ULL << j) * ((desired >> ptr) & 0x1);
                    vec[i].store(old);
                    ptr++;
                }
            } else {
                for (int j = 0; j < 8; j++) {
                    uint8_t old = vec[i].load();
                    old |= (1ULL << j) * ((desired >> ptr) & 0x1);
                    vec[i].store(old);
                    ptr++;
                    if (ptr == 64) goto label1;
                }
            }
        }
label1:
        old_value = vec[0].load();
        new_value = old_value & ~(0x1);
        vec[0].store(new_value);
    }

    uint64_t get_min() { 
        return 0; 
    }

    uint64_t get_max() {
        return std::numeric_limits<uint64_t>::max();
    }

private:
    std::array<std::atomic<uint8_t>, N> vec;

    uint64_t increment() {
        
        /*
        * LSB(vec[0]) - bit, which indicates lock
        * All other bits are so called valid
        * It's assumed that type is unsigned
        */

        size_t n = vec.size();
        uint8_t old_value, new_value;
        do {
            do{
                old_value = vec[0].load();
            } while (old_value & 0x1);
            
            new_value = old_value | 0x1;
        } while (!vec[0].compare_exchange_weak(old_value, new_value));
        // lock is acqurired by our thread

        uint64_t result = 0;
        uint64_t cur_pow = 1;
        int ptr = 0;
        
        for (int i = 0; i < n; i++) {
            if (i == 0) {
                for (int j = 1; j < 8; j++) {
                    result += ((vec[i] >> j) & 0x1) * cur_pow;
                    cur_pow <<= 1;
                    ptr++;
                }
            } else {
                for (int j = 0; j < 8; j++) {
                    result += ((vec[i] >> j) & 0x1) * cur_pow;
                    cur_pow <<= 1;
                    ptr++;
                    if (ptr == 64) goto label1;
                }
            }
        }  
label1:

        vec[0].fetch_add(2);
        if (vec[0].load() == 0x1) {
            // overflow of vec[0]
            size_t index = 0;       
            do {
                index++;
                if (index == n) break;
                vec[index].fetch_add(1);
            } while (vec[index].load() == 0x0);
        }

        // release lock
        new_value = vec[0].load() & ~(0x1);
        vec[0].store(new_value);

        return result;
    }
};


class IncrementLockFree {
public:
    static inline constexpr uint64_t MAX = 8'388'607;

    IncrementLockFree() {
        high.store(0);
        low.store(0);
    }

    uint64_t fetch_add() {
        return increment_lock_free();
    }

    void reset(uint64_t desired) {
        bool old_value, new_value;
        do {
            do{
                old_value = lock.load();
            } while (old_value);
            
            new_value = true;
        } while (!lock.compare_exchange_weak(old_value, new_value));
        
        desired %= MAX;

        uint32_t val_l = static_cast<uint32_t>(desired & 0x7FF);
        uint32_t val_h = static_cast<uint32_t>((desired >> 11) & 0xFFF);
        low.store(val_l << 21);
        high.store(val_h << 20);

        lock.store(false);
    }

    uint64_t get_min() { 
        return 0; 
    }

    uint64_t get_max() {
        return MAX;
    }

private:
    std::atomic_bool lock; // only for reset method
    std::atomic<uint32_t> high;
    std::atomic<uint32_t> low;
    using T = uint32_t;

    T get_version(T num) {
        return num & 0xFFFFF; // get first 20 bits.
    }

    T get_value_low(T num) {
        return num >> 21;
    }

    bool is_overflow(T num) {
        return (num >> 20) & 0x1;
    }

    T set_overflow(T num) {
        return num | (1 << 20);
    }

    T unset_overflow(T num) {
        return num & ~(1 << 20);
    }

    bool equal_version(T val1, T val2) {
        // takes 2 values, extracts their versions
        // true - if versions are same
        return get_version(val1) == get_version(val2);
    }

    T increment_u64(T num) {
        T version = get_version(num);
        version = (version + 1) & 0xFFFFF;
        num += (1 << 21);
        if (get_value_low(num) == 0x0) {
            num |= (1 << 20); // overflow set
        }
        return (num & ~0xFFFFF) | version;
    }

    T increment_high_with_ver(T num, T ver) {
        ver = ver & 0xFFFFF;
        num += (1 << 20);
        return (num & ~0xFFFFF) | ver;
    } 


    bool is_snaps_equal(const T& high1, const T& low1, 
                        const T& high2, const T& low2) {
        return (high1 == high2 && low1 == low2);
    }

    std::pair<T,T> get_lf_snapshot(std::atomic<T>& high, std::atomic<T>& low) {
        // make lock free snapshot
        T low1, high1, low2, high2;

        high1 = high.load(); low1 = low.load();
        high2 = high.load(); low2 = low.load();

        while (!is_snaps_equal(high1, low1, high2, low2)) {
            std::swap(high1, high2);
            std::swap(low1, low2);
            high2 = high.load();
            low2 = low.load();
        }

        return {high2, low2};
    }

    void printt_high(T val) {
        for (int i = 0; i < 20; i++) {
            std::cout << ((val >> i) & 0x1);
        }
        std::cout << "|";
        for (int i = 0; i < 12; i++) {
            std::cout << ((val >> (i + 20)) & 0x1);
        }
    }
    void printt_low(T val) {
        for (int i = 0; i < 20; i++) {
            std::cout << ((val >> i) & 0x1);
        }
        std::cout << "|" << ((val >> 20) & 0x1) << "|" ;
        for (int i = 0; i < 11; i++) {
            std::cout << ((val >> (i + 21)) & 0x1);
        }
    }


    uint64_t increment_lock_free() {
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

        uint64_t result = old_low >> 21;
        result |= ((static_cast<uint64_t>(old_high) >> 20) << 11);
        return result;
    }

};