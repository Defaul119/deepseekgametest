// Small deterministic RNG (xorshift64*). Fixed seeds reproduce fire
// animation, AI noise and test cases exactly across runs and platforms.
#pragma once

#include <cstdint>

namespace rf {

struct Rng {
    uint64_t state;

    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : state(seed ? seed : 1) {}

    uint64_t nextU64() {
        uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 0x2545F4914F6CDD1Dull;
    }

    uint32_t nextU32() { return static_cast<uint32_t>(nextU64() >> 32); }

    // Uniform in [0, n). n must be non-zero.
    uint32_t below(uint32_t n) { return n ? nextU32() % n : 0; }

    // Uniform in [lo, hi] inclusive.
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1)));
    }
};

}  // namespace rf
