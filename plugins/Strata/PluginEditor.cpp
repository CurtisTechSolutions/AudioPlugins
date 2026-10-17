#include "PluginEditor.h"

namespace strata
{

Editor::Editor (Processor& p)
    : ap::ui::EditorBase (p, p.apvts, "Strata", "Polyphonic subtractive synth", juce::Colour (0xfff5a623), 980, 2),
      processor (p),
      meter (p.outputLevel)
{
    setHeaderComponent (&meter, 14, 40);

    // Left column: sound sources + filter.
    addSection ("Oscillator A", { ids::aWave, ids::aOct, ids::aSemi, ids::aFine, ids::aLevel, ids::aPw,
                                  ids::uniVoices, ids::uniDetune, ids::uniSpread }, 0, 0);
    addSection ("Oscillator B", { ids::bWave, ids::bOct, ids::bSemi, ids::bFine, ids::bLevel, ids::bPw }, 0, 0);
    addSection ("Sub & Noise",  { ids::subLevel, ids::subWave, ids::subOct, ids::noiseLevel }, 0, 0);
    addSection ("Filter",       { ids::fType, ids::fCutoff, ids::fRes, ids::fEnvAmt, ids::fKey, ids::fLfo, ids::fDrive }, 0, 0);

    // Right column: modulation, global, effects.
    addSection ("Amp Envelope",    { ids::ampA, ids::ampD, ids::ampS, ids::ampR }, 0, 1);
    addSection ("Filter Envelope", { ids::fenvA, ids::fenvD, ids::fenvS, ids::fenvR }, 0, 1);
    addSection ("LFO 1 · Pitch",   { ids::lfo1Shape, ids::lfo1Rate, ids::lfo1Depth, ids::lfo1Fade }, 0, 1);
    addSection ("LFO 2 · Filter",  { ids::lfo2Shape, ids::lfo2Rate }, 0, 1);
    addSection ("Global",          { ids::voices, ids::glide, ids::velAmp, ids::velFilter, ids::bendRange, ids::master }, 0, 1);
    addSection ("Effects",         { ids::chorusMix, ids::chorusRate, ids::delayTime, ids::delayFb, ids::delayMix,
                                     ids::reverbSize, ids::reverbMix }, 0, 1);

    finishLayout();
}

} // namespace strata
