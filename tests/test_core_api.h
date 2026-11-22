#pragma once
#include <cstdint>

// Test-only observations from the synthetic core. Snapshots are copied under
// a lock; the runner itself uses only the standard libretro entry points.
struct TestCoreSnapshot {
    unsigned boots = 0, resets = 0, saves = 0, loads = 0;
    uint64_t runs = 0, loaded_value = 0;
    uint64_t frame_time_calls = 0;
    int64_t frame_time = 0;
    bool fast_forward = false;
    unsigned throttle = 0;
    char resolution[24]{}, dual_core[24]{}, renderer[24]{}, rsp[24]{};
};
using TestCoreRead = TestCoreSnapshot(*)();
using TestCoreObserve = void(*)(void(*)());
