#include "usda.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "orrery/time.hpp"

namespace orrery {
namespace {

std::string primName(const std::string& name) {
    std::string out;
    for (char c : name) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
    return out;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string vec(const Vec3& v) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "(%.6f, %.6f, %.6f)", v.x, v.y, v.z);
    return buf;
}

float linearToSrgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

}  // namespace

void writeUsda(const std::string& path, const SolarSystem& system, const SystemState& state) {
    std::ostringstream o;
    const CalendarDate utc = calendarDate(tdbToUtc(state.jdTdb));
    o << "#usda 1.0\n(\n"
      << "    defaultPrim = \"SolarSystem\"\n"
      << "    metersPerUnit = 1000\n"
      << "    upAxis = \"Z\"\n"
      << "    doc = " << quoted("Solar system at " + formatIsoDate(utc) + " UTC (JD " + std::to_string(state.jdTdb) +
                                " TDB), written by orrery. Units: km. Frame: ecliptic and mean equinox of J2000, "
                                "origin at the solar-system barycentre. True positions and radii.")
      << "\n)\n\n";
    o << "def Xform \"SolarSystem\" (\n    kind = \"assembly\"\n)\n{\n";
    const auto& bodies = system.bodies();
    for (size_t i = 0; i < bodies.size(); ++i) {
        const Body& b = bodies[i];
        if (!state.valid[i]) continue;
        const char* type = b.radiusKm > 0 ? "Sphere" : "Xform";
        o << "    def " << type << " " << quoted(primName(b.name)) << " (\n        customData = {\n            dictionary orrery = {\n"
          << "                string name = " << quoted(b.name) << "\n"
          << "                string kind = " << quoted(kindName(b.kind)) << "\n"
          << "                int naifId = " << b.naif << "\n"
          << "                string positionSource = "
          << quoted(b.source == BodySource::Spk ? "JPL DE440s (NAIF)"
                    : b.source == BodySource::Table ? "JPL Horizons osculating elements (interpolated)"
                                                    : "JPL Horizons state vectors")
          << "\n";
        if (b.radiusKm > 0) o << "                string radiusSource = " << quoted(b.radiusSource) << "\n";
        o << "            }\n        }\n    )\n    {\n";
        if (b.radiusKm > 0) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.3f", b.radiusKm);
            o << "        double radius = " << buf << "\n";
            o << "        color3f[] primvars:displayColor = [(" << linearToSrgb(b.color[0]) << ", "
              << linearToSrgb(b.color[1]) << ", " << linearToSrgb(b.color[2]) << ")]\n";
        }
        o << "        double3 xformOp:translate = " << vec(state.positions[i]) << "\n"
          << "        uniform token[] xformOpOrder = [\"xformOp:translate\"]\n    }\n\n";
    }
    for (size_t g = 0; g < system.groups().size(); ++g) {
        const SmallBodyGroup& group = system.groups()[g];
        o << "    def Points " << quoted(primName(group.id)) << " (\n        customData = {\n"
          << "            dictionary orrery = {\n"
          << "                string label = " << quoted(group.label) << "\n"
          << "                string positionSource = \"JPL SBDB osculating elements, two-body\"\n"
          << "            }\n        }\n    )\n    {\n        point3f[] points = [";
        bool first = true;
        int count = 0;
        for (size_t k = 0; k < system.smallBodies().size(); ++k) {
            if (system.smallBodies()[k].group != static_cast<int>(g)) continue;
            o << (first ? "" : ", ") << vec(state.smallBodies[k]);
            first = false;
            ++count;
        }
        o << "]\n        float[] widths = [1000] (\n            interpolation = \"constant\"\n        )\n"
          << "        color3f[] primvars:displayColor = [(" << linearToSrgb(group.color[0]) << ", "
          << linearToSrgb(group.color[1]) << ", " << linearToSrgb(group.color[2]) << ")] (\n            interpolation = \"constant\"\n        )\n    }\n\n";
        (void)count;
    }
    o << "}\n";
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path);
    out << o.str();
}

}  // namespace orrery
