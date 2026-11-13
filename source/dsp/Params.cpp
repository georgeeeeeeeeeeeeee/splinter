#include "dsp/Params.h"

#include <cmath>

namespace fault::params
{
float dbToGain (float db) noexcept { return db <= -60.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
} // namespace fault::params
