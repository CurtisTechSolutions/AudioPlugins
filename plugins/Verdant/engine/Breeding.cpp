#include "Breeding.h"

#include <algorithm>
#include <utility>

namespace verdant
{

std::vector<Patch> breed (const Patch& a, const Patch& b, const BreedSettings& settings)
{
    std::vector<Patch> children;
    const int count = std::clamp (settings.count, 0, 64);
    children.reserve ((size_t) count);
    for (int k = 0; k < count; ++k)
    {
        Patch child = a;
        child.name = a.name + " \xC3\x97 " + b.name + " #" + std::to_string (k + 1);
        children.push_back (std::move (child));
    }
    return children;
}

void BreedHistory::push (Patch a, Patch b, std::vector<Patch> children)
{
    generations.push_back (Generation { std::move (a), std::move (b), std::move (children) });
}

bool BreedHistory::canUndo() const
{
    return ! generations.empty();
}

void BreedHistory::undo()
{
    if (! generations.empty())
        generations.pop_back();
}

const std::vector<Patch>* BreedHistory::current() const
{
    return generations.empty() ? nullptr : &generations.back().children;
}

int BreedHistory::generation() const
{
    return (int) generations.size();
}

} // namespace verdant
