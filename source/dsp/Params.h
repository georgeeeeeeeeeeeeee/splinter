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

// Stage 1.
inline constexpr FloatSpec time { "time", "Time", 0.0f, 1.0f, 0.5f, "" };          // exp. 16 s -> 80 Hz, or div/mult
inline constexpr FloatSpec repeats { "repeats", "Repeats", 1.0f, 32.0f, 1.0f, "" }; // exp. knob curve, whole steps
inline constexpr FloatSpec mix { "mix", "Mix", 0.0f, 100.0f, 50.0f, "%" };
inline constexpr FloatSpec windowing { "windowing", "Windowing", 0.0f, 100.0f, 2.0f, "%" };
inline constexpr FloatSpec width { "width", "Width", 0.0f, 100.0f, 50.0f, "%" };      // 50% = as recorded
inline constexpr std::string_view freezeId = "freeze", resetId = "reset", clockSourceId = "clockSource", freezeButtonModeId = "freezeButtonMode";
inline constexpr std::string_view kClockSources[] = { "Internal", "Host", "Note" };   // Note: Stage 5
inline constexpr std::string_view kFreezeModes[] = { "Latching", "Momentary" };

// Stage 2. The Bend / Break toggles are stored per mode (§3.7), as separate parameters so each has
// its own automation lane: the UI's single button acts on the current mode's one.
inline constexpr std::string_view modeId = "mode", bendId = "bend", breakId = "break", noteToSpeedId = "noteToSpeed";
inline constexpr std::string_view bendOnMacroId = "bendOnMacro", bendOnMicroId = "bendOnMicro";
inline constexpr std::string_view breakOnMacroId = "breakOnMacro", breakOnMicroId = "breakOnMicro";
inline constexpr std::string_view kModes[] = { "Macro", "Micro" };

inline constexpr int kStateVersion = 3;

float dbToGain (float db) noexcept;
} // namespace fault::params
