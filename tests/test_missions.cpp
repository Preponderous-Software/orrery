// Missions: the list itself (checked against the real catalogue at each
// mission's date), the start pose, goals completing as the ship flies (landing
// tours in any order, crashes not counting, latitude and far-side landings,
// flybys that cross the goal within one step), and the best-time records.
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "orrery/missions.hpp"
#include "orrery/system.hpp"
#include "orrery/time.hpp"

#ifndef ORRERY_SOURCE_DIR
#define ORRERY_SOURCE_DIR "."
#endif

using namespace orrery;

namespace {

constexpr double kDt = 1.0 / 60;
constexpr double kEarthR = 6371, kMoonR = 1737.4, kMarsR = 3390;
const Vec3 kEarth{kAuKm, 0, 0}, kMoon = kEarth + Vec3{384400, 0, 0}, kMars{-1.5 * kAuKm, 0, 0};

// The Sun, Earth, the Moon, Mars and a probe with no radius, indexed 0..4.
std::vector<FlightBody> world() {
    std::vector<FlightBody> b(5);
    b[0] = {{0, 0, 0}, 695700, 1e308, true, {}, false};
    b[1] = {kEarth, kEarthR, 25 * kEarthR, true, {}, true};
    b[2] = {kMoon, kMoonR, 12 * kMoonR, true, {}, true};
    b[3] = {kMars, kMarsR, 25 * kMarsR, true, {}, true};
    b[4] = {{0, 3 * kAuKm, 0}, 0, 0, true, {}, true};
    return b;
}

int find(const std::string& name) {
    const char* names[] = {"Sun", "Earth", "Moon", "Mars", "Probe"};
    for (int i = 0; i < 5; ++i) {
        if (name == names[i]) return i;
    }
    return -1;
}

MissionGoal landOn(const std::string& body) {
    MissionGoal g;
    g.body = body;
    g.text = "land on " + body;
    return g;
}

// Flies the ship onto a body from `dir` (a unit vector from its centre) on
// thrusters, updating the run every step, until it touches down.
Ship descend(MissionRun& run, const std::vector<FlightBody>& bodies, int body, const Vec3& dir, double throttle) {
    const FlightBody& b = bodies[static_cast<size_t>(body)];
    Ship ship;
    ship.place(b.centre + dir * (b.radius + 20), dir * -1.0);
    ship.setThrottle(throttle);
    for (double t = 0; t < 600 && ship.touchdown().grade == Touchdown::None; t += kDt) {
        ship.step(kDt, {}, bodies);
        run.update(kDt, ship, bodies);
    }
    return ship;
}

}  // namespace

