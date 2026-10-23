#include "orrery/system.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "orrery/csv.hpp"
#include "orrery/time.hpp"

namespace orrery {
namespace {

constexpr double kGmSun = 132712440041.279419;  // km^3/s^2, DE440 (gm_de440.tpc BODY10_GM)

float srgbToLinear(double c) {
    return static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
}

BodyKind parseKind(const std::string& s) {
    if (s == "star") return BodyKind::Star;
    if (s == "planet") return BodyKind::Planet;
    if (s == "dwarf") return BodyKind::Dwarf;
    if (s == "moon") return BodyKind::Moon;
    if (s == "comet") return BodyKind::Comet;
    if (s == "spacecraft") return BodyKind::Spacecraft;
    throw std::runtime_error("unknown body kind " + s);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

const char* kindName(BodyKind kind) {
    switch (kind) {
        case BodyKind::Star: return "star";
        case BodyKind::Planet: return "planet";
        case BodyKind::Dwarf: return "dwarf planet";
        case BodyKind::Moon: return "moon";
        case BodyKind::Comet: return "comet";
        case BodyKind::Spacecraft: return "spacecraft";
    }
    return "?";
}

bool hasSolidSurface(BodyKind kind, long naif) {
    return kind != BodyKind::Star && naif != 599 && naif != 699 && naif != 799 && naif != 899;
}

SolarSystem::SolarSystem(const std::string& dataDir, const std::string& kernelPath) {
    spk_ = std::make_unique<SpkFile>(kernelPath.empty() ? dataDir + "/kernels/de440s.bsp" : kernelPath);
    const std::string catalog = dataDir + "/catalog/";
    if (!std::ifstream(catalog + "bodies.csv")) {
        // The catalogue is built from data fetched from JPL, never committed (DATA_SOURCES.md).
        throw std::runtime_error("no catalogue in " + catalog +
                                 ": run tools/fetch_data.py all, then tools/build_catalog.py (see README)");
    }

    const CsvTable b = readCsv(catalog + "bodies.csv");
    for (const auto& row : b.rows) {
        Body body;
        body.name = b.text(row, "name");
        body.naif = std::stol(b.text(row, "naif"));
        body.kind = parseKind(b.text(row, "kind"));
        body.parentNaif = std::stol(b.text(row, "parent"));
        const std::string source = b.text(row, "source");
        if (source == "spk") {
            body.source = BodySource::Spk;
            body.spkTarget = static_cast<int>(b.number(row, "spk"));
        } else if (source == "table") {
            body.source = BodySource::Table;
            body.table = ElementTable::load(catalog + b.text(row, "table"));
        } else if (source == "trajectory") {
            body.source = BodySource::Trajectory;
            const CsvTable t = readCsv(catalog + b.text(row, "table"));
            for (const auto& r : t.rows) {
                TrajectorySample s;
                s.jd = t.number(r, "jd_tdb");
                s.position = {t.number(r, "x_km"), t.number(r, "y_km"), t.number(r, "z_km")};
                s.velocity = {t.number(r, "vx_km_s"), t.number(r, "vy_km_s"), t.number(r, "vz_km_s")};
                body.trajectory.push_back(s);
            }
        } else {
            throw std::runtime_error("unknown source " + source + " for " + body.name);
        }
        body.radiusKm = b.numberOr(row, "radius_km", 0);
        body.radiusSource = b.text(row, "radius_source");
        if (!b.text(row, "pole_ra0").empty()) {
            body.hasPole = true;
            body.poleRa0 = b.number(row, "pole_ra0");
            body.poleRaRate = b.number(row, "pole_ra_rate");
            body.poleDec0 = b.number(row, "pole_dec0");
            body.poleDecRate = b.number(row, "pole_dec_rate");
            body.pmW0 = b.number(row, "pm_w0");
            body.pmRate = b.number(row, "pm_rate");
            if (body.pmRate != 0) body.rotationPeriodHours = 360.0 / std::fabs(body.pmRate) * 24.0;
        }
        const double period = b.numberOr(row, "rotation_period_h", 0);
        if (period > 0) body.rotationPeriodHours = period;
        body.rotationSource = b.text(row, "rotation_source");
        body.gm = b.numberOr(row, "gm", 0);
        body.systemGm = b.numberOr(row, "system_gm", 0);
        for (int i = 0; i < 3; ++i) body.color[i] = srgbToLinear(b.number(row, i == 0 ? "r" : i == 1 ? "g" : "b"));
        bodies_.push_back(std::move(body));
    }
    if (bodies_.empty() || bodies_[0].naif != 10) throw std::runtime_error("bodies.csv must start with the Sun");
    for (Body& body : bodies_) {
        for (size_t j = 0; j < bodies_.size(); ++j) {
            if (bodies_[j].naif == body.parentNaif) body.parent = static_cast<int>(j);
        }
    }

    const CsvTable g = readCsv(catalog + "groups.csv");
    for (const auto& row : g.rows) {
        SmallBodyGroup grp;
        grp.id = g.text(row, "group");
        grp.label = g.text(row, "label");
        grp.count = static_cast<int>(g.number(row, "count"));
        for (int i = 0; i < 3; ++i) grp.color[i] = srgbToLinear(g.number(row, i == 0 ? "r" : i == 1 ? "g" : "b"));
        groups_.push_back(grp);
    }
    const CsvTable s = readCsv(catalog + "small_bodies.csv");
    smallBodies_.reserve(s.rows.size());
    for (const auto& row : s.rows) {
        SmallBody sb;
        const std::string& id = s.text(row, "group");
        sb.group = -1;
        for (size_t j = 0; j < groups_.size(); ++j) {
            if (groups_[j].id == id) sb.group = static_cast<int>(j);
        }
        if (sb.group < 0) throw std::runtime_error("small body in unknown group " + id);
        Elements& e = sb.elements;
        e.a = s.number(row, "a_au");
        e.e = s.number(row, "e");
        e.i = s.number(row, "i_deg");
        e.node = s.number(row, "node_deg");
        e.peri = s.number(row, "peri_deg");
        e.m = s.number(row, "m_deg");
        e.epochJd = s.number(row, "epoch_jd_tdb");
        const double gmAu3PerDay2 = kGmSun * std::pow(kSecondsPerDay, 2) / std::pow(kAuKm, 3);
        e.n = s.numberOr(row, "n_deg_per_day", std::sqrt(gmAu3PerDay2 / std::pow(std::fabs(e.a), 3)) / kDegToRad);
        smallBodies_.push_back(sb);
    }

    const CsvTable r = readCsv(catalog + "rings.csv");
    for (const auto& row : r.rows) {
        Ring ring;
        const long planet = static_cast<long>(r.number(row, "planet"));
        for (size_t j = 0; j < bodies_.size(); ++j) {
            if (bodies_[j].naif == planet) ring.planet = static_cast<int>(j);
        }
        ring.name = r.text(row, "name");
        ring.innerKm = r.number(row, "inner_km");
        ring.outerKm = r.number(row, "outer_km");
        for (int i = 0; i < 3; ++i) ring.color[i] = srgbToLinear(r.number(row, i == 0 ? "r" : i == 1 ? "g" : "b"));
        ring.alpha = static_cast<float>(r.number(row, "alpha"));
        if (ring.planet >= 0) rings_.push_back(ring);
    }
}

int SolarSystem::find(const std::string& name) const {
    const std::string key = lower(name);
    for (size_t i = 0; i < bodies_.size(); ++i) {
        if (lower(bodies_[i].name) == key) return static_cast<int>(i);
    }
    // Comets and spacecraft by a prefix ("halley" finds "1P/Halley").
    for (size_t i = 0; i < bodies_.size(); ++i) {
        if (lower(bodies_[i].name).find(key) != std::string::npos) return static_cast<int>(i);
    }
    return -1;
}

double SolarSystem::firstJd() const { return kJ2000 + spk_->coverageStartEt() / kSecondsPerDay + 1.0; }
double SolarSystem::lastJd() const { return kJ2000 + spk_->coverageEndEt() / kSecondsPerDay - 1.0; }

Vec3 SolarSystem::spkPosition(int target, double jd) const {
    return icrfToEcliptic(spk_->position(target, 0, (jd - kJ2000) * kSecondsPerDay));
}

void SolarSystem::evaluate(double jd, SystemState& out, bool withSmallBodies) const {
    const size_t n = bodies_.size();
    out.jdTdb = jd;
    out.positions.assign(n, Vec3{});
    out.valid.assign(n, true);
    out.extrapolated.assign(n, false);
    std::vector<Vec3> relative(n);  // tables and trajectories: relative to the parent
    std::vector<bool> isRelative(n, false);

    for (size_t i = 0; i < n; ++i) {
        const Body& b = bodies_[i];
        if (b.source == BodySource::Spk) {
            out.positions[i] = spkPosition(b.spkTarget, jd);
        } else if (b.source == BodySource::Table) {
            bool ex = false;
            relative[i] = b.table.position(jd, &ex);
            out.extrapolated[i] = ex;
            isRelative[i] = true;
        } else {
            const auto& tr = b.trajectory;
            if (tr.empty() || jd < tr.front().jd || jd > tr.back().jd) {
                out.valid[i] = false;
                relative[i] = tr.empty() ? Vec3{} : (jd < tr.front().jd ? tr.front().position : tr.back().position);
            } else {
                const auto it = std::upper_bound(tr.begin(), tr.end(), jd,
                                                 [](double t, const TrajectorySample& s) { return t < s.jd; });
                const TrajectorySample& a = it == tr.begin() ? *it : *(it - 1);
                const TrajectorySample& c = it == tr.end() ? tr.back() : *it;
                const double h = (c.jd - a.jd) * kSecondsPerDay;
                if (h <= 0) {
                    relative[i] = a.position;
                } else {
                    // Cubic Hermite with the sampled velocities.
                    const double t = (jd - a.jd) * kSecondsPerDay / h, t2 = t * t, t3 = t2 * t;
                    relative[i] = a.position * (2 * t3 - 3 * t2 + 1) + a.velocity * (h * (t3 - 2 * t2 + t)) +
                                  c.position * (-2 * t3 + 3 * t2) + c.velocity * (h * (t3 - t2));
                }
            }
            isRelative[i] = true;
        }
    }
    // A planet's DE position is its system's barycentre: move it to the
    // planet's centre with its moons' GMs (only moons in the catalogue).
    for (size_t i = 0; i < n; ++i) {
        const Body& p = bodies_[i];
        if (p.source != BodySource::Spk || p.systemGm <= 0) continue;
        Vec3 offset;
        for (size_t j = 0; j < n; ++j) {
            if (bodies_[j].parent == static_cast<int>(i) && isRelative[j] && bodies_[j].gm > 0) {
                offset += relative[j] * (bodies_[j].gm / p.systemGm);
            }
        }
        out.positions[i] -= offset;
    }
    // Parents come before children in bodies.csv except where they do not;
    // resolve relative positions until all are placed.
    std::vector<bool> placed(n, false);
    for (size_t i = 0; i < n; ++i) placed[i] = !isRelative[i];
    for (int pass = 0; pass < 4; ++pass) {
        for (size_t i = 0; i < n; ++i) {
            if (placed[i]) continue;
            const int parent = bodies_[i].parent;
            if (parent >= 0 && placed[static_cast<size_t>(parent)]) {
                out.positions[i] = out.positions[static_cast<size_t>(parent)] + relative[i];
                placed[i] = true;
            }
        }
    }

    if (withSmallBodies) {
        const Vec3 sun = out.positions[0];
        out.smallBodies.resize(smallBodies_.size());
        for (size_t k = 0; k < smallBodies_.size(); ++k) {
            out.smallBodies[k] = sun + keplerPosition(smallBodies_[k].elements, jd) * kAuKm;
        }
    } else {
        out.smallBodies.clear();
    }
}

Vec3 SolarSystem::velocity(int body, double jd) const {
    const Body& b = bodies_[static_cast<size_t>(body)];
    if (b.source == BodySource::Spk) {
        Vec3 p, v;
        spk_->state(b.spkTarget, 0, (jd - kJ2000) * kSecondsPerDay, p, &v);
        return icrfToEcliptic(v);
    }
    SystemState a, c;
    const double h = 60.0 / kSecondsPerDay;
    evaluate(jd - h, a, false);
    evaluate(jd + h, c, false);
    return (c.positions[static_cast<size_t>(body)] - a.positions[static_cast<size_t>(body)]) / 120.0;
}

bool SolarSystem::osculating(int body, double jd, const SystemState& state, Elements& out) const {
    const Body& b = bodies_[static_cast<size_t>(body)];
    if (b.kind == BodyKind::Spacecraft || body == 0) return false;
    if (b.source == BodySource::Table) {
        out = b.table.at(jd);
        return true;
    }
    // DE bodies: from the state relative to the parent.
    const int parent = b.parent;
    if (parent < 0) return false;
    const Vec3 r = state.positions[static_cast<size_t>(body)] - state.positions[static_cast<size_t>(parent)];
    const Vec3 v = velocity(body, jd) - velocity(parent, jd);
    const Body& p = bodies_[static_cast<size_t>(parent)];
    const double gm = (p.naif == 10 ? kGmSun : p.gm) + std::max(b.systemGm, b.gm);
    out = elementsFromState(r, v, gm, jd);
    return true;
}

bool SolarSystem::pole(int body, double jd, Vec3& out) const {
    const Body& b = bodies_[static_cast<size_t>(body)];
    if (!b.hasPole) return false;
    const double T = (jd - kJ2000) / kDaysPerJulianCentury;
    out = icrfToEcliptic(raDecToIcrf(b.poleRa0 + b.poleRaRate * T, b.poleDec0 + b.poleDecRate * T));
    return true;
}

bool SolarSystem::frame(int body, double jd, BodyFrame& out) const {
    const Body& b = bodies_[static_cast<size_t>(body)];
    if (!b.hasPole) return false;
    const double d = jd - kJ2000;
    const double T = d / kDaysPerJulianCentury;
    out = bodyFrame(b.poleRa0 + b.poleRaRate * T, b.poleDec0 + b.poleDecRate * T, b.pmW0 + b.pmRate * d);
    return true;
}

}  // namespace orrery
