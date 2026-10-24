#include "orrery/missions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace orrery {
namespace {

constexpr double kFarSideCos = -0.5;  // more than 120 degrees from the point under the other body

MissionGoal land(const std::string& body) { return {MissionGoal::Land, body, "land on " + body, -90, "", 0}; }

MissionGoal pass(const std::string& body, double withinKm, const std::string& text) {
    return {MissionGoal::Pass, body, text, -90, "", withinKm};
}

// The closest the segment a..b comes to the origin.
double closest(const Vec3& a, const Vec3& b) {
    const Vec3 d = b - a;
    const double dd = dot(d, d);
    const double t = dd > 0 ? std::clamp(-dot(a, d) / dd, 0.0, 1.0) : 0.0;
    return length(a + d * t);
}

}  // namespace

const std::vector<Mission>& missions() {
    static const std::vector<Mission> list = [] {
        std::vector<Mission> m;
        const std::string date = "2026-11-24";
        m.push_back({"galilean", "tour", "The Galilean moons", "Land on Io, Europa, Ganymede and Callisto, in any order.",
                     date, "Jupiter", {land("Io"), land("Europa"), land("Ganymede"), land("Callisto")}, 0});
        m.push_back({"mars-system", "tour", "Mars and its moons", "Land on Phobos, Deimos and Mars.", date, "Mars",
                     {land("Phobos"), land("Deimos"), land("Mars")}, 0});
        m.push_back({"inner-planets", "tour", "The inner planets", "From Earth, land on Mercury, Venus and Mars.", date,
                     "Earth", {land("Mercury"), land("Venus"), land("Mars")}, 0});
        // Races: par is a second under the landing autopilot's time at this date
        // (24.97 s and 46.07 s; Pluto's thin air costs the autopilot 0.1 s). Mass
        // lock caps everyone's approach the same way; the time is won at the end,
        // on thrusters, where a hard landing counts.
        m.push_back({"moon-run", "race", "Moon run",
                     "From Earth, land on the Moon. Beat the autopilot: a hard landing counts.", date, "Earth",
                     {land("Moon")}, 24});
        m.push_back({"pluto-dash", "race", "Pluto dash",
                     "From Earth, land on Pluto, 5 billion km out. A hard landing counts.", date, "Earth",
                     {land("Pluto")}, 45});
        MissionGoal far = land("Moon");
        far.text = "land on the far side of the Moon, facing away from Earth";
        far.awayFrom = "Earth";
        m.push_back({"lunar-far-side", "precision", "The far side",
                     "Land on the side of the Moon that never faces Earth.", date, "Earth", {far}, 0});
        MissionGoal pole = land("Mars");
        pole.text = "land within 10 degrees of Mars's north pole";
        pole.minLatitudeDeg = 80;
        m.push_back({"mars-north-pole", "precision", "North pole of Mars",
                     "Land within 10 degrees of Mars's north pole.", date, "Mars", {pole}, 0});
        m.push_back({"saturn-skim", "sightseeing", "Skim Saturn",
                     "Pass within 20,000 km of Saturn's cloud tops, inside its rings. It has no surface: do not touch.",
                     date, "Saturn", {pass("Saturn", 20000, "pass within 20,000 km of Saturn's cloud tops")}, 0});
        m.push_back({"halley", "sightseeing", "Halley's comet", "Fly out to Halley's comet, near the far end of its orbit.",
                     date, "Earth", {pass("1P/Halley", 10000, "pass within 10,000 km of Halley's comet")}, 0});
        m.push_back({"voyager-1", "sightseeing", "Catch Voyager 1", "Catch up with Voyager 1, the farthest spacecraft.",
                     date, "Earth", {pass("Voyager 1", 10000, "pass within 10,000 km of Voyager 1")}, 0});
        // A race through air: the landing autopilot keeps to 60% of the burn
        // speed and takes 32.50 s. Par is 2.5 s under that: riding the heat at
        // 80% of the burn speed wins about 3.5 s (test_atmosphere).
        m.push_back({"hot-entry", "race", "Hot entry",
                     "From the Moon, land on Earth. The autopilot runs cool: ride the heat, but do not burn up.", date,
                     "Moon", {land("Earth")}, 30});
        return m;
    }();
    return list;
}

