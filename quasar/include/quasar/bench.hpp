#pragma once

#include <cstddef>
#include <cstdint>

namespace quasar {

struct BenchResult {
    int depth = 0;
    std::uint64_t nodes = 0;
    int timeMs = 0;
};

BenchResult runBench(int depth = 8, std::size_t hashMb = 64);

}  // namespace quasar
