#include "KeyAware.h"

#include <algorithm>
#include <cstdlib>

namespace verdant
{

int snapToScale (int midiNote, const ap::Scale& scale)
{
    return scale.nearest ((float) midiNote);
}

int scaleDegreeOffset (int midiNote, int degrees, const ap::Scale& scale)
{
    int n = snapToScale (midiNote, scale);
    const int dir = degrees < 0 ? -1 : 1;
    for (int d = 0; d < std::abs (degrees); ++d)
    {
        int next = n + dir;
        for (int guard = 0; guard < 12 && ! scale.contains (next); ++guard)
            next += dir;
        n = next;
    }
    return n;
}

namespace
{
    int passThrough (const MidiEvent* in, int numIn, MidiEvent* out, int maxOut)
    {
        const int n = std::min (numIn, maxOut);
        if (in != nullptr && out != nullptr && n > 0)
            std::copy (in, in + n, out);
        return std::max (0, n);
    }
}

void Arpeggiator::prepare (double sampleRate) { sr = sampleRate; }
void Arpeggiator::set (const ArpParams& p, const ap::Scale& s) { params = p; scale = s; }
int Arpeggiator::process (const MidiEvent* in, int numIn, const Transport&, int, MidiEvent* out, int maxOut) { return passThrough (in, numIn, out, maxOut); }
void Arpeggiator::reset() {}

void StepSequencer::prepare (double sampleRate) { sr = sampleRate; }
void StepSequencer::set (const SeqParams& p, const ap::Scale& s) { params = p; scale = s; }
int StepSequencer::process (const MidiEvent* in, int numIn, const Transport&, int, MidiEvent* out, int maxOut) { return passThrough (in, numIn, out, maxOut); }
float StepSequencer::modValue() const noexcept { return 0.0f; }
int StepSequencer::currentStep() const noexcept { return -1; }
void StepSequencer::reset() {}

} // namespace verdant