int findMission(const std::string& id) {
    const auto& list = missions();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

void missionStartPose(const FlightBody& start, const FlightBody& toward, Vec3& position, Vec3& forward) {
    const Vec3 rel = toward.centre - start.centre;
    forward = length(rel) > 0 ? normalize(rel) : Vec3{1, 0, 0};
    position = start.centre + forward * Ship::standoff(start);
}

bool MissionRun::start(const Mission& m, const std::function<int(const std::string&)>& find) {
    std::vector<int> bodies, away;
    if (find(m.start) < 0) return false;
    for (const MissionGoal& g : m.goals) {
        bodies.push_back(find(g.body));
        away.push_back(g.awayFrom.empty() ? -1 : find(g.awayFrom));
        if (bodies.back() < 0 || (!g.awayFrom.empty() && away.back() < 0)) return false;
    }
    mission_ = &m;
    complete_ = false;
    seconds_ = 0;
    goalBody_ = bodies;
    awayBody_ = away;
    done_.assign(m.goals.size(), false);
    lastRel_.assign(m.goals.size(), Vec3{});
    haveLast_.assign(m.goals.size(), false);
    miss_.clear();
    return true;
}

void MissionRun::update(double dt, const Ship& ship, const std::vector<FlightBody>& bodies) {
    if (!running()) return;
    seconds_ += dt;
    miss_.clear();
    const int n = static_cast<int>(bodies.size());
    for (size_t i = 0; i < done_.size(); ++i) {
        const MissionGoal& g = mission_->goals[i];
        const int bi = goalBody_[i];
        if (done_[i] || bi >= n || !bodies[static_cast<size_t>(bi)].present) continue;
        const FlightBody& b = bodies[static_cast<size_t>(bi)];
        if (g.kind == MissionGoal::Pass) {
            const Vec3 rel = ship.position - b.centre;
            const double near = haveLast_[i] ? closest(lastRel_[i], rel) : length(rel);
            if (near - b.radius <= g.withinKm) done_[i] = true;
            lastRel_[i] = rel;
            haveLast_[i] = true;
            continue;
        }
        if (ship.landed() != bi) continue;  // a crash or a loss never lands
        const Vec3 normal = normalize(ship.position - b.centre);
        char buf[160];
        const double latitude = std::asin(std::clamp(dot(normal, b.frame.z), -1.0, 1.0)) / kDegToRad;
        if (latitude < g.minLatitudeDeg) {
            std::snprintf(buf, sizeof buf, "landed at latitude %+.0f: this goal needs %+.0f or more", latitude,
                          g.minLatitudeDeg);
            miss_ = buf;
            continue;
        }
        const int ai = awayBody_[i];
        if (ai >= 0 && ai < n && bodies[static_cast<size_t>(ai)].present) {
            const Vec3 toward = normalize(bodies[static_cast<size_t>(ai)].centre - b.centre);
            if (dot(normal, toward) > kFarSideCos) {
                std::snprintf(buf, sizeof buf, "landed %.0f degrees from the point under %s: the far side starts at 120",
                              std::acos(std::clamp(dot(normal, toward), -1.0, 1.0)) / kDegToRad, g.awayFrom.c_str());
                miss_ = buf;
                continue;
            }
        }
        done_[i] = true;
    }
    complete_ = std::all_of(done_.begin(), done_.end(), [](bool d) { return d; });
}

int MissionRun::goalsDone() const { return static_cast<int>(std::count(done_.begin(), done_.end(), true)); }

int MissionRun::nextGoal() const {
    for (size_t i = 0; i < done_.size(); ++i) {
        if (!done_[i]) return static_cast<int>(i);
    }
    return -1;
}

std::map<std::string, double> parseRecords(const std::string& text) {
    std::map<std::string, double> out;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string id, extra;
        double seconds = 0;
        if (!(fields >> id >> seconds) || (fields >> extra) || !(seconds > 0) || !std::isfinite(seconds)) continue;
        out[id] = seconds;
    }
    return out;
}

std::string formatRecords(const std::map<std::string, double>& records) {
    std::string out;
    char buf[64];
    for (const auto& [id, seconds] : records) {
        std::snprintf(buf, sizeof buf, " %.3f\n", seconds);
        out += id + buf;
    }
    return out;
}

bool recordTime(std::map<std::string, double>& records, const std::string& id, double seconds) {
    const auto it = records.find(id);
    if (it != records.end() && it->second <= seconds) return false;
    records[id] = seconds;
    return true;
}

std::string clockText(double seconds) {
    const double tenths = std::floor(std::max(seconds, 0.0) * 10 + 0.5);
    const long t = static_cast<long>(tenths);
    char buf[48];
    if (t >= 36000) {
        std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", t / 36000, t / 600 % 60, t / 10 % 60);
    } else {
        std::snprintf(buf, sizeof buf, "%ld:%02ld.%ld", t / 600, t / 10 % 60, t % 10);
    }
    return buf;
}

}  // namespace orrery
