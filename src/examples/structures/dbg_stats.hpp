#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>

enum class SampleTag : std::uint8_t {
  Raw = 0,
  Filtered = 1,
};

struct Sample {
  SampleTag tag = SampleTag::Raw;
  std::vector<std::uint64_t> values;
};

struct Stats {
  std::uint64_t min = 0;
  std::uint64_t max = 0;
  std::uint64_t mean = 0;
  std::uint64_t count = 0;
};

class DbgStats {
  public:
    static Stats summarize(Sample sample) {
      Stats out;
      out.count = sample.values.size();
      if (sample.values.empty()) {
        return out;
      }
      out.min = *std::min_element(sample.values.begin(), sample.values.end());
      out.max = *std::max_element(sample.values.begin(), sample.values.end());
      std::uint64_t sum = 0;
      for (auto v : sample.values) {
        sum += v;
      }
      out.mean = sum / out.count;
      return out;
    }
};
