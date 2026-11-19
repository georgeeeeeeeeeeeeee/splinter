#include "plugin/Presets.h"

#include "FaultPresetData.h"

namespace fault
{
const juce::StringArray& PresetManager::categories()
{
    static const juce::StringArray c { "Repeat", "Stutter", "Skip", "Gate", "Tape", "Pitch", "Lo-Fi", "Texture", "Ruin", "Perform", "User" };
    return c;
}

juce::File PresetManager::userFolder()
{
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto folder = appData.getChildFile ("Splinter").getChildFile ("Presets");
    // Splinter was called Fault before release: bring presets saved under the old name across once.
    const auto old = appData.getChildFile ("Fault").getChildFile ("Presets");
    if (! folder.exists() && old.isDirectory())
        old.copyDirectoryTo (folder);
    return folder;
}

void PresetManager::rescan()
{
    list.clear();
    const auto add = [this] (const juce::String& text, bool user, const juce::File& f) {
        const auto v = juce::JSON::parse (text);
        if (! v.isObject()) return;
        PresetInfo p { v["name"].toString(), user ? "User" : v["category"].toString(), v["about"].toString(), text, user, f };
        if (p.name.isNotEmpty()) list.push_back (std::move (p));
    };
    for (int i = 0; i < FaultPresetData::namedResourceListSize; ++i)
    {
        int size = 0;
        if (const char* data = FaultPresetData::getNamedResource (FaultPresetData::namedResourceList[i], size))
            add (juce::String::fromUTF8 (data, size), false, {});
    }
    for (const auto& f : userFolder().findChildFiles (juce::File::findFiles, false, "*.json"))
        add (f.loadFileAsString(), true, f);
    const auto& cats = categories();
    std::stable_sort (list.begin(), list.end(), [&cats] (const PresetInfo& a, const PresetInfo& b) {
        const int ca = cats.indexOf (a.category), cb = cats.indexOf (b.category);
        if (ca != cb) return (ca < 0 ? 99 : ca) < (cb < 0 ? 99 : cb);
        return a.user ? a.name.compareIgnoreCase (b.name) < 0 : false;   // factory: file order
    });
    setCurrentName (name);
}

void PresetManager::setCurrentName (const juce::String& n)
{
    name = n;
    currentIndex = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == n) currentIndex = i;
}

bool PresetManager::apply (juce::AudioProcessorValueTreeState& s, const juce::var& preset, juce::String& error, bool strict)
{
    const auto params = preset["params"];
    auto* obj = params.getDynamicObject();
    if (obj == nullptr)
    {
        error = "no \"params\" object";
        return false;
    }
    // Check every key first, so a bad preset changes nothing in strict mode.
    for (const auto& prop : obj->getProperties())
        if (s.getParameter (prop.name.toString()) == nullptr)
        {
            error = "unknown parameter '" + prop.name.toString() + "'";
            if (strict) return false;
        }
    for (auto* p : s.processor.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (r == nullptr || r->getParameterID() == "reset")
            continue;
        float norm = r->getDefaultValue();
        if (obj->hasProperty (r->getParameterID()))
        {
            const auto v = obj->getProperty (r->getParameterID());
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r); c != nullptr && v.isString())
            {
                const int i = c->choices.indexOf (v.toString(), true);
                if (i < 0)
                {
                    error = r->getParameterID() + ": no choice '" + v.toString() + "'";
                    if (strict) return false;
                    continue;
                }
                norm = r->convertTo0to1 ((float) i);
            }
            else if (v.isBool())
                norm = (bool) v ? 1.0f : 0.0f;
            else if (v.isDouble() || v.isInt() || v.isInt64())
            {
                const float x = (float) (double) v;
                const auto range = r->getNormalisableRange();
                if (x < range.start - 1.0e-4f || x > range.end + 1.0e-4f)
                {
                    error = r->getParameterID() + ": " + juce::String (x) + " is out of range";
                    if (strict) return false;
                }
                norm = r->convertTo0to1 (juce::jlimit (range.start, range.end, x));
            }
            else
            {
                error = r->getParameterID() + ": not a value";
                if (strict) return false;
                continue;
            }
        }
        r->setValueNotifyingHost (norm);
    }
    return true;
}

bool PresetManager::load (int index, juce::String& error)
{
    if (index < 0 || index >= (int) list.size())
        return false;
    const auto& p = list[(size_t) index];
    error.clear();
    const bool ok = apply (apvts, juce::JSON::parse (p.json), error, false);
    currentIndex = index;
    name = p.name;
    if (onLoad) onLoad();
    return ok && error.isEmpty();
}

juce::String PresetManager::toJson (const juce::String& n, const juce::String& category) const
{
    auto params = std::make_unique<juce::DynamicObject>();
    for (auto* p : apvts.processor.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (r == nullptr || r->getParameterID() == "reset" || std::abs (r->getValue() - r->getDefaultValue()) < 1.0e-5f)
            continue;
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r))
            params->setProperty (r->getParameterID(), c->getCurrentChoiceName());
        else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (r))
            params->setProperty (r->getParameterID(), b->get());
        else
            params->setProperty (r->getParameterID(), std::round (r->convertFrom0to1 (r->getValue()) * 10000.0) / 10000.0);
    }
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("name", n);
    root->setProperty ("category", category);
    root->setProperty ("params", juce::var (params.release()));
    return juce::JSON::toString (juce::var (root.release()), false);
}

bool PresetManager::save (const juce::String& n, juce::String& error)
{
    const auto clean = juce::File::createLegalFileName (n.trim());
    if (clean.isEmpty())
    {
        error = "give it a name";
        return false;
    }
    const auto dir = userFolder();
    if (! dir.createDirectory())
    {
        error = "can't create " + dir.getFullPathName();
        return false;
    }
    const auto f = dir.getChildFile (clean + ".json");
    if (! f.replaceWithText (toJson (n.trim(), "User"), false, false, "\n"))
    {
        error = "can't write " + f.getFullPathName();
        return false;
    }
    name = n.trim();
    rescan();
    return true;
}
} // namespace fault
