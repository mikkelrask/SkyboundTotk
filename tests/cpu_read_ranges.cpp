#include "../a64-kit/gpu/render/rx_cpu_read_ranges.h"
#include <cassert>
#include <cstdio>
int main() {
    rx::CpuReadRanges ranges;
    assert(!ranges.overlaps(0x1000, 4));
    ranges.note(0x1003, 4);
    assert(ranges.overlaps(0x1000, 1));
    assert(ranges.overlaps(0x1fff, 2));
    assert(!ranges.overlaps(0x2000, 1));
    assert(!ranges.overlaps(0xfff, 1));
    assert(!ranges.overlaps(0x1000, 0));
    for (int i=0;i<100;i++) ranges.note(0x1003, 4); // duplicates don't exhaust the cap
    for (uint64_t i=2;i<=16;i++) ranges.note(i*0x1000, 1);
    assert(ranges.overlaps(0x10000, 1));
    ranges.note(0x11000, 1);
    assert(!ranges.overlaps(0x11000, 1));
    rx::CpuReadRanges limits;
    limits.note(0x100000, 256u<<10);
    assert(limits.overlaps(0x13ffff, 1));
    limits.note(0x200001, 256u<<10); // page rounding exceeds the limit
    assert(!limits.overlaps(0x200001, 1));
    limits.note(UINT64_MAX-1, 4);
    limits.note(UINT64_MAX-2, 1);
    assert(!limits.overlaps(UINT64_MAX-1, 4));
    assert(!limits.overlaps(0, 1));
    std::puts("PASS: page ranges, overlap boundaries, duplicates, cap, size and overflow");
}
