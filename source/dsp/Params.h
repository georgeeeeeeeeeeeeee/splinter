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

// Stage 3. seed: 0 = a new random seed each session; any other value makes Macro mode reproducible.
inline constexpr std::string_view stereoModeId = "stereoMode", seedId = "seed";
inline constexpr std::string_view kStereoModes[] = { "Unique", "Shared" };
inline constexpr int kMaxSeed = 99999;

// Stage 4. corrupt 0..1: 0 is off (for DJ Filter the centre is off). corruptSet "Original 3" plays only
// Decimate, Dropout and Destroy (DJ Filter and Vinyl fall back to Decimate).
inline constexpr std::string_view corruptId = "corrupt", corruptTypeId = "corruptType", corruptSetId = "corruptSet";
inline constexpr std::string_view kCorruptTypes[] = { "Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl" };
inline constexpr std::string_view kCorruptSets[] = { "All", "Original 3" };

// Stage 5: modulation (brief §3.10). Per modulator n = 1..4: mod<n>Shape, mod<n>Rate (free, Hz),
// mod<n>Sync ("Free" or a synced length), mod<n>Phase, mod<n>Polarity. Per route n = 1..8:
// route<n>Source, route<n>Dest, route<n>Depth (-100..+100%). Per CC lane: cc<n>Polarity (the CC
// numbers and gate notes are plug-in state, learnable, not parameters).
inline constexpr std::string_view bendAttenId = "bendAtten", breakAttenId = "breakAtten", corruptAttenId = "corruptAtten";
inline constexpr std::string_view gateModeId = "gateMode", corruptAsResetId = "corruptAsReset";
// Stage 7: the output safety clipper, on by default.
inline constexpr std::string_view safetyClipId = "safetyClip";
// Output trim (dry and wet), before the clipper: lets presets arrive at a matched loudness.
inline constexpr FloatSpec output { "output", "Output", -24.0f, 12.0f, 0.0f, "dB" };
inline constexpr std::string_view envAttackId = "envAttack", envReleaseId = "envRelease", envGainId = "envGain";
inline constexpr std::string_view kGateModes[] = { "Latching", "Momentary" };
inline constexpr std::string_view kModShapes[] = { "Sine", "Triangle", "Saw", "Square", "S&H", "Smooth Random" };
inline constexpr std::string_view kPolarities[] = { "Unipolar", "Bipolar" };
inline constexpr std::string_view kModSources[] = { "None", "Mod 1", "Mod 2", "Mod 3", "Mod 4", "Env", "CC 1", "CC 2", "CC 3", "CC 4" };
inline constexpr std::string_view kModDests[] = { "None", "Time", "Repeats", "Mix", "Bend", "Break", "Corrupt" };
inline constexpr std::string_view kGateNames[] = { "Freeze", "Bend", "Break", "Corrupt", "Reset", "Note clock", "Break bar" };
// C1 .. F1 (Live's numbering, C3 = 60). Break bar has no note until you learn one (-1): any default
// would steal a pitch from someone's existing set.
inline constexpr int kDefaultGateNotes[] = { 36, 37, 38, 39, 40, 41, -1 };
inline constexpr int kDefaultCcNumbers[] = { 1, 16, 17, 18 };            // mod wheel, then general purpose 1-3

// Break this bar: fillTrigger fires on its rising edge (a button, automation or a MIDI-mapped key).
inline constexpr std::string_view fillTriggerId = "fillTrigger", fillLengthId = "fillLength", fillEveryId = "fillEvery";
inline constexpr FloatSpec fillAmount { "fillAmount", "Break Amount", 0.0f, 100.0f, 60.0f, "%" };
inline constexpr FloatSpec fillChance { "fillChance", "Break Chance", 0.0f, 100.0f, 50.0f, "%" };
inline constexpr std::string_view kFillLengths[] = { "1 beat", "2 beats", "1 bar", "2 bars" };
inline constexpr std::string_view kFillEvery[] = { "By hand", "Every 2 bars", "Every 4 bars", "Every 8 bars", "Every 16 bars" };
inline constexpr int kFillEveryBars[] = { 0, 2, 4, 8, 16 };

inline constexpr int kStateVersion = 9;   // 7: safetyClip (Stage 7); 8: output trim; 9: Break this bar

float dbToGain (float db) noexcept;
} // namespace fault::params
