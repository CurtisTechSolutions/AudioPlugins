#include "modules/Module.h"

// Intentionally minimal: keeps frisson_modules a valid static library before
// the first real module lands, and anchors the contract header in the build.
namespace frisson
{
    static_assert (sizeof (Modulation) > 0, "contract header must compile");
}
