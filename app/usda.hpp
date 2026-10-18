// Writes the solar system at one instant as an OpenUSD text layer (.usda).
#pragma once

#include <string>

#include "orrery/system.hpp"

namespace orrery {

// True positions and radii (no enlargement), km, ecliptic J2000 with Z
// toward ecliptic north, origin at the solar-system barycentre. Each body is
// a Sphere with customData naming its data sources; the small bodies are one
// Points prim per group. Throws std::runtime_error if the file cannot be written.
void writeUsda(const std::string& path, const SolarSystem& system, const SystemState& state);

}  // namespace orrery
