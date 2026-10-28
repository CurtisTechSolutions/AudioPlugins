#include "Physics.h"

namespace verdant
{

void BouncingBall::set (float gravity01, float bounce01) { gravity = gravity01; bounce = bounce01; }
void BouncingBall::trigger (float) {}
void BouncingBall::advance (float) noexcept {}
float BouncingBall::height() const noexcept { return h; }

void Spring::set (float stiffness01, float damping01) { stiffness = stiffness01; damping = damping01; }
void Spring::trigger (float) {}
void Spring::advance (float) noexcept {}
float Spring::displacement() const noexcept { return x; }

void Orbit::set (float speed01, float shape01) { speed = speed01; shape = shape01; }
void Orbit::trigger() {}
void Orbit::advance (float) noexcept {}
float Orbit::x() const noexcept { return px; }
float Orbit::y() const noexcept { return py; }

} // namespace verdant
