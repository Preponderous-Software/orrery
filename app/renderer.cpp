#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <limits>

#include "orrery/time.hpp"
#include "image_file.hpp"

namespace orrery {
namespace {

float f(double v) { return static_cast<float>(v); }

void setVec(float out[3], const Vec3& v) {
    out[0] = f(v.x);
    out[1] = f(v.y);
    out[2] = f(v.z);
}

std::string thousands(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f", v);
    std::string s = buf, out;
    const size_t start = s[0] == '-' ? 1 : 0;
    for (size_t i = 0; i < s.size(); ++i) {
        out += s[i];
        const size_t rest = s.size() - i - 1;
        if (i >= start && rest > 0 && rest % 3 == 0) out += ',';
    }
    return out;
}

// Two unit vectors perpendicular to n and to each other.
void basis(const Vec3& n, Vec3& u, Vec3& v) {
    const Vec3 helper = std::fabs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    u = normalize(cross(helper, n));
    v = cross(n, u);
}

std::string distanceText(double km) {
    if (km < 1e7) return thousands(km) + " km";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f au", km / kAuKm);
    return buf;
}

std::string speedText(double kmPerS) {
    const double a = std::fabs(kmPerS);
    char buf[96];
    if (a < 10) std::snprintf(buf, sizeof buf, "%.2f km/s", kmPerS);
    else std::snprintf(buf, sizeof buf, "%s km/s", thousands(kmPerS).c_str());
    std::string s = buf;
    const double c = a / kLightKmPerS;
    if (c >= 10) std::snprintf(buf, sizeof buf, "  (%s c)", thousands(c).c_str());
    else if (c >= 0.01) std::snprintf(buf, sizeof buf, "  (%.2f c)", c);
    else buf[0] = 0;
    return s + buf;
}

// Touchdown and climb speeds: m/s below 1 km/s.
std::string verticalText(double kmPerS) {
    char buf[64];
    const double a = std::fabs(kmPerS);
    if (a < 1) std::snprintf(buf, sizeof buf, "%.0f m/s", a * 1000);
    else if (a < 10) std::snprintf(buf, sizeof buf, "%.2f km/s", a);
    else return thousands(a) + " km/s";
    return buf;
}

std::string durationText(double s) {
    char buf[64];
    if (s < 60) std::snprintf(buf, sizeof buf, "%.0f s", s);
    else if (s < 3600) std::snprintf(buf, sizeof buf, "%d min %02d s", static_cast<int>(s / 60), static_cast<int>(s) % 60);
    else if (s < 86400) std::snprintf(buf, sizeof buf, "%d h %02d min", static_cast<int>(s / 3600), static_cast<int>(s / 60) % 60);
    else std::snprintf(buf, sizeof buf, "%.1f days", s / 86400);
    return buf;
}

constexpr float kWhite[4] = {1, 1, 1, 1};
constexpr float kDim[4] = {0.75f, 0.78f, 0.85f, 1};
constexpr float kWarn[4] = {1.0f, 0.75f, 0.35f, 1};
constexpr float kGo[4] = {0.45f, 1.0f, 0.6f, 1};
constexpr float kAlarm[4] = {1.0f, 0.42f, 0.38f, 1};

const float* toneColor(HudTone tone) {
    switch (tone) {
        case HudTone::Dim: return kDim;
        case HudTone::Go: return kGo;
        case HudTone::Warn: return kWarn;
        default: return kWhite;
    }
}

}  // namespace

// Planet maps by NAIF id: equirectangular, 0 degrees longitude in the
// centre, east to the right, north at the top. Sources and licences are in
// DATA_SOURCES.md ("Planet maps").
struct MapFile {
    long naif;
    const char* file;
};
constexpr MapFile kMaps[] = {
    {10, "sun.jpg"},       {199, "mercury.jpg"}, {299, "venus_atmosphere.jpg"}, {399, "earth_daymap.jpg"},
    {301, "moon.jpg"},     {499, "mars.jpg"},    {599, "jupiter.jpg"},          {699, "saturn.jpg"},
    {799, "uranus.jpg"},   {899, "neptune.jpg"},
};

Renderer::Renderer(strata::Engine& engine, const SolarSystem& system, const std::string& textureDir)
    : engine_(engine), sys_(system) {
    const auto& bodies = sys_.bodies();
    textured_.assign(bodies.size(), TexturedBody{});
    if (strata_abi_minor() < 7) {
        std::fprintf(stderr, "orrery: Strata %s has no textures (ABI 1.7); planets are plain spheres\n",
                     strata_version_string());
        return;
    }
    // A unit latitude-longitude sphere in body coordinates: +Z north, +X the
    // prime meridian, east longitude counter-clockwise about +Z. The seam's
    // column is duplicated at u = 0 and u = 1.
    const int slices = 96, stacks = 48;
    for (int j = 0; j <= stacks; ++j) {
        const double v = static_cast<double>(j) / stacks;
        const double lat = (0.5 - v) * kPi;
        for (int i = 0; i <= slices; ++i) {
            const double u = static_cast<double>(i) / slices;
            const double lon = (u - 0.5) * 2 * kPi;  // the map's centre is longitude 0
            spherePositions_.insert(spherePositions_.end(), {f(std::cos(lat) * std::cos(lon)),
                                                             f(std::cos(lat) * std::sin(lon)), f(std::sin(lat))});
            sphereUvs_.insert(sphereUvs_.end(), {f(u), f(v)});
        }
    }
    const auto row = static_cast<uint32_t>(slices + 1);
    for (uint32_t j = 0; j < static_cast<uint32_t>(stacks); ++j) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(slices); ++i) {
            const uint32_t a = j * row + i, b = a + 1, c = a + row, d = c + 1;
            sphereIndices_.insert(sphereIndices_.end(), {a, c, b, b, c, d});  // counter-clockwise from outside
        }
    }
    for (const MapFile& map : kMaps) {
        int body = -1;
        for (size_t i = 0; i < bodies.size(); ++i) {
            if (bodies[i].naif == map.naif) body = static_cast<int>(i);
        }
        if (body < 0) continue;
        const std::string path = textureDir + "/" + map.file;
        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        std::string error;
        if (!loadRgba8(path, rgba, w, h, &error)) {
            std::fprintf(stderr, "orrery: cannot read %s (%s); %s stays a plain sphere\n", path.c_str(),
                         error.c_str(), bodies[static_cast<size_t>(body)].name.c_str());
            continue;
        }
        TexturedBody& t = textured_[static_cast<size_t>(body)];
        t.texture = engine_.createTextureRgba8(w, h, rgba.data(), STRATA_TEXTURE_MIPMAPS | STRATA_TEXTURE_CLAMP_V);
        t.unlit = map.naif == 10;  // the Sun shines; it is not lit
    }
}

int Renderer::texturedBodies() const {
    int n = 0;
    for (const auto& t : textured_) n += t.texture != 0 ? 1 : 0;
    return n;
}

