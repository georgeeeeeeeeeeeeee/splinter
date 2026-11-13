#pragma once
// Presets: factory ones compiled in (presets/*.json via juce_add_binary_data) plus the user's own, saved
// as the same JSON in the user's application-data folder. A preset lists parameter values by ID in the
// parameter's own units (choices by name); anything it doesn't list goes back to its default, so a
// preset always sounds the same whatever was loaded before.
//
//   { "name": "Worn Reel", "category": "Tape", "about": "...", "params": { "time": 0.5, "corruptType": "Vinyl", ... } }

#include <juce_audio_processors/juce_audio_processors.h>

namespace fault
{
struct PresetInfo
{
    juce::String name, category, about, json;
    bool user = false;
    juce::File file;
};

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) { rescan(); }

    static const juce::StringArray& categories();   // factory categories in order, then "User"
    static juce::File userFolder();

    void rescan();
    const std::vector<PresetInfo>& presets() const noexcept { return list; }
    bool load (int index, juce::String& error);    // message thread
    bool save (const juce::String& name, juce::String& error);
    int current() const noexcept { return currentIndex; }
    juce::String currentName() const { return name; }
    void setCurrentName (const juce::String& n);   // restoring a session: find it in the list
    std::function<void()> onLoad;                   // after a load (e.g. clear gate flips)

    // Strict: unknown keys or bad values are errors (the check tool); the plug-in reports and goes on.
    static bool apply (juce::AudioProcessorValueTreeState&, const juce::var& preset, juce::String& error, bool strict);
    juce::String toJson (const juce::String& name, const juce::String& category) const;

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<PresetInfo> list;
    int currentIndex = -1;
    juce::String name { "Init" };
};
} // namespace fault
