#pragma once

// Factory presets for the APVTS-based plugins, exposed to hosts as the program
// list and to the editor through ap::ui::PresetBar.
//
// A preset lists only the parameters it changes, in the parameter's own units
// (Hz, ms, dB, %, a choice index, 0/1 for a toggle). Loading one first returns
// every parameter to its default, so a preset never inherits leftovers from the
// one before it. Program 0 of every bank is "Default" (all defaults), which is
// the state a fresh instance opens in.
//
// Thread safety: setCurrentProgram() only calls setValueNotifyingHost(), which
// hosts are allowed to trigger from any thread; the program number is atomic.
//
// The order of a bank is part of the plugin's public interface (hosts store
// program numbers), so new presets go at the end.

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace ap::plug
{

struct PresetValue
{
    const char* id;
    float value;    // in the parameter's own units, not normalised
};

struct FactoryPreset
{
    juce::String name;
    juce::String category;
    std::vector<PresetValue> values;
};

class Programs
{
public:
    static constexpr const char* kStateProperty = "program";

    Programs (juce::AudioProcessor& ownerIn, std::vector<FactoryPreset> bankIn)
        : owner (ownerIn), bank (std::move (bankIn))
    {
        jassert (! bank.empty());
        for (auto* p : owner.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                params.push_back (ranged);
        for (auto* p : params)
            byId[p->getParameterID()] = p;

        // A typo in a preset's parameter ID would silently load the default instead.
        jassert (problems().isEmpty());
    }

    int size() const noexcept { return (int) bank.size(); }
    const FactoryPreset& preset (int index) const { return bank[(size_t) juce::jlimit (0, size() - 1, index)]; }

    // ---- AudioProcessor program API
    int  getNumPrograms() const noexcept { return juce::jmax (1, size()); }
    int  getCurrentProgram() const noexcept { return juce::jlimit (0, getNumPrograms() - 1, current.load()); }
    juce::String getProgramName (int index) const
    {
        return index >= 0 && index < size() ? bank[(size_t) index].name : juce::String();
    }

    /** Loads a preset (the host's setCurrentProgram). Safe on any thread the host calls it from. */
    void setCurrentProgram (int index)
    {
        if (index < 0 || index >= size()) return;
        current.store (index);
        for (auto* p : params)
        {
            const float target = targetValue (*p, bank[(size_t) index]);
            if (std::fabs (p->getValue() - target) > 1.0e-6f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (target);
                p->endChangeGesture();
            }
        }
    }

    /** Loads a preset chosen in the editor and tells the host the program changed. */
    void selectFromEditor (int index)
    {
        if (index < 0 || index >= size()) return;
        setCurrentProgram (index);
        owner.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
    }

    /** True when any parameter differs from the current preset (for the "edited" mark). */
    bool isModified() const
    {
        const auto& p = bank[(size_t) getCurrentProgram()];
        for (auto* param : params)
            if (std::fabs (param->getValue() - targetValue (*param, p)) > 1.0e-4f)
                return true;
        return false;
    }

    // ---- State: the program number travels in the saved state as an attribute.
    void saveTo (juce::ValueTree& state) const { state.setProperty (kStateProperty, getCurrentProgram(), nullptr); }
    void restoreFrom (const juce::XmlElement& xml)
    {
        const int p = xml.getIntAttribute (kStateProperty, 0);
        current.store (p >= 0 && p < size() ? p : 0);
    }

    /** Bank mistakes: duplicate or empty names, values that name no parameter or fall outside its
        range, and a program 0 that is not an empty "Default". Empty when the bank is sound. */
    juce::StringArray problems() const
    {
        juce::StringArray out;
        juce::StringArray names;
        for (const auto& p : bank)
        {
            if (p.name.isEmpty() || names.contains (p.name)) out.add ("duplicate or empty name '" + p.name + "'");
            names.add (p.name);
            for (const auto& v : p.values)
            {
                const auto it = byId.find (v.id);
                if (it == byId.end()) { out.add (p.name + ": unknown parameter " + v.id); continue; }
                const auto range = it->second->getNormalisableRange();
                if (! std::isfinite (v.value) || v.value < range.start - 1.0e-4f || v.value > range.end + 1.0e-4f)
                    out.add (p.name + ": " + v.id + " = " + juce::String (v.value) + " out of range");
            }
        }
        if (bank.empty() || bank.front().name != "Default" || ! bank.front().values.empty())
            out.add ("program 0 must be an empty \"Default\"");
        return out;
    }

private:
    float targetValue (juce::RangedAudioParameter& param, const FactoryPreset& p) const
    {
        for (const auto& v : p.values)
            if (param.getParameterID() == v.id)
                return param.convertTo0to1 (param.getNormalisableRange().snapToLegalValue (v.value));
        return param.getDefaultValue();
    }

    juce::AudioProcessor& owner;
    std::vector<FactoryPreset> bank;
    std::vector<juce::RangedAudioParameter*> params;
    std::map<juce::String, juce::RangedAudioParameter*> byId;
    std::atomic<int> current { 0 };
};

} // namespace ap::plug