bool Renderer::placeTextured(size_t i, const SystemState& state, const Vec3& origin) {
    TexturedBody& t = textured_[i];
    if (t.texture == 0) return false;
    if (!state.valid[i] || drawnRadius_[i] <= 0) {
        if (t.mesh != 0) engine_.removeMesh(t.mesh);
        t.mesh = 0;
        return true;
    }
    if (t.mesh == 0) {
        StrataTexturedMesh m{};
        m.mesh.positions = spherePositions_.data();
        m.mesh.normals = spherePositions_.data();  // a unit sphere's normals are its positions
        m.mesh.indices = sphereIndices_.data();
        m.mesh.vertex_count = static_cast<int32_t>(spherePositions_.size() / 3);
        m.mesh.index_count = static_cast<int32_t>(sphereIndices_.size());
        m.mesh.color[0] = m.mesh.color[1] = m.mesh.color[2] = m.mesh.color[3] = 1.0f;
        m.uvs = sphereUvs_.data();
        m.texture = t.texture;
        m.flags = t.unlit ? STRATA_MESH_UNLIT : 0u;
        t.mesh = engine_.addTexturedMesh(m);
    }
    // Pole and prime meridian from the IAU elements (axial tilt and sidereal
    // rotation); a body without them keeps the ecliptic axes.
    BodyFrame frame;
    sys_.frame(static_cast<int>(i), state.jdTdb, frame);
    const double r = drawnRadius_[i];
    const Vec3 c = drawn_[i] - origin;
    const float model[16] = {f(frame.x.x * r), f(frame.x.y * r), f(frame.x.z * r), 0,
                             f(frame.y.x * r), f(frame.y.y * r), f(frame.y.z * r), 0,
                             f(frame.z.x * r), f(frame.z.y * r), f(frame.z.z * r), 0,
                             f(c.x),           f(c.y),           f(c.z),           1};
    engine_.setMeshTransform(t.mesh, model);
    return true;
}

void Renderer::layout(const SystemState& state, const Scale& scale) {
    const auto& bodies = sys_.bodies();
    const size_t n = bodies.size();
    drawn_.assign(n, Vec3{});
    drawnRadius_.assign(n, 0.0);
    const Vec3 sunTrue = state.positions[0];
    for (size_t i = 0; i < n; ++i) {
        drawnRadius_[i] = bodies[i].radiusKm > 0 ? scale.radius(bodies[i].radiusKm, i == 0) : 0.0;
    }
    drawn_[0] = sunTrue;
    // Heliocentric objects first, then satellites (whose parents are placed).
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 1; i < n; ++i) {
            const int parent = bodies[i].parent;
            const bool helio = parent <= 0;
            if (helio != (pass == 0)) continue;
            if (helio) {
                drawn_[i] = scale.heliocentric(state.positions[i], sunTrue, sunTrue);
            } else {
                const auto p = static_cast<size_t>(parent);
                drawn_[i] = scale.satellite(state.positions[i], state.positions[p], drawn_[p], bodies[p].radiusKm);
            }
        }
    }
    drawnSmall_.resize(state.smallBodies.size());
    for (size_t k = 0; k < state.smallBodies.size(); ++k) {
        drawnSmall_[k] = scale.heliocentric(state.smallBodies[k], sunTrue, sunTrue);
    }
}

double Renderer::defaultDistance(int body) const {
    const auto& bodies = sys_.bodies();
    const double tanHalf = std::tan(25.0 * kDegToRad);
    if (body == 0) {
        // The planets out to Jupiter in view.
        const int jupiter = sys_.find("Jupiter");
        const double extent = jupiter >= 0 ? length(drawn_[static_cast<size_t>(jupiter)] - drawn_[0]) : 5 * kAuKm;
        return extent * 1.25 / tanHalf;
    }
    double extent = drawnRadius_[static_cast<size_t>(body)] * 6.0;
    for (size_t j = 0; j < bodies.size(); ++j) {
        if (bodies[j].parent == body) extent = std::max(extent, 1.15 * length(drawn_[j] - drawn_[static_cast<size_t>(body)]));
    }
    if (extent <= 0) extent = 0.2 * kAuKm;  // comets and spacecraft have no size
    return extent / tanHalf;
}

