#pragma once
// Parameter IDs, ranges and defaults (brief §4). Pure C++: the plugin layer builds APVTS params from
// these. IDs are saved in hosts' sets and automation: never rename one once shipped.

#include <string_view>

namespace fault::params
{
struct FloatSpec
{
    std::string_view id, name;
    float min, max, def;
    std::string_view unit;
};

// Stage 0: the host check's only parameter.
inline constexpr FloatSpec gain { "gain", "Gain", -60.0f, 12.0f, 0.0f, "dB" };

inline constexpr int kStateVersion = 1;

float dbToGain (float db) noexcept;
} // namespace fault::params
