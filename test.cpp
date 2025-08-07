#include <atomic>
#include <mutex>
#include <unordered_map>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#define DUMMY_SOLUTION 0
#if DUMMY_SOLUTION
std::atomic<uint32_t> low;
std::atomic<uint32_t> high;

std::mutex m; // forbidden according to task

uint64_t fetch_add() {
  uint64_t result = 0;
  m.lock();
  result = uint64_t(low.load()) | (uint64_t(high.load()) << 32);
  low++;
  if (low.load() == 0) {
    high++;
  }
  m.unlock();
  return result;
}

// not thread safe.
void reset(uint64_t desired) {
  uint64_t max_int_32 = std::numeric_limits<uint32_t>::max();
  low.store(desired & max_int_32);
  high.store((desired >> 32) & max_int_32);
}

uint64_t get_min() { return 0; }

uint64_t get_max() {
  return std::numeric_limits<uint64_t>::max();
  ;
}

#endif // DUMMY_SOLUTION


#define BLOCKING_SOLUTION 0
#if BLOCKING_SOLUTION

/*
    range: [0, uint64_t::max()]
    the lower part of this counter overflows by 127 
    because an array of uint8_t is used and 
    the first bit in the first number is a lock.
*/

#include "inc.hpp"
IncrementBlocking digit;

uint64_t fetch_add() {
    return digit.fetch_add();
}

void reset(uint64_t desired) {
    digit.reset(desired);
}

uint64_t get_min() { 
    return digit.get_min(); 
}

uint64_t get_max() {
    return digit.get_max();
}

#endif // BLOCKING_SOLUTION 


#define LOCK_FREE_SOLUTION 1
#if LOCK_FREE_SOLUTION

/*
    range: [0, 8'388'607]
    the lower part of this counter overflows by 2048.
*/

#include "inc.hpp"
IncrementLockFree digit;

uint64_t fetch_add() {
    return digit.fetch_add();
}

void reset(uint64_t desired) {
    digit.reset(desired);
}

uint64_t get_min() { 
    return digit.get_min(); 
}

uint64_t get_max() {
    return digit.get_max();
}

#endif // LOCK_FREE_SOLUTION


#if LOCK_FREE_SOLUTION

TEST_CASE("reset + returt of single fetch_add") {
  for (uint64_t i = 1; i < 22; i++) {
    const uint64_t middle = (1UL << i);
    const uint64_t delta = std::min(uint64_t(100), middle / 2);
    const uint64_t min = middle - delta;
    const uint64_t max = middle + delta;

    // test in range [min, max)
    uint64_t j = min;
    reset(j);
    for (; j < max; j++) {
      REQUIRE(j == fetch_add());
    }
  }
}

TEST_CASE("total overflow test") {
  reset(get_max() - 1);
  REQUIRE(get_max() - 1 == fetch_add());
  REQUIRE(get_max() == fetch_add());
  REQUIRE(get_min() == fetch_add());
  REQUIRE(get_min() + 1 == fetch_add());
}


#else 

TEST_CASE("reset + returt of single fetch_add") {
  for (uint64_t i = 1; i < 63; i++) {
    const uint64_t middle = (1UL << i);
    const uint64_t delta = std::min(uint64_t(100), middle / 2);
    const uint64_t min = middle - delta;
    const uint64_t max = middle + delta;

    // test in range [min, max)
    uint64_t j = min;
    reset(j);
    for (; j < max; j++) {
      REQUIRE(j == fetch_add());
    }
  }
}

TEST_CASE("total overflow test") {
  reset(get_max() - 1);
  REQUIRE(get_max() - 1 == fetch_add());
  REQUIRE(get_max() == fetch_add());
  REQUIRE(get_min() == fetch_add());
  REQUIRE(get_min() + 1 == fetch_add());
}

#endif