Vec3 Renderer::forwardOf(const CameraState& cam) const {
    if (cam.ship) return cam.look;
    const double y = cam.yawDeg * kDegToRad, p = cam.pitchDeg * kDegToRad;
    const Vec3 outward{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
    return cam.freeFly ? outward : -outward;
}

Vec3 Renderer::eyeOf(const CameraState& cam) const {
    if (cam.freeFly || cam.ship) return cam.eye;
    return drawn_[static_cast<size_t>(cam.focus)] - forwardOf(cam) * cam.distance;
}

std::vector<FlightBody> Renderer::flightBodies(const SystemState& state) const {
    const auto& bodies = sys_.bodies();
    std::vector<FlightBody> out(bodies.size());
    for (size_t i = 0; i < bodies.size(); ++i) {
        FlightBody& fb = out[i];
        fb.centre = drawn_[i];
        fb.radius = drawnRadius_[i];
        fb.present = state.valid[i];
        sys_.frame(static_cast<int>(i), state.jdTdb, fb.frame);  // a landed ship turns with its body
        fb.solid = hasSolidSurface(bodies[i].kind, bodies[i].naif);
        fb.air = atmosphereOf(bodies[i].naif);
        if (fb.radius > 0 && bodies[i].radiusKm > 0) fb.realPerDrawn = bodies[i].radiusKm / fb.radius;
        // The Sun holds everything; a planet its moon system; a moon a few radii.
        if (i == 0) {
            fb.influence = std::numeric_limits<double>::infinity();
        } else if (fb.radius > 0) {
            fb.influence = (bodies[i].kind == BodyKind::Moon ? 12 : 25) * fb.radius;
            for (size_t j = 0; j < bodies.size(); ++j) {
                if (bodies[j].parent == static_cast<int>(i) && state.valid[j]) {
                    fb.influence = std::max(fb.influence, 1.3 * length(drawn_[j] - drawn_[i]));
                }
            }
        }
    }
    return out;
}

void Renderer::addLine(const Vec3& a, const Vec3& b, const float color[4], const Vec3& origin) {
    StrataLineVertex va{}, vb{};
    setVec(va.position, a - origin);
    setVec(vb.position, b - origin);
    std::memcpy(va.color, color, sizeof va.color);
    std::memcpy(vb.color, color, sizeof vb.color);
    lines_.push_back(va);
    lines_.push_back(vb);
}

void Renderer::drawOrbit(int body, const SystemState& state, const Scale& scale, const Vec3& origin) {
    const auto& b = sys_.bodies()[static_cast<size_t>(body)];
    Elements el;
    if (!sys_.osculating(body, state.jdTdb, state, el) || el.e >= 1.0) return;
    const int parent = b.parent < 0 ? 0 : b.parent;
    const auto p = static_cast<size_t>(parent);
    const float alpha = b.kind == BodyKind::Moon ? 0.45f : 0.55f;
    const float color[4] = {b.color[0] * 0.8f + 0.1f, b.color[1] * 0.8f + 0.1f, b.color[2] * 0.8f + 0.1f, alpha};
    const int segments = b.kind == BodyKind::Comet ? 720 : 256;
    Vec3 previous;
    for (int k = 0; k <= segments; ++k) {
        const double E = 2 * kPi * k / segments;
        const Vec3 truePos = state.positions[p] + positionAtEccentricAnomaly(el, E);
        const Vec3 drawnPos = parent == 0 ? scale.heliocentric(truePos, state.positions[0], drawn_[0])
                                          : scale.satellite(truePos, state.positions[p], drawn_[p],
                                                            sys_.bodies()[p].radiusKm);
        if (k > 0) addLine(previous, drawnPos, color, origin);
        previous = drawnPos;
    }
}

void Renderer::addRings(int planet, const SystemState& state, const Scale& scale, const Vec3& origin) {
    Vec3 pole;
    if (!sys_.pole(planet, state.jdTdb, pole)) return;
    Vec3 u, v;
    basis(pole, u, v);
    const auto& body = sys_.bodies()[static_cast<size_t>(planet)];
    const Vec3 centre = drawn_[static_cast<size_t>(planet)] - origin;
    const int segments = 192;
    for (const Ring& ring : sys_.rings()) {
        if (ring.planet != planet) continue;
        const double r0 = scale.ringRadius(ring.innerKm, body.radiusKm);
        const double r1 = scale.ringRadius(ring.outerKm, body.radiusKm);
        const float shade = 0.35f + 0.65f * ring.alpha;
        for (int side = 0; side < 2; ++side) {
            const Vec3 normal = side == 0 ? pole : -pole;
            const auto base = static_cast<uint32_t>(meshPositions_.size() / 3);
            for (int k = 0; k <= segments; ++k) {
                const double a = 2 * kPi * k / segments;
                const Vec3 dir = u * std::cos(a) + v * std::sin(a);
                for (double r : {r0, r1}) {
                    const Vec3 p = centre + dir * r;
                    meshPositions_.insert(meshPositions_.end(), {f(p.x), f(p.y), f(p.z)});
                    meshNormals_.insert(meshNormals_.end(), {f(normal.x), f(normal.y), f(normal.z)});
                    meshColors_.insert(meshColors_.end(),
                                       {ring.color[0] * shade, ring.color[1] * shade, ring.color[2] * shade});
                }
            }
            for (int k = 0; k < segments; ++k) {
                const uint32_t i0 = base + 2 * static_cast<uint32_t>(k), i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
                // Counter-clockwise seen from the side the normal points to.
                if (side == 0) {
                    meshIndices_.insert(meshIndices_.end(), {i0, i2, i1, i1, i2, i3});
                } else {
                    meshIndices_.insert(meshIndices_.end(), {i0, i1, i2, i1, i3, i2});
                }
            }
        }
    }
}

bool Renderer::project(const Vec3& rel, int width, int height, float& sx, float& sy, float& clipW) const {
    const float* v = camera_.view;
    const float* pr = camera_.projection;
    const float in[4] = {f(rel.x), f(rel.y), f(rel.z), 1};
    float eyeSpace[4] = {0, 0, 0, 0}, clip[4] = {0, 0, 0, 0};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) eyeSpace[r] += v[c * 4 + r] * in[c];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) clip[r] += pr[c * 4 + r] * eyeSpace[c];
    if (clip[3] <= 0) return false;
    sx = (clip[0] / clip[3] * 0.5f + 0.5f) * static_cast<float>(width);
    sy = (0.5f - clip[1] / clip[3] * 0.5f) * static_cast<float>(height);
    clipW = clip[3];
    return true;
}

