#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>

namespace ap::plug
{

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

inline juce::ParameterID pid (const char* id) { return juce::ParameterID (id, 1); }

/** Float parameter with an optional mid-point for skew (0 = linear) and unit suffix. */
inline std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id, const juce::String& name,
                                                              float min, float max, float def,
                                                              float midpoint = 0.0f, const juce::String& suffix = {},
                                                              int decimals = 2)
{
    juce::NormalisableRange<float> range (min, max);
    if (midpoint > min && midpoint < max) range.setSkewForCentre (midpoint);

    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel (suffix)
                     .withStringFromValueFunction ([suffix, decimals] (float v, int)
                     {
                         // juce::String (v, 0) means "full precision", not "no decimals".
                         juce::String s = decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v));
                         if (decimals > 0 && s.containsChar ('.'))
                         {
                             s = s.trimCharactersAtEnd ("0");
                             s = s.trimCharactersAtEnd (".");
                         }
                         // Ratios read "3:1", everything else "3 dB".
                         return suffix.isEmpty() ? s : s + (suffix.startsWithChar (':') ? "" : " ") + suffix;
                     })
                     .withValueFromStringFunction ([] (const juce::String& text)
                     {
                         return text.retainCharacters ("0123456789.-").getFloatValue();
                     });

    return std::make_unique<juce::AudioParameterFloat> (pid (id), name, range, def, attrs);
}

/** Frequency parameter in Hz that displays kHz above 1000. */
inline std::unique_ptr<juce::AudioParameterFloat> hzParam (const char* id, const juce::String& name,
                                                           float min, float max, float def)
{
    juce::NormalisableRange<float> range (min, max);
    range.setSkewForCentre (std::sqrt (min * max));

    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel ("Hz")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                             : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
                     })
                     .withValueFromStringFunction ([] (const juce::String& text)
                     {
                         const float v = text.retainCharacters ("0123456789.-").getFloatValue();
                         return text.containsIgnoreCase ("k") ? v * 1000.0f : v;
                     });

    return std::make_unique<juce::AudioParameterFloat> (pid (id), name, range, def, attrs);
}

/** Time parameter in milliseconds, log-ish skew, shows seconds above 1000 ms. */
inline std::unique_ptr<juce::AudioParameterFloat> msParam (const char* id, const juce::String& name,
                                                           float min, float max, float def)
{
    juce::NormalisableRange<float> range (min, max);
    if (min > 0.0f) range.setSkewForCentre (std::sqrt (min * max));
    else            range.setSkewForCentre (max * 0.2f);

    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel ("ms")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s"
                                             : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
                     })
                     .withValueFromStringFunction ([] (const juce::String& text)
                     {
                         const float v = text.retainCharacters ("0123456789.-").getFloatValue();
                         return text.trim().endsWithIgnoreCase ("s") && ! text.containsIgnoreCase ("ms") ? v * 1000.0f : v;
                     });

    return std::make_unique<juce::AudioParameterFloat> (pid (id), name, range, def, attrs);
}

inline std::unique_ptr<juce::AudioParameterFloat> dbParam (const char* id, const juce::String& name,
                                                           float min, float max, float def)
{
    return floatParam (id, name, min, max, def, 0.0f, "dB", 1);
}

inline std::unique_ptr<juce::AudioParameterFloat> percentParam (const char* id, const juce::String& name, float def)
{
    return floatParam (id, name, 0.0f, 100.0f, def, 0.0f, "%", 0);
}

inline std::unique_ptr<juce::AudioParameterInt> intParam (const char* id, const juce::String& name,
                                                          int min, int max, int def, const juce::String& suffix = {})
{
    return std::make_unique<juce::AudioParameterInt> (pid (id), name, min, max, def,
                                                      juce::AudioParameterIntAttributes().withLabel (suffix));
}

inline std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const juce::String& name,
                                                                const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (pid (id), name, choices, def);
}

inline std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (pid (id), name, def);
}

/** Fetches a raw atomic for a parameter; asserts in debug when the ID is wrong. */
inline std::atomic<float>* raw (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

} // namespace ap::plug