int main() {
    SECTION("the mission list: ten missions with unique ids, every kind, and pars on the races only");
    {
        const auto& list = missions();
        CHECK(list.size() == 10);
        std::set<std::string> ids, kinds;
        for (const Mission& m : list) {
            CHECK_MSG(ids.insert(m.id).second, m.id);
            kinds.insert(m.kind);
            CHECK_MSG(!m.title.empty() && !m.brief.empty() && !m.goals.empty(), m.id);
            CHECK_MSG((m.kind == "race") == (m.parSeconds > 0), m.id);
            CalendarDate c;
            CHECK_MSG(parseIsoDate(m.date, c), m.id);
            for (const MissionGoal& g : m.goals) {
                CHECK_MSG(!g.text.empty(), m.id);
                CHECK_MSG(g.kind == MissionGoal::Land || g.withinKm > 0, m.id);
            }
            CHECK(findMission(m.id) == static_cast<int>(&m - list.data()));
        }
        CHECK(kinds == (std::set<std::string>{"tour", "race", "precision", "sightseeing"}));
        CHECK(findMission("no-such-mission") == -1);
    }

    SECTION("every mission names real bodies, present at its date; landings are on solid ground");
    {
        const SolarSystem sys(std::string(ORRERY_SOURCE_DIR) + "/data");
        for (const Mission& m : missions()) {
            CalendarDate c;
            parseIsoDate(m.date, c);
            SystemState s;
            sys.evaluate(utcToTdb(julianDate(c)), s, false);
            auto present = [&](const std::string& name) {
                const int i = sys.find(name);
                return i >= 0 && s.valid[static_cast<size_t>(i)];
            };
            CHECK_MSG(present(m.start), m.id + ": " + m.start);
            for (const MissionGoal& g : m.goals) {
                CHECK_MSG(present(g.body), m.id + ": " + g.body);
                const int i = sys.find(g.body);
                if (i < 0) continue;
                const Body& b = sys.bodies()[static_cast<size_t>(i)];
                if (g.kind == MissionGoal::Land) CHECK_MSG(b.radiusKm > 0 && hasSolidSurface(b.kind, b.naif), g.body);
                if (g.minLatitudeDeg > -90) CHECK_MSG(b.hasPole, g.body + " needs a pole for a latitude");
                if (!g.awayFrom.empty()) CHECK_MSG(present(g.awayFrom), m.id + ": " + g.awayFrom);
            }
        }
    }

    SECTION("a mission starts at the standoff from its start body, facing the first goal");
    {
        const auto b = world();
        Vec3 pos, fwd;
        missionStartPose(b[1], b[2], pos, fwd);
        CHECK(length(fwd - Vec3{1, 0, 0}) < 1e-12);
        CHECK(length(pos - (kEarth + Vec3{Ship::standoff(b[1]), 0, 0})) < 1e-6);
    }

    SECTION("a landing tour completes in any order; a crash does not count; the clock stops at the end");
    {
        Mission m;
        m.id = "test-tour";
        m.start = "Earth";
        m.goals = {landOn("Earth"), landOn("Moon")};
        MissionRun run;
        Mission bad = m;
        bad.goals.push_back(landOn("Vulcan"));
        CHECK(!run.start(bad, find) && !run.running() && run.mission() == nullptr);

        CHECK(run.start(m, find) && run.running() && run.goalCount() == 2 && run.nextGoal() == 0);
        CHECK(run.goalBody(0) == 1 && run.goalBody(1) == 2);
        const auto bodies = world();
        Ship crash = descend(run, bodies, 2, {1, 0, 0}, 1.0);  // 2 km/s into the Moon
        CHECK(crash.touchdown().grade == Touchdown::Crash && !run.goalDone(1));

        Ship moon = descend(run, bodies, 2, {0, 0, 1}, 0.03);
        CHECK(moon.landed() == 2 && run.goalDone(1) && !run.goalDone(0));
        CHECK(run.goalsDone() == 1 && run.nextGoal() == 0 && run.running() && !run.complete());
        CHECK(run.miss().empty());

        Ship earth = descend(run, bodies, 1, {0, 1, 0}, 0.03);
        CHECK(earth.landed() == 1 && run.complete() && !run.running() && run.nextGoal() == -1);
        const double at = run.seconds();
        CHECK_MSG(at > 1, std::to_string(at));
        run.update(5, earth, bodies);
        CHECK(run.seconds() == at);
        run.abandon();
        CHECK(run.mission() == nullptr && !run.complete());
    }

    SECTION("precision: latitude and far-side goals count only where they ask, and say why not");
    {
        const auto bodies = world();  // Mars's frame is the default one: its pole is +Z
        Mission m;
        m.id = "test-pole";
        m.start = "Mars";
        MissionGoal pole = landOn("Mars");
        pole.minLatitudeDeg = 80;
        m.goals = {pole};
        MissionRun run;
        CHECK(run.start(m, find));
        descend(run, bodies, 3, {1, 0, 0}, 0.03);  // the equator
        CHECK_MSG(!run.goalDone(0) && run.miss().find("latitude +0") != std::string::npos, run.miss());
        descend(run, bodies, 3, normalize(Vec3{0.1, 0, 1}), 0.03);  // 84 degrees north
        CHECK_MSG(run.complete() && run.miss().empty(), run.miss());

        MissionGoal far = landOn("Moon");
        far.awayFrom = "Earth";
        m.goals = {far};
        CHECK(run.start(m, find));
        descend(run, bodies, 2, {-1, 0, 0}, 0.03);  // under Earth
        CHECK_MSG(!run.goalDone(0) && run.miss().find("0 degrees from the point under Earth") != std::string::npos,
                  run.miss());
        descend(run, bodies, 2, normalize(Vec3{0.4, 1, 0}), 0.03);  // 112 degrees round: not far enough
        CHECK(!run.goalDone(0));
        descend(run, bodies, 2, normalize(Vec3{1, 0.5, 0}), 0.03);  // 153 degrees round
        CHECK_MSG(run.complete(), run.miss());
    }

    SECTION("a flyby counts when the ship crosses the goal's distance between two steps, even at warp");
    {
        const auto bodies = world();
        Mission m;
        m.id = "test-pass";
        m.start = "Earth";
        MissionGoal g;
        g.kind = MissionGoal::Pass;
        g.body = "Probe";
        g.text = "pass the probe";
        g.withinKm = 10000;
        m.goals = {g};
        auto flyby = [&](double missKm) {
            MissionRun run;
            run.start(m, find);
            Ship ship;
            ship.place(bodies[4].centre + Vec3{-5e6, missKm, 0}, {1, 0, 0});
            ship.setTier(kDriveTierCount - 1);
            ship.setThrottle(1);
            ship.speed = ship.topSpeed();  // 600 million km per second: past the probe in one step
            run.update(0, ship, bodies);
            for (int k = 0; k < 2; ++k) {
                ship.step(kDt, {}, bodies);
                run.update(kDt, ship, bodies);
            }
            CHECK(ship.position.x > bodies[4].centre.x + 1e6);
            return run.complete();
        };
        CHECK(flyby(5000));
        CHECK(!flyby(50000));
    }

    SECTION("a race's par can be beaten by hand: on thrusters at the end, where a hard landing counts");
    {
        const auto bodies = world();
        Mission m;
        m.id = "test-race";
        m.start = "Earth";
        m.goals = {landOn("Moon")};
        auto race = [&](bool byHand) {
            MissionRun run;
            run.start(m, find);
            Vec3 pos, fwd;
            missionStartPose(bodies[1], bodies[2], pos, fwd);
            Ship ship;
            ship.place(pos, fwd);
            ship.setTarget(2);
            if (byHand) {
                ship.setTier(kDriveTierCount - 1);
                ship.setThrottle(1);
            } else {
                ship.engageLanding(true);
            }
            run.update(0, ship, bodies);
            for (double t = 0; t < 120 && !run.complete(); t += kDt) {
                // By hand: under mass lock to within a kilometre, then thrusters at 580 m/s.
                if (byHand && ship.nearest() == 2 && ship.altitude() < 1 && ship.tier() > 0) {
                    ship.setTier(0);
                    ship.setThrottle(0.29);
                }
                ship.step(kDt, {}, bodies);
                run.update(kDt, ship, bodies);
            }
            CHECK(run.complete());
            return run.seconds();
        };
        const double autopilot = race(false), hand = race(true);  // 25.2 s and 23.4 s
        // The races' pars are a second under the autopilot's time, so this is the margin they leave.
        CHECK_MSG(hand < autopilot - 1, std::to_string(hand) + " s by hand, " + std::to_string(autopilot) + " s autopilot");
    }

    SECTION("records: best times survive a round trip, bad lines are skipped, and only a faster time replaces one");
    {
        auto r = parseRecords("moon-run 31.25\ngalilean 600\nbroken\nnan-run nan\nneg -3\nextra 4 5\nword abc\n");
        CHECK(r.size() == 2 && r["moon-run"] == 31.25 && r["galilean"] == 600);
        CHECK(parseRecords(formatRecords(r)) == r);
        CHECK(!recordTime(r, "moon-run", 40));
        CHECK(r["moon-run"] == 31.25);
        CHECK(recordTime(r, "moon-run", 30.5) && r["moon-run"] == 30.5);
        CHECK(recordTime(r, "halley", 75) && r.size() == 3);
        CHECK(parseRecords("").empty());
    }

    SECTION("mission clock text");
    {
        CHECK(clockText(0) == "0:00.0");
        CHECK(clockText(31.24) == "0:31.2");
        CHECK(clockText(59.96) == "1:00.0");
        CHECK(clockText(725) == "12:05.0");
        CHECK(clockText(3723.4) == "1:02:03");
    }

    return check::finish();
}