// A small delta-winged hull, in lengths of the ship: +x forward, +y left, +z up.
namespace {
struct HullFace {
    int a, b, c;
    float color[3];
};
constexpr double kHull[][3] = {
    {0.60, 0, 0},         // 0 nose
    {-0.15, 0, 0.13},     // 1 canopy ridge
    {-0.20, 0, -0.08},    // 2 keel
    {-0.50, 0.46, -0.01}, // 3 left wing tip
    {-0.50, -0.46, -0.01},// 4 right wing tip
    {-0.50, 0.12, 0.08},  // 5 tail, upper left
    {-0.50, -0.12, 0.08}, // 6 tail, upper right
    {-0.50, 0.12, -0.05}, // 7 tail, lower left
    {-0.50, -0.12, -0.05},// 8 tail, lower right
    {0.18, 0, 0.075},     // 9 canopy front
};
constexpr float kHullGrey[3] = {0.78f, 0.80f, 0.84f};
constexpr float kHullDark[3] = {0.30f, 0.32f, 0.36f};
constexpr float kCanopy[3] = {0.12f, 0.30f, 0.55f};
constexpr float kStripe[3] = {0.75f, 0.22f, 0.16f};
constexpr HullFace kFaces[] = {
    {0, 9, 3, {kHullGrey[0], kHullGrey[1], kHullGrey[2]}},
    {0, 4, 9, {kHullGrey[0], kHullGrey[1], kHullGrey[2]}},
    {9, 1, 3, {kCanopy[0], kCanopy[1], kCanopy[2]}},
    {9, 4, 1, {kCanopy[0], kCanopy[1], kCanopy[2]}},
    {1, 5, 3, {kHullGrey[0], kHullGrey[1], kHullGrey[2]}},
    {1, 4, 6, {kHullGrey[0], kHullGrey[1], kHullGrey[2]}},
    {1, 6, 5, {kStripe[0], kStripe[1], kStripe[2]}},
    {0, 3, 2, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {0, 2, 4, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {2, 3, 7, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {2, 8, 4, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {2, 7, 8, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {3, 5, 7, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {4, 8, 6, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {5, 6, 8, {kHullDark[0], kHullDark[1], kHullDark[2]}},
    {5, 8, 7, {kHullDark[0], kHullDark[1], kHullDark[2]}},
};

uint64_t mix(uint64_t x) {  // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
double unit(uint64_t h, int k) { return static_cast<double>((h >> (k * 16)) & 0xFFFF) / 65536.0; }
}  // namespace

void Renderer::addShip(const Ship& ship, const Vec3& origin) {
    const double L = ship.size();
    const Vec3 left = cross(ship.up, ship.forward);
    auto place = [&](int k) {
        return ship.position + ship.forward * (kHull[k][0] * L) + left * (kHull[k][1] * L) + ship.up * (kHull[k][2] * L);
    };
    Vec3 centroid;
    for (int k = 0; k < 10; ++k) centroid += place(k) / 10.0;
    // Flat shading: three vertices of its own per face, wound to face outward.
    for (const HullFace& face : kFaces) {
        Vec3 a = place(face.a), b = place(face.b), c = place(face.c);
        Vec3 n = normalize(cross(b - a, c - a));
        if (dot(n, (a + b + c) / 3.0 - centroid) < 0) {
            std::swap(b, c);
            n = -n;
        }
        const auto base = static_cast<uint32_t>(meshPositions_.size() / 3);
        for (const Vec3& p : {a, b, c}) {
            const Vec3 r = p - origin;
            meshPositions_.insert(meshPositions_.end(), {f(r.x), f(r.y), f(r.z)});
            meshNormals_.insert(meshNormals_.end(), {f(n.x), f(n.y), f(n.z)});
            meshColors_.insert(meshColors_.end(), {face.color[0], face.color[1], face.color[2]});
        }
        meshIndices_.insert(meshIndices_.end(), {base, base + 1, base + 2});
    }
    // Engines: orange on thrusters and cruise, blue-white at warp, brighter with throttle.
    const double push = std::clamp(std::fabs(ship.throttle()), 0.0, 1.0);
    const bool warp = ship.tier() >= 2;
    for (double side : {-1.0, 1.0}) {
        StrataPoint glow{};
        setVec(glow.position, ship.position + ship.forward * (-0.52 * L) + left * (0.07 * side * L) +
                                  ship.up * (0.015 * L) - origin);
        glow.world_diameter = f(L * (0.08 + 0.22 * push));
        glow.color[0] = warp ? 0.55f : 1.0f;
        glow.color[1] = warp ? 0.80f : 0.62f;
        glow.color[2] = warp ? 1.0f : 0.28f;
        glow.color[3] = f(0.35 + 0.65 * push);
        glow.flags = STRATA_POINT_SOFT;
        points_.push_back(glow);
    }
    // Heat: a glow of hot air over the leading end, orange turning yellow-white.
    const double heat = std::clamp(ship.heat(), 0.0, 1.0);
    if (heat > 0.02) {
        const Vec3 lead = ship.forward * (ship.speed < 0 ? -1.0 : 1.0);
        for (int k = 0; k < 3; ++k) {
            StrataPoint hot{};
            setVec(hot.position, ship.position + lead * ((0.45 - 0.35 * k) * L) - origin);
            hot.world_diameter = f(L * (0.5 + 0.9 * heat) * (1.0 - 0.25 * k));
            hot.color[0] = 1.0f;
            hot.color[1] = f(0.45 + 0.5 * heat);
            hot.color[2] = f(0.15 + 0.6 * heat * heat);
            hot.color[3] = f((0.25 + 0.65 * heat) * (1.0 - 0.3 * k));
            hot.flags = STRATA_POINT_SOFT;
            points_.push_back(hot);
        }
    }
}

// The sky from inside a body's air: its colour, as bright as the air above
// is thick (the column above goes as the density here), on the day side only.
// Never at full strength, so the HUD stays readable against it.
std::array<float, 3> Renderer::skyColor(const Ship& ship) const {
    if (ship.air() < 0) return {0, 0, 0};
    const auto i = static_cast<size_t>(ship.air());
    const Atmosphere air = atmosphereOf(sys_.bodies()[i].naif);
    const Vec3 normal = normalize(ship.position - drawn_[i]);
    const double sunUp = dot(normal, normalize(drawn_[0] - drawn_[i]));
    const double day = std::clamp((sunUp + 0.1) / 0.3, 0.0, 1.0);
    const double thick = std::sqrt(std::min(ship.airDensity() / air.surfaceDensity, 1.0));
    const float k = f(0.7 * day * thick);
    return {air.sky[0] * k, air.sky[1] * k, air.sky[2] * k};
}

void Renderer::addDust(const Ship& ship, const Vec3& origin) {
    // Motes fixed in the frame of the body the ship moves with, so they stream
    // past at the ship's own speed. They sit on a lattice sized to the ship;
    // two neighbouring power-of-two lattices fade into each other as it resizes.
    const Vec3 base = ship.anchor() >= 0 ? drawn_[static_cast<size_t>(ship.anchor())] : Vec3{};
    const Vec3 rel = ship.position - base;
    const double level = std::log2(16 * ship.size());
    const double low = std::floor(level);
    const Vec3 velocity = ship.forward * ship.speed;
    const bool warp = ship.tier() >= 2;
    for (int step = 0; step < 2; ++step) {
        const double cell = std::exp2(low + step);
        const double weight = step == 0 ? 1 - (level - low) : level - low;
        if (weight < 0.02) continue;
        const auto cx = static_cast<int64_t>(std::floor(rel.x / cell));
        const auto cy = static_cast<int64_t>(std::floor(rel.y / cell));
        const auto cz = static_cast<int64_t>(std::floor(rel.z / cell));
        for (int64_t dx = -3; dx <= 3; ++dx)
            for (int64_t dy = -3; dy <= 3; ++dy)
                for (int64_t dz = -3; dz <= 3; ++dz) {
                    const uint64_t h = mix(static_cast<uint64_t>(cx + dx) * 73856093ull ^
                                           static_cast<uint64_t>(cy + dy) * 19349663ull ^
                                           static_cast<uint64_t>(cz + dz) * 83492791ull ^
                                           static_cast<uint64_t>(low + step + 1000));
                    const Vec3 p = Vec3{(static_cast<double>(cx + dx) + unit(h, 0)) * cell,
                                        (static_cast<double>(cy + dy) + unit(h, 1)) * cell,
                                        (static_cast<double>(cz + dz) + unit(h, 2)) * cell};
                    const double fade = 1 - length(p - rel) / (3.5 * cell);
                    if (fade <= 0) continue;
                    const float alpha = f(std::min(1.0, 1.3 * fade) * weight);
                    Vec3 streak = velocity * 0.12;  // where the mote was 120 ms ago, relative to the ship
                    if (length(streak) > 3 * cell) streak = normalize(streak) * (3 * cell);
                    const Vec3 at = base + p;
                    if (length(streak) < 0.03 * cell) {
                        StrataPoint mote{};
                        setVec(mote.position, at - origin);
                        mote.size_pixels = 1.5f;
                        mote.color[0] = mote.color[1] = mote.color[2] = 0.8f;
                        mote.color[3] = alpha;
                        points_.push_back(mote);
                    } else {
                        const float color[4] = {warp ? 0.6f : 0.85f, warp ? 0.8f : 0.85f, 1.0f, alpha};
                        addLine(at, at + streak, color, origin);
                    }
                }
    }
}

void Renderer::addTargetMarker(int body, const CameraState& cam, const Vec3& origin, int width, int height) {
    const Vec3 rel = drawn_[static_cast<size_t>(body)] - origin;
    const Vec3 fwd = cam.look;
    const Vec3 right = normalize(cross(fwd, cam.up));
    const Vec3 up = cross(right, fwd);
    const double depth = dot(rel, fwd);
    float sx = 0, sy = 0, clipW = 0;
    const bool onScreen = project(rel, width, height, sx, sy, clipW) && sx >= 0 && sy >= 0 &&
                          sx <= static_cast<float>(width) && sy <= static_cast<float>(height);
    const float green[4] = {0.45f, 1.0f, 0.6f, 0.9f};
    const std::string& name = sys_.bodies()[static_cast<size_t>(body)].name;
    const float size = width >= 1000 ? 16.0f : 8.0f;
    if (onScreen && depth > 0) {
        // Corner brackets around the body, at its distance, never under 18 px.
        const double perPixel = 2 * depth * std::tan(cam.fovYDeg * kDegToRad / 2) / height;
        const double half = std::max(drawnRadius_[static_cast<size_t>(body)] * 1.25, 18 * perPixel);
        const double arm = 0.35 * half;
        const Vec3 c = drawn_[static_cast<size_t>(body)];
        for (double sxn : {-1.0, 1.0})
            for (double syn : {-1.0, 1.0}) {
                const Vec3 corner = c + right * (sxn * half) + up * (syn * half);
                addLine(corner, corner - right * (sxn * arm), green, origin);
                addLine(corner, corner - up * (syn * arm), green, origin);
            }
        return;
    }
    // Off screen: a pointer at the edge toward it.
    double x = dot(rel, right), y = dot(rel, up);
    if (std::hypot(x, y) < 1e-12) y = -1;
    const double a = std::atan2(y, x);
    const float rx = static_cast<float>(width) * 0.42f, ry = static_cast<float>(height) * 0.40f;
    const float px = static_cast<float>(width) / 2 + rx * f(std::cos(a));
    const float py = static_cast<float>(height) / 2 - ry * f(std::sin(a));
    const char* arrow = std::fabs(x) > std::fabs(y) ? (x > 0 ? ">" : "<") : (y > 0 ? "^" : "v");
    const std::string s = std::string(arrow) + " " + name;
    text(std::clamp(px - size * static_cast<float>(s.size()) / 2, 4.0f,
                    static_cast<float>(width) - size * static_cast<float>(s.size()) - 4),
         py - size / 2, size, green, s);
}

void Renderer::text(float x, float y, float size, const float color[4], const std::string& s) {
    textStorage_.push_back(s);
    StrataText t{};
    t.x = x * textScale_;
    t.y = y * textScale_;
    t.pixel_size = size * textScale_;
    t.flags = STRATA_TEXT_SHADOW;
    std::memcpy(t.color, color, sizeof t.color);
    t.utf8 = textStorage_.back().c_str();
    texts_.push_back(t);
}

FrameReport Renderer::draw(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width,
                           int height) {
    const auto& bodies = sys_.bodies();
    items_.clear();
    points_.clear();
    lines_.clear();
    texts_.clear();
    textStorage_.clear();
    meshPositions_.clear();
    meshNormals_.clear();
    meshColors_.clear();
    meshIndices_.clear();
    FrameReport report;

    const Vec3 origin = eyeOf(cam);  // floating origin: everything is drawn relative to the eye
    const Vec3 forward = forwardOf(cam);

    // Near plane from the closest surface, far plane a fixed ratio beyond it.
    // Lines and points use depth clamp, so nothing beyond the far plane is lost.
    double nearest = 1e300;
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (drawnRadius_[i] > 0 && state.valid[i]) {
            nearest = std::min(nearest, length(drawn_[i] - origin) - drawnRadius_[i]);
        }
    }
    double nearPlane = std::max(0.01, std::min(nearest * 0.5, 1e9));
    if (cam.nearLimit > 0) nearPlane = std::min(nearPlane, cam.nearLimit);
    // The ship sits just in front of its camera, so the depth range reaches further.
    const double farPlane = nearPlane * (cam.ship ? 1e7 : 1e6);
    const float zero[3] = {0, 0, 0};
    const float target[3] = {f(forward.x), f(forward.y), f(forward.z)};
    const Vec3 upVec = cam.ship ? cam.up : Vec3{0, 0, 1};  // ecliptic north, or the ship's up
    const float up[3] = {f(upVec.x), f(upVec.y), f(upVec.z)};
    camera_ = strata::cameraLookAt(zero, target, up, f(cam.fovYDeg),
                                   static_cast<float>(width) / static_cast<float>(height), f(nearPlane),
                                   f(farPlane));
    const StrataCamera& camera = camera_;
    engine_.setCamera(camera);

    StrataLighting lighting = strata::lightingDefaults(STRATA_LIGHTING_SPACE);
    lighting.legacy_ambient = 0.05f;
    engine_.setLighting(lighting);
    StrataPointLight sunLight{};
    setVec(sunLight.position, drawn_[0] - origin);
    sunLight.range = 1e13f;  // far beyond everything: intensity is the same at Mercury and in the Kuiper belt
    sunLight.color[0] = 1.0f;
    sunLight.color[1] = 0.96f;
    sunLight.color[2] = 0.9f;
    sunLight.intensity = 1.25f;
    engine_.setPointLights(&sunLight, 1);

    // Which planet system the camera is in: its moons get orbits and labels.
    int system = cam.freeFly ? -1 : view.ship ? view.ship->anchor() : cam.focus;
    if (system > 0 && bodies[static_cast<size_t>(system)].kind == BodyKind::Moon) system = bodies[static_cast<size_t>(system)].parent;

    // ---- bodies
    for (size_t i = 0; i < bodies.size(); ++i) {
        const Body& b = bodies[i];
        const bool mapped = placeTextured(i, state, origin);
        if (!state.valid[i]) continue;
        const Vec3 rel = drawn_[i] - origin;
        if (mapped && drawnRadius_[i] > 0) ++report.spheres;
        if (i == 0) {
            StrataPoint disc{}, glow{};
            setVec(disc.position, rel);
            disc.size_pixels = 6;
            disc.world_diameter = f(2 * drawnRadius_[i]);
            disc.color[0] = 1.0f;
            disc.color[1] = 0.93f;
            disc.color[2] = 0.75f;
            disc.color[3] = 1.0f;
            glow = disc;
            glow.size_pixels = 46;
            glow.world_diameter = f(7 * drawnRadius_[i]);
            glow.color[0] = 1.0f;
            glow.color[1] = 0.7f;
            glow.color[2] = 0.3f;
            glow.color[3] = 0.55f;
            glow.flags = STRATA_POINT_SOFT;
            points_.push_back(disc);
            points_.push_back(glow);
            continue;
        }
        if (drawnRadius_[i] > 0 && !mapped) {
            StrataRenderItem item{};
            item.kind = STRATA_PRIMITIVE_SPHERE;
            setVec(item.position, rel);
            const float d = f(2 * drawnRadius_[i]);
            item.scale[0] = item.scale[1] = item.scale[2] = d;
            std::memcpy(item.color, b.color, sizeof b.color);
            item.color[3] = 1;
            items_.push_back(item);
        }
        StrataPoint marker{};
        setVec(marker.position, rel);
        switch (b.kind) {
            case BodyKind::Planet: marker.size_pixels = 5; break;
            case BodyKind::Dwarf: marker.size_pixels = 4; break;
            case BodyKind::Moon: marker.size_pixels = b.parent == system ? 3.5f : 2.5f; break;
            default: marker.size_pixels = 4; break;
        }
        std::memcpy(marker.color, b.color, sizeof b.color);
        marker.color[3] = state.extrapolated[i] ? 0.5f : 1.0f;
        points_.push_back(marker);
        if (view.axes && b.hasPole && drawnRadius_[i] > 0) {
            Vec3 pole;
            sys_.pole(static_cast<int>(i), state.jdTdb, pole);
            const float axis[4] = {0.9f, 0.4f, 0.4f, 0.9f};
            addLine(drawn_[i] - pole * (1.6 * drawnRadius_[i]), drawn_[i] + pole * (1.6 * drawnRadius_[i]), axis, origin);
        }
    }

    // ---- small bodies
    if (view.smallBodies) {
        const auto& sbs = sys_.smallBodies();
        for (size_t k = 0; k < drawnSmall_.size(); ++k) {
            const SmallBodyGroup& g = sys_.groups()[static_cast<size_t>(sbs[k].group)];
            StrataPoint p{};
            setVec(p.position, drawnSmall_[k] - origin);
            p.size_pixels = 2.0f;
            std::memcpy(p.color, g.color, sizeof g.color);
            p.color[3] = 0.75f;
            points_.push_back(p);
        }
    }

    // ---- orbits, trajectories, rings
    if (view.orbits) {
        for (size_t i = 1; i < bodies.size(); ++i) {
            const Body& b = bodies[i];
            const bool show = b.kind == BodyKind::Planet || b.kind == BodyKind::Dwarf || b.kind == BodyKind::Comet ||
                              (b.kind == BodyKind::Moon && b.parent == system);
            if (show) drawOrbit(static_cast<int>(i), state, view.scale, origin);
            if (b.kind == BodyKind::Spacecraft && !b.trajectory.empty()) {
                const float color[4] = {0.95f, 0.9f, 0.4f, 0.5f};
                Vec3 previous;
                bool have = false;
                for (size_t k = 0; k < b.trajectory.size(); k += 3) {
                    if (b.trajectory[k].jd > state.jdTdb) break;
                    const Vec3 p = view.scale.heliocentric(state.positions[0] + b.trajectory[k].position,
                                                           state.positions[0], drawn_[0]);
                    if (have) addLine(previous, p, color, origin);
                    previous = p;
                    have = true;
                }
                if (have && state.valid[i]) addLine(previous, drawn_[i], color, origin);
            }
        }
    }
    for (size_t i = 1; i < bodies.size(); ++i) {
        if (bodies[i].kind == BodyKind::Planet) addRings(static_cast<int>(i), state, view.scale, origin);
    }
    if (view.ship) {
        addDust(*view.ship, origin);
        if (!view.cockpit) {
            const size_t before = meshIndices_.size();
            addShip(*view.ship, origin);
            report.shipTriangles = static_cast<int>((meshIndices_.size() - before) / 3);
        }
        // No marker for the body the ship sits on: it would point at its centre, under the HUD.
        if (cam.focus >= 0 && state.valid[static_cast<size_t>(cam.focus)] && cam.focus != view.ship->landed()) {
            addTargetMarker(cam.focus, cam, origin, width, height);
        }
    }
    if (ringMesh_ != 0) engine_.removeMesh(ringMesh_);
    ringMesh_ = 0;
    if (!meshIndices_.empty()) {
        StrataMesh mesh{};
        mesh.positions = meshPositions_.data();
        mesh.normals = meshNormals_.data();
        mesh.colors = meshColors_.data();
        mesh.indices = meshIndices_.data();
        mesh.vertex_count = static_cast<int32_t>(meshPositions_.size() / 3);
        mesh.index_count = static_cast<int32_t>(meshIndices_.size());
        mesh.color[3] = 1;
        ringMesh_ = engine_.addMesh(mesh);
    }

    // ---- labels
    if (view.labels) {
        const float* pr = camera.projection;
        struct Rect {
            float x0, y0, x1, y1;
        };
        std::vector<Rect> taken;
        if (view.ship && !view.cockpit) {  // keep labels off the ship
            const Ship& ship = *view.ship;
            const Vec3 left = cross(ship.up, ship.forward);
            Rect box{1e9f, 1e9f, -1e9f, -1e9f};
            for (const auto& k : kHull) {
                const Vec3 p = ship.position + (ship.forward * k[0] + left * k[1] + ship.up * k[2]) * ship.size();
                float hx = 0, hy = 0, hw = 0;
                if (!project(p - origin, width, height, hx, hy, hw)) continue;
                box = {std::min(box.x0, hx), std::min(box.y0, hy), std::max(box.x1, hx), std::max(box.y1, hy)};
            }
            if (box.x0 <= box.x1) taken.push_back(box);
        }
        if (view.hud) {  // keep labels off the HUD text
            const float k = width >= 1000 ? 1.0f : 0.55f;
            taken.push_back({0, 0, width >= 1000 ? 880.0f : 380.0f, 128 * k});
            if (view.ship) {
                taken.push_back({0, static_cast<float>(height) - 150 * k, 900 * k, static_cast<float>(height)});
            } else {
                taken.push_back({0, static_cast<float>(height) - 70 * k, 560 * k, static_cast<float>(height)});
            }
        }
        // Close to a body, things far behind it are not labelled (they would
        // sit next to it on screen and read as its neighbours).
        const double labelRange = cam.freeFly || cam.ship || cam.focus == 0 ? 1e300 : 25.0 * cam.distance;
        // Discs big enough on screen to hide what is behind them, and its label.
        struct Disc {
            size_t body;
            float x, y, r, w;
        };
        std::vector<Disc> discs;
        for (size_t i = 0; i < bodies.size(); ++i) {
            float sx = 0, sy = 0, clipW = 0;
            if (!state.valid[i] || drawnRadius_[i] <= 0 || !project(drawn_[i] - origin, width, height, sx, sy, clipW)) continue;
            const float r = f(drawnRadius_[i] * pr[5] * 0.5 * height / clipW);
            if (r > 4) discs.push_back({i, sx, sy, r, clipW});
        }
        auto tryLabel = [&](size_t i, float size) {
            const Vec3 rel = drawn_[i] - origin;
            if (length(rel) > labelRange && i != 0) return;
            float sx = 0, sy = 0, clipW = 0;
            if (!project(rel, width, height, sx, sy, clipW)) return;
            for (const Disc& d : discs) {  // behind a nearer body: no label
                if (d.body != i && d.w < clipW && std::hypot(sx - d.x, sy - d.y) < d.r) return;
            }
            const float radiusPx = f(drawnRadius_[i] * pr[5] * 0.5 * height / clipW);
            const std::string& name = bodies[i].name;
            const float x = sx + std::max(radiusPx, 3.0f) + 5, y = sy - size / 2;
            const Rect r{x - 2, y - 2, x + size * static_cast<float>(name.size()) + 2, y + size + 2};
            if (r.x0 < 0 || r.x1 > static_cast<float>(width) || r.y0 < 0 || r.y1 > static_cast<float>(height)) return;
            for (const Rect& t : taken) {
                if (r.x0 < t.x1 && r.x1 > t.x0 && r.y0 < t.y1 && r.y1 > t.y0) return;
            }
            for (const Disc& d : discs) {  // nor one written across a nearer body
                const float nx = std::clamp(d.x, r.x0, r.x1), ny = std::clamp(d.y, r.y0, r.y1);
                if (d.body != i && d.w < clipW && std::hypot(nx - d.x, ny - d.y) < d.r) return;
            }
            taken.push_back(r);
            text(x, y, size, bodies[i].kind == BodyKind::Moon ? kDim : kWhite, name);
            ++report.labels;
        };
        // Priority order: the Sun, planets, dwarf planets, the focused system's
        // moons, then comets and spacecraft.
        const BodyKind order[] = {BodyKind::Star, BodyKind::Planet, BodyKind::Dwarf, BodyKind::Moon,
                                  BodyKind::Comet, BodyKind::Spacecraft};
        for (BodyKind kind : order) {
            for (size_t i = 0; i < bodies.size(); ++i) {
                if (bodies[i].kind != kind || !state.valid[i]) continue;
                if (kind == BodyKind::Moon && bodies[i].parent != system && static_cast<int>(i) != cam.focus &&
                    bodies[i].naif != 301)
                    continue;
                tryLabel(i, (kind == BodyKind::Moon ? 8.0f : 16.0f) * view.uiScale);
            }
        }
    }
    if (view.hud) {
        // The HUD is laid out in text pixels and drawn uiScale times larger.
        const float u = std::max(view.uiScale, 1.0f);
        textScale_ = u;
        hud(state, cam, view, static_cast<int>(static_cast<float>(width) / u), static_cast<int>(static_cast<float>(height) / u));
        textScale_ = 1;
    }

    // ---- submit
    StrataFrameDesc frame{};
    frame.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH;
    const float night[3] = {0.004f, 0.005f, 0.012f};
    const std::array<float, 3> sky = view.ship ? skyColor(*view.ship) : std::array<float, 3>{0, 0, 0};
    for (int c = 0; c < 3; ++c) frame.clear_color[c] = std::max(night[c], sky[static_cast<size_t>(c)]);
    frame.clear_color[3] = 1;
    engine_.beginFrame(frame);
    engine_.submitPrimitives(items_.data(), items_.size());
    engine_.submitLines(lines_.data(), lines_.size());
    engine_.submitPoints(points_.data(), points_.size());
    engine_.submitText(texts_.data(), texts_.size());
    engine_.endFrame();
    report.spheres += static_cast<int>(items_.size());
    report.points = static_cast<int>(points_.size());
    report.lineVertices = static_cast<int>(lines_.size());
    return report;
}

void Renderer::flightHud(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width,
                         int height) {
    const Ship& ship = *view.ship;
    const auto& bodies = sys_.bodies();
    const float big = width >= 1000 ? 16.0f : 8.0f, small = 8;
    float y = static_cast<float>(height) - 14 - small - 4 * (big + 6) - (small + 6);
    if (!view.prompt.empty()) y -= big + 6;
    const bool missionShown = !view.missionLine.empty() || !view.missionGoal.empty();
    if (missionShown) y -= big + 6 + small + 6;

    // DRIVE  thrusters  cruise  [WARP 1]  warp 2  warp 3
    float x = 10;
    auto put = [&](const std::string& s, const float color[4], float size) {
        text(x, y, size, color, s);
        x += size * static_cast<float>(s.size());
    };
    if (missionShown) {
        // MISSION  The Galilean moons  2/4  3:12.4
        // next: land on Ganymede   best 5:01.0
        text(10, y, big, view.missionTone == HudTone::Go ? kGo : kWhite, view.missionLine);
        y += big + 6;
        text(10, y, small, toneColor(view.missionTone), view.missionGoal);
        y += small + 6;
    }
    put("DRIVE  ", kDim, big);
    for (int i = 0; i < kDriveTierCount; ++i) {
        std::string name = kDriveTiers[i].name;
        if (i == ship.tier()) {
            for (char& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            put("[" + name + "]  ", i >= 2 ? kGo : kWhite, big);
        } else {
            put(name + "  ", kDim, big);
        }
    }
    y += big + 6;

    // SPEED  149,896 km/s  (0.50 c)    THROTTLE [########--] 80%
    x = 10;
    put("SPEED  ", kDim, big);
    put(speedText(ship.speed), kWhite, big);
    const int bars = static_cast<int>(std::lround(std::fabs(ship.throttle()) * 10));
    put("    THROTTLE [" + std::string(static_cast<size_t>(bars), '#') + std::string(static_cast<size_t>(10 - bars), '-') + "] ",
        kDim, big);
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s%.0f%%", ship.throttle() < 0 ? "reverse " : "", std::fabs(ship.throttle()) * 100);
    put(buf, kWhite, big);
    if (ship.air() >= 0 || ship.heat() >= 0.01) {
        std::snprintf(buf, sizeof buf, "   HEAT %.0f%%", std::min(ship.heat(), 1.0) * 100);
        put(buf, ship.heat() >= 0.75 ? kAlarm : ship.heat() >= 0.4 ? kWarn : kWhite, big);
    }
    y += big + 6;

    // What is happening: autopilot, mass lock, touchdown, arrived.
    auto name = [&](int i) { return bodies[static_cast<size_t>(i)].name; };
    x = 10;
    if (ship.autopilot()) put((ship.landing() ? "LANDING on " : "AUTOPILOT to ") + name(ship.target()), kGo, big);
    const Touchdown& td = ship.touchdown();
    if (ship.heat() >= 0.75) {
        if (ship.autopilot()) put("   ", kDim, big);
        put("HOT: slow down or burn up", kAlarm, big);
    } else if (ship.massLock() >= 0) {
        if (ship.autopilot()) put("   ", kDim, big);
        put("MASS LOCK: " + name(ship.massLock()) + " holds the drive", kWarn, big);
    } else if (td.grade == Touchdown::Soft) {
        put("LANDED on " + name(td.body) + "  " + verticalText(td.impactKmPerS), kGo, big);
    } else if (td.grade == Touchdown::Hard) {
        put("HARD LANDING on " + name(td.body) + "  " + verticalText(td.impactKmPerS), kWarn, big);
    } else if (td.grade == Touchdown::Crash) {
        put("CRASHED into " + name(td.body) + " at " + verticalText(td.impactKmPerS), kAlarm, big);
    } else if (td.grade == Touchdown::Burned) {
        std::snprintf(buf, sizeof buf, "BURNED UP in %s's air at %.1f km/s", name(td.body).c_str(), td.impactKmPerS);
        put(buf, kAlarm, big);
    } else if (td.grade == Touchdown::Lost) {
        put(bodies[static_cast<size_t>(td.body)].kind == BodyKind::Star ? "BURNED UP in " + name(td.body)
                                                                         : "LOST in " + name(td.body) + "'s clouds",
            kAlarm, big);
    } else if (ship.arrived() >= 0) {
        put("ARRIVED at " + name(ship.arrived()), kGo, big);
    }
    y += big + 6;

    // TARGET Mars  0.512 au  ETA 4 min 13 s
    x = 10;
    if (cam.focus >= 0 && state.valid[static_cast<size_t>(cam.focus)]) {
        const FlightBody target{drawn_[static_cast<size_t>(cam.focus)], drawnRadius_[static_cast<size_t>(cam.focus)], 0, true, {}, true, {}, 1};
        const Vec3 rel = target.centre - ship.position;
        const double dist = length(rel);
        put("TARGET ", kDim, big);
        put(name(cam.focus) + "  " + distanceText(dist), kWhite, big);
        const double closing = dist > 0 ? ship.speed * dot(ship.forward, rel / dist) : 0;
        const double remaining = dist - (ship.landing() ? target.radius : Ship::standoff(target));
        if (closing > 0 && remaining > 0) put("   ETA " + durationText(remaining / closing), kWhite, big);
    } else {
        put("no target: Tab or 0-9 picks one", kDim, big);
    }
    y += big + 6;

    std::string near;
    if (ship.nearest() >= 0) near = "nearest surface: " + name(ship.nearest()) + ", " + distanceText(ship.altitude());
    if (ship.landed() >= 0) {
        near = "on " + name(ship.landed()) + ": nose up and throttle to lift off";
    } else if (td.grade == Touchdown::Crash) {
        near += "   the ship was put back above the surface";
    } else if (td.grade == Touchdown::Lost) {
        near += "   no solid surface: put back outside it";
    } else if (td.grade == Touchdown::Burned) {
        near += "   too fast in the air: put back above it";
    } else if (ship.nearest() >= 0 && ship.climb() != 0) {
        near += (ship.climb() < 0 ? "   descending " : "   climbing ") + verticalText(ship.climb());
    }
    if (ship.air() >= 0) {
        std::snprintf(buf, sizeof buf, "   air %.2g kg/m3", ship.airDensity());
        near += buf;
    }
    if (ship.anchor() > 0) near += "   moving with " + name(ship.anchor());
    if (view.scale.mode == ScaleMode::Compressed) near += "   (speeds and distances are of the COMPRESSED drawing)";
    text(10, y, small, kDim, near);

    if (view.cockpit) {
        text(static_cast<float>(width) / 2 - big / 2, static_cast<float>(height) / 2 - big / 2, big, kGo, "+");
    }
}

void Renderer::hud(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width, int height) {
    const auto& bodies = sys_.bodies();
    float y = 10;
    const float big = width >= 1000 ? 16.0f : 8.0f, small = 8;
    text(10, y, big, kWhite, view.timeLine);
    y += big + 6;
    char buf[256];
    if (view.scale.mode == ScaleMode::True) {
        std::snprintf(buf, sizeof buf, "TRUE DISTANCES  |  bodies enlarged x%g, Sun x%g", view.scale.enlarge,
                      std::min(view.scale.enlarge, kSunEnlargeCap));
        text(10, y, big, kWhite, buf);
    } else {
        text(10, y, big, kWarn, "COMPRESSED (LOG) DISTANCES AND SIZES  |  NOT TO SCALE");
    }
    y += big + 8;

    if (!cam.freeFly) {
        const size_t i = static_cast<size_t>(cam.focus);
        const Body& b = bodies[i];
        std::snprintf(buf, sizeof buf, "%s%s  (%s)", cam.ship ? "TARGET  " : "", b.name.c_str(), kindName(b.kind));
        text(10, y, big, kWhite, buf);
        y += big + 4;
        std::string line;
        if (b.radiusKm > 0) line += "radius " + thousands(b.radiusKm) + " km";
        if (b.rotationPeriodHours > 0) {
            const double h = b.rotationPeriodHours;
            std::snprintf(buf, sizeof buf, "   rotation %.2f %s%s", h > 72 ? h / 24 : h, h > 72 ? "d" : "h",
                          b.pmRate < 0 ? " (retrograde)" : "");
            line += buf;
        }
        Vec3 pole;
        Elements el;
        if (b.kind != BodyKind::Moon && sys_.pole(static_cast<int>(i), state.jdTdb, pole) &&
            sys_.osculating(static_cast<int>(i), state.jdTdb, state, el)) {
            const double si = std::sin(el.i * kDegToRad), ci = std::cos(el.i * kDegToRad);
            const double O = el.node * kDegToRad;
            const Vec3 normal{si * std::sin(O), -si * std::cos(O), ci};
            std::snprintf(buf, sizeof buf, "   axial tilt %.1f deg", std::acos(std::clamp(dot(pole, normal), -1.0, 1.0)) / kDegToRad);
            line += buf;
        }
        if (!line.empty()) {
            text(10, y, small, kDim, line);
            y += small + 4;
        }
        if (i != 0) {
            const double fromSun = length(state.positions[i] - state.positions[0]) / kAuKm;
            std::snprintf(buf, sizeof buf, "%.4f au from the Sun", fromSun);
            line = buf;
            if (b.parent > 0) {
                const double fromParent = length(state.positions[i] - state.positions[static_cast<size_t>(b.parent)]);
                line += "   " + thousands(fromParent) + " km from " + bodies[static_cast<size_t>(b.parent)].name;
            }
            text(10, y, small, kDim, line);
            y += small + 4;
        }
        const char* src = b.source == BodySource::Spk ? "position: JPL DE440s"
                          : b.source == BodySource::Table ? "position: JPL Horizons osculating elements, interpolated"
                                                          : "position: JPL Horizons trajectory";
        text(10, y, small, kDim, src);
        y += small + 4;
        if (state.extrapolated[i]) {
            text(10, y, small, kWarn, "outside this body's element table: extrapolated two-body");
            y += small + 4;
        }
        if (!state.valid[i]) {
            text(10, y, small, kWarn, "no trajectory data at this date");
            y += small + 4;
        }
    } else {
        text(10, y, big, kWhite, "free flight");
        y += big + 4;
    }
    if (view.smallBodies) {
        std::snprintf(buf, sizeof buf, "%zu small bodies (SBDB elements, two-body)", sys_.smallBodies().size());
        text(10, y, small, kDim, buf);
    }
    if (view.ship) flightHud(state, cam, view, width, height);
    if (!view.prompt.empty()) text(10, static_cast<float>(height) - 60, big, kWarn, view.prompt);
    if (view.keyHints) {
        text(10, static_cast<float>(height) - 14 - small, small, kDim,
             view.ship ? "W/S throttle   wheel drive   drag/arrows/A D steer   Q/E roll   Tab target   T autopilot   "
                         "G land   Z stop   C view   V leave"
                       : "H help   Tab next body   M scale mode   Space pause   V ship   I missions");
    }

    if (view.help) {
        static const char* lines[] = {
            "Space        play / pause",
            ", .          slower / faster (negative runs backwards)",
            "N            now, at real time        J  jump to a date",
            "Tab, S-Tab   next / previous body     0-9  Sun, planets, Pluto",
            "Home         the Sun",
            "Left drag    orbit the focus          Wheel, PgUp/PgDn  zoom",
            "F            free flight: WASD move, Q/E down/up, drag to look",
            "V            ship: W/S throttle, wheel drive tier, T autopilot, G land",
            "I            missions: 1-9, 0 start one; Backspace abandons",
            "M            TRUE / COMPRESSED scale",
            "[ ]          body enlargement (TRUE mode)",
            "O  L  B  X   orbits, labels, small bodies, spin axes",
#if defined(__EMSCRIPTEN__)
            "Touch        drag to orbit, pinch to zoom; the toolbar does the rest",
#else
            "P            screenshot         U  export USD layer",
            "Esc          quit",
#endif
        };
        float hy = static_cast<float>(height) / 2 - 130;
        const float hx = static_cast<float>(width) / 2 - 300;
        for (const char* l : lines) {
            text(hx, hy, big, kWhite, l);
            hy += big + 6;
        }
    }

    // The mission list (I), over the body's details; on a small screen without
    // the one-line briefs, so it clears the ship's HUD.
    float py = big > small ? 130 : 110;
    for (const HudLine& l : view.panel) {
        if (l.small && big == small) continue;
        const float size = l.small ? small : big;
        text(10, py, size, toneColor(l.tone), l.text);
        py += size + (l.small ? 8 : 4);
    }
}

}  // namespace orrery
