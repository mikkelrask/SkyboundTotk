#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rx {
// Match the bounded page-range tracking used by the native visibility experiment.
// These are hints, not allocation identities: reuse may produce false positives.
// Deliberately retain the first 16 distinct small ranges; no cache eviction here.
class CpuReadRanges {
    struct Range { uint64_t begin, end; };
    std::array<Range, 16> ranges{};
    size_t used = 0;
public:
    void note(uint64_t address, uint64_t size) {
        constexpr uint64_t mask = 4095;
        if (!size || address > std::numeric_limits<uint64_t>::max() - size) return;
        const uint64_t end = address + size;
        if (end > std::numeric_limits<uint64_t>::max() - mask) return;
        const uint64_t lo = address & ~mask, hi = (end + mask) & ~mask;
        if (hi <= lo || hi - lo > (256u << 10)) return;
        for (size_t i = 0; i < used; ++i)
            if (ranges[i].begin == lo && ranges[i].end == hi) return;
        if (used < ranges.size()) ranges[used++] = {lo, hi};
    }
    bool overlaps(uint64_t address, uint64_t size) const {
        if (!size || address > std::numeric_limits<uint64_t>::max() - size) return false;
        for (size_t i = 0; i < used; ++i)
            if (ranges[i].begin < address + size && address < ranges[i].end) return true;
        return false;
    }
};
} // namespace rx
