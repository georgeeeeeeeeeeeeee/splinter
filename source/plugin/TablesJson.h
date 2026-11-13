#pragma once
// MacroTables <-> JSON (tuning/macro_tables.json) and the C++ baker. JUCE, so used by the plug-in and
// the render CLI, never by /dsp. Generic over MacroTables::fields(): keys are the field names, arrays
// of per-zone weights are written one row per zone, and keys starting with "_" are notes (ignored).

#include "dsp/MacroTables.h"

#include <juce_core/juce_core.h>

namespace fault
{
// Starts from `into` (normally the defaults) and overwrites every key present. Unknown keys, wrong
// sizes and non-numbers are errors: `into` is untouched and `error` says what's wrong.
bool tablesFromJson (const juce::String& text, dsp::MacroTables& into, juce::String& error);
juce::String tablesToJson (const dsp::MacroTables& t);
juce::String tablesToCpp (const dsp::MacroTables& t);   // MacroTablesBaked.inc
} // namespace fault
