// orrery: the solar system from JPL data, drawn with the Strata engine.
//
//   orrery [--data DIR] [--date YYYY-MM-DD[THH:MM[:SS]]] [--focus NAME] [--mode true|compressed]
//          [--enlarge K] [--rate DAYS_PER_SECOND] [--width W --height H]
//   orrery --headless [view options] --screenshot out.png
//   orrery --headless [view options] --frames DIR --count N [--step DAYS] [--yaw-step DEG]
//   orrery [view options] --export-usda out.usda
//   orrery --smoke-test DIR
//
// View options: --yaw DEG --pitch DEG --distance-au AU (drawn), --no-labels, --no-orbits,
// --no-small-bodies, --no-hud, --axes. Ship: --ship [--drive 1-5] [--throttle F] [--cockpit]
// [--target NAME [--autopilot | --land]] starts in the ship where the orbit camera would be (with
// --frames it flies 1/30 s per frame); --altitude KM starts it instead that many real km above the
// target's surface under the Sun, flying level; --fuel F starts with that fraction of a full tank. --mission ID starts that mission instead (the ship options
// still apply; --land and --autopilot fly to its first goal); --missions opens the mission list.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "orrery/missions.hpp"
#include "orrery/system.hpp"
#include "orrery/time.hpp"
#include "renderer.hpp"
#include "strata/strata.hpp"
#include "usda.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#ifndef ORRERY_SOURCE_DIR
#define ORRERY_SOURCE_DIR "."
#endif
#if defined(__EMSCRIPTEN__)
#define ORRERY_DEFAULT_DATA "/data"  // the preloaded data file (tools/make_web_data.py)
#else
#define ORRERY_DEFAULT_DATA ORRERY_SOURCE_DIR "/data"
#endif

using namespace orrery;

namespace {

// SDL3 scancodes (USB HID usage ids), as Strata reports them.
enum Key {
    kA = 4, kB = 5, kC = 6, kD = 7, kE = 8, kF = 9, kG = 10, kH = 11, kI = 12, kJ = 13, kL = 15, kM = 16, kN = 17, kO = 18, kP = 19,
    kQ = 20, kR = 21, kS = 22, kT = 23, kU = 24, kV = 25, kW = 26, kX = 27, kZ = 29, k1 = 30, k0 = 39, kReturn = 40,
    kEscape = 41, kBackspace = 42, kTab = 43,
    kSpace = 44, kMinus = 45, kLeftBracket = 47, kRightBracket = 48, kComma = 54, kPeriod = 55, kHome = 74, kPageUp = 75,
    kPageDown = 78, kRight = 79, kLeft = 80, kDown = 81, kUp = 82,
};
constexpr int kShift = 0x0001 | 0x0002;

// Time rates in days per second of wall-clock time; 1/86400 is real time.
const double kRates[] = {-365.25, -30, -7, -1, -1.0 / 24, -1.0 / 1440, -1.0 / 86400, 1.0 / 86400, 1.0 / 1440,
                         1.0 / 24, 1, 7, 30, 365.25};
constexpr int kRealTimeRate = 7;
const double kEnlargements[] = {1, 10, 100, 500, 2000};

struct Args {
    std::string data = ORRERY_DEFAULT_DATA;
    std::string date;  // empty: now
    std::string focus = "Sun";
    ScaleMode mode = ScaleMode::True;
    double enlarge = 500;
    double rate = 1.0 / 86400;
    bool headless = false;
    int width = 1280, height = 720;
    std::string screenshot, framesDir, usda, smokeDir;
    int count = 0;
    double step = 1.0;      // days per frame
    double yawStep = 0.0;   // degrees per frame
    double yaw = -60, pitch = 25, distanceAu = 0;
    bool labels = true, orbits = true, smallBodies = true, hud = true, axes = false;
    bool ship = false, cockpit = false, autopilot = false, land = false;
    std::string target;     // ship: the target, if not --focus
    int drive = 1;          // 1 = thrusters .. 5 = warp 3
    double throttle = 0;
    double altitudeKm = -1;  // ship: start this far above the target's surface (real km); < 0 = no
    double fuel = 1;         // ship: the tank at the start, 0..1
    std::string mission;    // start this mission (an id from missions())
    bool missionsPanel = false;
};

int usage() {
    std::fputs(
        "usage: orrery [--data DIR] [--date YYYY-MM-DD[THH:MM[:SS]]] [--focus NAME] [--mode true|compressed]\n"
        "              [--enlarge K] [--rate DAYS_PER_SECOND] [--width W] [--height H]\n"
        "              [--yaw DEG] [--pitch DEG] [--distance-au AU] [--no-labels] [--no-orbits]\n"
        "              [--no-small-bodies] [--no-hud] [--axes]\n"
        "              [--ship [--drive 1-5] [--throttle F] [--cockpit] [--target NAME [--autopilot | --land]\n"
        "                      [--altitude KM]] [--fuel F]]\n"
        "              [--mission ID] [--missions]\n"
        "       orrery --headless ... --screenshot out.png\n"
        "       orrery --headless ... --frames DIR --count N [--step DAYS] [--yaw-step DEG]\n"
        "       orrery ... --export-usda out.usda\n"
        "       orrery --smoke-test DIR\n",
        stderr);
    return 2;
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(k + " needs a value");
            return argv[++i];
        };
        if (k == "--help" || k == "-h") throw std::invalid_argument("help");
        if (k == "--data") a.data = val();
        else if (k == "--date") a.date = val();
        else if (k == "--focus") a.focus = val();
        else if (k == "--mode") {
            const std::string m = val();
            if (m == "true") a.mode = ScaleMode::True;
            else if (m == "compressed") a.mode = ScaleMode::Compressed;
            else return false;
        } else if (k == "--enlarge") a.enlarge = std::atof(val().c_str());
        else if (k == "--rate") a.rate = std::atof(val().c_str());
        else if (k == "--headless") a.headless = true;
        else if (k == "--width") a.width = std::atoi(val().c_str());
        else if (k == "--height") a.height = std::atoi(val().c_str());
        else if (k == "--screenshot") a.screenshot = val();
        else if (k == "--frames") a.framesDir = val();
        else if (k == "--count") a.count = std::atoi(val().c_str());
        else if (k == "--step") a.step = std::atof(val().c_str());
        else if (k == "--yaw-step") a.yawStep = std::atof(val().c_str());
        else if (k == "--yaw") a.yaw = std::atof(val().c_str());
        else if (k == "--pitch") a.pitch = std::atof(val().c_str());
        else if (k == "--distance-au") a.distanceAu = std::atof(val().c_str());
        else if (k == "--export-usda") a.usda = val();
        else if (k == "--smoke-test") { a.smokeDir = val(); a.headless = true; }
        else if (k == "--no-labels") a.labels = false;
        else if (k == "--no-orbits") a.orbits = false;
        else if (k == "--no-small-bodies") a.smallBodies = false;
        else if (k == "--no-hud") a.hud = false;
        else if (k == "--axes") a.axes = true;
        else if (k == "--ship") a.ship = true;
        else if (k == "--cockpit") a.cockpit = true;
        else if (k == "--target") a.target = val();
        else if (k == "--autopilot") a.autopilot = true;
        else if (k == "--land") a.land = true;
        else if (k == "--altitude") a.altitudeKm = std::atof(val().c_str());
        else if (k == "--fuel") a.fuel = std::atof(val().c_str());
        else if (k == "--drive") a.drive = std::atoi(val().c_str());
        else if (k == "--throttle") a.throttle = std::atof(val().c_str());
        else if (k == "--mission") a.mission = val();
        else if (k == "--missions") a.missionsPanel = true;
        else return false;
    }
    return a.width > 0 && a.height > 0 && a.enlarge >= 1 && a.drive >= 1 && a.drive <= kDriveTierCount;
}

const char* touchdownName(Touchdown::Grade g) {
    switch (g) {
        case Touchdown::Soft: return "soft";
        case Touchdown::Hard: return "hard";
        case Touchdown::Crash: return "crash";
        case Touchdown::Lost: return "lost";
        case Touchdown::Burned: return "burned";
        default: return "none";
    }
}

#if !defined(__EMSCRIPTEN__)
// Where the desktop build keeps the mission records: $ORRERY_RECORDS, else the
// user's data directory. The browser build keeps them in the page's storage.
std::filesystem::path recordsPath() {
    namespace fs = std::filesystem;
    if (const char* p = std::getenv("ORRERY_RECORDS"); p && *p) return p;
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA"); a && *a) return fs::path(a) / "orrery" / "records.txt";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return fs::path(x) / "orrery" / "records.txt";
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".local" / "share" / "orrery" / "records.txt";
#endif
    return {};
}
#endif

#if defined(__EMSCRIPTEN__)
// A JSON string literal (the mission list's text has no control characters).
std::string jsonText(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
#endif

std::string rateText(double rate, bool paused) {
    if (paused) return "paused";
    const double a = std::fabs(rate);
    char buf[64];
    const char* sign = rate < 0 ? "-" : "";
    if (std::fabs(a - 1.0 / 86400) < 1e-12) std::snprintf(buf, sizeof buf, "%sreal time", sign);
    else if (a < 1.0 / 24 - 1e-12) std::snprintf(buf, sizeof buf, "%s%g min/s", sign, a * 1440);
    else if (a < 1 - 1e-9) std::snprintf(buf, sizeof buf, "%s%g h/s", sign, a * 24);
    else if (a < 365) std::snprintf(buf, sizeof buf, "%s%g d/s", sign, a);
    else std::snprintf(buf, sizeof buf, "%s%g yr/s", sign, a / 365.25);
    return buf;
}

std::string timeLine(double jdTdb, const std::string& rate) {
    return formatIsoDate(calendarDate(tdbToUtc(jdTdb))) + " UTC   " + rate;
}

int nearestIndex(const double* values, int count, double v) {
    int best = 0;
    for (int i = 1; i < count; ++i) {
        if (std::fabs(values[i] - v) < std::fabs(values[best] - v)) best = i;
    }
    return best;
}

// The "next" bodies Tab cycles through: everything with a name, in catalogue order.
int cycle(const SolarSystem& sys, const SystemState& state, int from, int dir) {
    const int n = static_cast<int>(sys.bodies().size());
    int i = from;
    for (int k = 0; k < n; ++k) {
        i = (i + dir + n) % n;
        if (state.valid[static_cast<size_t>(i)]) return i;
    }
    return from;
}

struct Smoke {
    const char* name;
    const char* date;
    const char* focus;
    ScaleMode mode;
    double enlarge;
    bool ship;
    double distanceAu;  // 0: the default
};

// Fraction of pixels brighter than the background, and whether a pixel near
// the centre is lit.
double litFraction(const std::vector<uint8_t>& rgba) {
    size_t lit = 0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] + rgba[i + 1] + rgba[i + 2] > 60) ++lit;
    }
    return static_cast<double>(lit) / static_cast<double>(rgba.size() / 4);
}

int rateIndexFor(double rate) { return nearestIndex(kRates, static_cast<int>(sizeof kRates / sizeof kRates[0]), rate); }

// Everything one view of the solar system needs, on the heap so that the
// browser build's session outlives main().
struct World {
    Args args;
    std::unique_ptr<SolarSystem> sys;
    double jd = 0;
    SystemState state;
    std::unique_ptr<strata::Engine> engine;
    std::unique_ptr<Renderer> renderer;
    CameraState cam;
    ViewOptions view;
    int width = 0, height = 0;

    // ---- the ship (V): it starts where the camera is, looking where it looks
    Ship ship;
    FlightControls controls;
    double shipDt = 0;          // seconds the next frame flies
    bool boardPending = false;  // board on the next frame, once bodies are laid out
    Vec3 chaseLook, chaseUp;    // the chase camera trails the ship's turns

    // ---- missions (I): timed goals for the ship, with the best time kept per device
    MissionRun mission;
    std::map<std::string, double> records;
    std::filesystem::path recordsFile;  // desktop: where the records are saved; empty = not saved
    int recordsVersion = 0;             // bumped with each new best (the page saves the records then)
    bool newBest = false;               // the finished mission set a record
    bool panel = false;                 // the mission list is open
    int missionPending = -1;            // --mission: start it on the first frame
    std::string missionNote;            // why the last mission was abandoned

    void board() { boardAt(renderer->eyeOf(cam), renderer->forwardOf(cam)); }

    // Boards the ship `altitudeKm` real km above a body's surface at the point
    // under the Sun, nose level and along the body's orbit-ish horizon.
    void boardAbove(int body, double altitudeKm) {
        const double r = renderer->drawnRadius(body), real = sys->bodies()[static_cast<size_t>(body)].radiusKm;
        if (body <= 0 || r <= 0 || real <= 0) return;
        const Vec3 centre = renderer->drawn()[static_cast<size_t>(body)];
        const Vec3 up = normalize(renderer->drawn()[0] - centre);
        const Vec3 level = normalize(cross(Vec3{0, 0, 1}, up));
        boardAt(centre + up * (r + altitudeKm * r / real), level);
        ship.up = up;  // wings level: the body's surface below, the Sun overhead
        chaseUp = up;
    }

    void boardAt(const Vec3& position, const Vec3& forward) {
        ship.place(position, forward);
        chaseLook = ship.forward;
        chaseUp = ship.up;
        cam.freeFly = false;
        cam.ship = true;
        view.ship = &ship;
    }

    // Starts mission `index` at its date, in TRUE scale with bodies at their
    // real size (so drawn km are real km and times compare), in the ship at the
    // standoff from its start body, aimed at its first goal.
    bool startMission(int index) {
        if (index < 0 || index >= static_cast<int>(missions().size())) return false;
        const Mission& m = missions()[static_cast<size_t>(index)];
        CalendarDate c;
        if (!parseIsoDate(m.date, c)) return false;
        if (!mission.start(m, [this](const std::string& name) { return sys->find(name); })) return false;
        jd = std::clamp(utcToTdb(julianDate(c)), sys->firstJd(), sys->lastJd());
        view.scale.mode = ScaleMode::True;
        view.scale.enlarge = 1;
        sys->evaluate(jd, state, view.smallBodies);
        renderer->layout(state, view.scale);
        const std::vector<FlightBody> bodies = renderer->flightBodies(state);
        const int first = mission.goalBody(0);
        Vec3 position, forward;
        missionStartPose(bodies[static_cast<size_t>(sys->find(m.start))], bodies[static_cast<size_t>(first)], position,
                         forward);
        boardAt(position, forward);
        ship.setTier(0);
        cam.focus = first;
        ship.setTarget(first);
        ship.step(0, {}, bodies);
        mission.update(0, ship, bodies);
        newBest = false;
        panel = false;
        missionNote.clear();
        return true;
    }

    // An empty tank's tow: back to the body last landed on, refuelled. A tow
    // ends a mission (it would be a free trip).
    void tow() {
        if (ship.fuel() > 0) return;
        abandonMission("towed");
        if (ship.tow(renderer->flightBodies(state)) < 0) return;
        chaseLook = ship.forward;
        chaseUp = ship.up;
    }

    void abandonMission(const std::string& why) {
        if (mission.running() && !why.empty()) missionNote = "mission abandoned: " + why;
        mission.abandon();
    }

    void finishMission() {
        const Mission& m = *mission.mission();
        newBest = recordTime(records, m.id, mission.seconds());
        std::printf("mission %s complete in %s (%.2f s)%s\n", m.id.c_str(), clockText(mission.seconds()).c_str(),
                    mission.seconds(), newBest ? ", a new best" : "");
        if (!newBest) return;
        ++recordsVersion;
        if (recordsFile.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(recordsFile.parent_path(), ec);
        std::ofstream out(recordsFile, std::ios::binary | std::ios::trunc);
        out << formatRecords(records);
        if (!out) std::fprintf(stderr, "could not save the mission records to %s\n", recordsFile.string().c_str());
    }

    void loadRecords() {
        std::ifstream in(recordsFile, std::ios::binary);
        if (!in) return;  // none yet
        std::ostringstream text;
        text << in.rdbuf();
        records = parseRecords(text.str());
    }

    // The HUD's mission line, the goal under it, and the mission list.
    void missionView() {
        view.missionLine.clear();
        view.missionGoal.clear();
        view.missionTone = HudTone::Plain;
        view.panel.clear();
        const Mission* m = mission.mission();
        auto best = [this](const std::string& id) {
            const auto it = records.find(id);
            return it == records.end() ? std::string() : "best " + clockText(it->second);
        };
        if (m && mission.complete()) {
            view.missionLine = "COMPLETE  " + m->title + "  " + clockText(mission.seconds());
            if (m->parSeconds > 0) {
                view.missionLine += mission.seconds() <= m->parSeconds ? "  UNDER PAR" : "  par " + clockText(m->parSeconds);
            }
            view.missionLine += newBest ? "  NEW BEST" : "  " + best(m->id);
            view.missionGoal = view.keyHints ? "I for the mission list" : "Missions in the toolbar for the list";
            view.missionTone = HudTone::Go;
        } else if (m) {
            view.missionLine = "MISSION  " + m->title + "  " + std::to_string(mission.goalsDone()) + "/" +
                               std::to_string(mission.goalCount()) + "  " + clockText(mission.seconds());
            if (m->parSeconds > 0) view.missionLine += "   par " + clockText(m->parSeconds);
            const int next = mission.nextGoal();
            if (!mission.miss().empty()) {
                view.missionGoal = mission.miss();
                view.missionTone = HudTone::Warn;
            } else if (next >= 0) {
                view.missionGoal = "next: " + m->goals[static_cast<size_t>(next)].text;
                if (!best(m->id).empty()) view.missionGoal += "   " + best(m->id);
            }
        } else if (!missionNote.empty()) {
            view.missionGoal = missionNote;
            view.missionTone = HudTone::Warn;
        }
        if (!panel) return;
        view.panel.push_back({"MISSIONS   1-9, 0, - start one   Backspace abandons   I closes", false, HudTone::Go});
        const auto& list = missions();
        for (size_t i = 0; i < list.size(); ++i) {
            char buf[160];
            const std::string key = i < 10 ? std::to_string((i + 1) % 10) : "-";
            std::snprintf(buf, sizeof buf, "%s  %-12s %-20s %s", key.c_str(), list[i].kind.c_str(),
                          list[i].title.c_str(), best(list[i].id).c_str());
            view.panel.push_back({buf, false, HudTone::Plain});
            view.panel.push_back({"   " + list[i].brief, true, HudTone::Dim});
        }
    }

    void disembark() {
        // Back to orbiting the target from where the camera is.
        const Vec3 rel = cam.eye - renderer->drawn()[static_cast<size_t>(cam.focus)];
        const Vec3 dir = normalize(rel);
        cam.yawDeg = std::atan2(dir.y, dir.x) / kDegToRad;
        cam.pitchDeg = std::clamp(std::asin(std::clamp(dir.z, -1.0, 1.0)) / kDegToRad, -89.0, 89.0);
        cam.distance = std::max(length(rel), 1.0);
        cam.ship = false;
        cam.nearLimit = 0;
        view.ship = nullptr;
    }

    void shipCamera(const std::vector<FlightBody>& bodies) {
        const double L = ship.size();
        if (view.cockpit) {
            cam.eye = ship.position + ship.forward * (0.3 * L) + ship.up * (0.1 * L);
            cam.look = ship.forward;
            cam.up = ship.up;
            cam.nearLimit = 0;
        } else {
            const double a = shipDt > 0 ? 1 - std::exp(-shipDt / 0.15) : 1.0;
            chaseLook = normalize(chaseLook + (ship.forward - chaseLook) * a);
            chaseUp = normalize(chaseUp + (ship.up - chaseUp) * a);
            cam.eye = ship.position - chaseLook * (3.2 * L) + chaseUp * (0.9 * L);
            cam.look = normalize(ship.position + chaseLook * (4.0 * L) - cam.eye);
            cam.up = normalize(chaseUp - cam.look * dot(chaseUp, cam.look));
            cam.nearLimit = 0.5 * L;
        }
        for (const FlightBody& b : bodies) {  // never inside a surface
            if (!b.present || b.radius <= 0) continue;
            const Vec3 rel = cam.eye - b.centre;
            if (length(rel) < b.radius + 0.3 * L) cam.eye = b.centre + normalize(rel) * (b.radius + 0.3 * L);
        }
    }

    FrameReport frame(const std::string& rate) {
        sys->evaluate(jd, state, view.smallBodies);
        renderer->layout(state, view.scale);
        if (cam.distance <= 0) {
            cam.distance = args.distanceAu > 0 ? args.distanceAu * kAuKm : renderer->defaultDistance(cam.focus);
        }
        const bool started = missionPending >= 0 && startMission(missionPending);
        missionPending = -1;
        if (boardPending || started) {
            if (boardPending) board();
            boardPending = false;
            if (!args.target.empty() && !started) cam.focus = sys->find(args.target);
            if (args.altitudeKm >= 0 && !started) boardAbove(cam.focus, args.altitudeKm);  // before the drive is set
            if (!started) ship.setFuel(args.fuel);
            ship.setTier(args.drive - 1);
            ship.setThrottle(args.throttle);
            ship.speed = args.throttle * ship.topSpeed();  // already up to speed; mass lock still applies
            ship.setTarget(cam.focus);
            ship.engageAutopilot(args.autopilot);
            if (args.land) ship.engageLanding(true);
        }
        if (cam.ship) {
            const std::vector<FlightBody> bodies = renderer->flightBodies(state);
            ship.setTarget(cam.focus);
            ship.step(shipDt, controls, bodies);
            if (mission.running()) {
                mission.update(shipDt, ship, bodies);
                if (mission.complete()) {
                    finishMission();
                } else if (mission.nextGoal() >= 0) {
                    // A goal just done: the target moves on to the next one.
                    for (int g = 0; g < mission.goalCount(); ++g) {
                        if (mission.goalDone(g) && mission.goalBody(g) == cam.focus) {
                            cam.focus = mission.goalBody(mission.nextGoal());
                            ship.setTarget(cam.focus);
                            break;
                        }
                    }
                }
            }
            shipCamera(bodies);
        }
        missionView();
        view.timeLine = timeLine(jd, rate);
        return renderer->draw(state, cam, view, width, height);
    }
};

// The interactive viewer: keyboard and mouse (SDL events through Strata), one
// step() per frame. The browser build also drives it from the page (touch,
// the toolbar) through the orrery_* exports below.
struct Interactive {
    std::unique_ptr<World> w;
    int rateIndex;
    bool paused = false, running = true, dragging = false, entering = false;
    std::string entry;
    bool keys[512] = {};
    int shot = 0;
    long frames = 0;
    double stickX = 0, stickY = 0;  // the page's thumb stick, -1..1 (right, up)
    FrameReport report;
    std::string status;  // orrery_status(): JSON for the page

    Interactive(std::unique_ptr<World> world, int rate) : w(std::move(world)), rateIndex(rate) {}

    double clampJd(double jd) const { return std::clamp(jd, w->sys->firstJd(), w->sys->lastJd()); }

    void focus(int body) {
        if (body < 0 || body >= static_cast<int>(w->sys->bodies().size())) return;
        w->cam.focus = body;
        if (w->cam.ship) return;  // in the ship the focus is the target; the camera stays
        w->cam.freeFly = false;
        w->cam.distance = w->renderer->defaultDistance(body);
    }

    void orbit(double dx, double dy) {
        CameraState& cam = w->cam;
        if (cam.ship) {  // in the ship a drag steers
            w->controls.yaw += dx * 0.003;
            w->controls.pitch -= dy * 0.003;
            return;
        }
        const double sign = cam.freeFly ? -1 : 1;
        cam.yawDeg -= dx * 0.3 * sign;
        cam.pitchDeg = std::clamp(cam.pitchDeg + dy * 0.3 * sign, -89.0, 89.0);
    }

    void zoom(double factor) {
        if (w->cam.ship) return;  // the ship's camera follows the ship
        if (factor > 0 && std::isfinite(factor)) w->cam.distance *= factor;
    }

    // Starts a mission from the list (keys, or the page), at real time.
    bool startMission(int index) {
        if (!w->startMission(index)) return false;
        rateIndex = kRealTimeRate;
        paused = false;
        return true;
    }

    // Steps the ship's drive up or down a tier (wheel, PgUp / PgDn).
    void drive(int by) {
        w->ship.engageAutopilot(false);
        w->ship.setTier(w->ship.tier() + by);
    }

    void key(int scancode, bool shift) {
        const SolarSystem& sys = *w->sys;
        CameraState& cam = w->cam;
        ViewOptions& view = w->view;
        Renderer& renderer = *w->renderer;
        double& jd = w->jd;
        if (entering) {
            if (scancode == kReturn) {
                CalendarDate c;
                if (parseIsoDate(entry, c)) {
                    jd = clampJd(utcToTdb(julianDate(c)));
                    w->abandonMission("the date changed");
                }
                entering = false;
            } else if (scancode == kEscape) {
                entering = false;
            } else if (scancode == kBackspace && !entry.empty()) {
                entry.pop_back();
            }
            return;
        }
        if (w->panel) {  // the mission list takes the keys while it is open
            if (scancode >= k1 && scancode <= k0) {
                startMission(scancode == k0 ? 9 : scancode - k1);
            } else if (scancode == kMinus) {
                startMission(10);
            } else if (scancode == kBackspace) {
                w->abandonMission("");
            } else if (scancode == kI || scancode == kEscape) {
                w->panel = false;
            }
            return;
        }
        if (cam.ship) {  // ship keys; the rest fall through to the usual ones
            switch (scancode) {
                case kV: w->disembark(); return;
                case kT: w->ship.engageAutopilot(!w->ship.autopilot() || w->ship.landing()); return;
                case kG: w->ship.engageLanding(!w->ship.landing()); return;
                case kZ: w->ship.allStop(); return;
                case kR: w->tow(); return;
                case kC: view.cockpit = !view.cockpit; return;
                case kPageUp: drive(1); return;
                case kPageDown: drive(-1); return;
                case kF: return;
                default: break;
            }
        }
        switch (scancode) {
            case kV: w->board(); break;
            case kEscape:
#if !defined(__EMSCRIPTEN__)
                running = false;  // a page is closed by the browser, not by Esc
#endif
                break;
            case kSpace: paused = !paused; break;
            case kComma: rateIndex = std::max(rateIndex - 1, 0); break;
            case kPeriod: rateIndex = std::min(rateIndex + 1, static_cast<int>(sizeof kRates / sizeof kRates[0]) - 1); break;
            case kI:
                w->panel = true;
                w->missionNote.clear();
                break;
            case kN:
                w->abandonMission("the date changed");
                jd = clampJd(utcToTdb(nowUtc()));
                rateIndex = kRealTimeRate;
                paused = false;
                break;
            case kJ: entering = true; entry.clear(); break;
            case kTab: focus(cycle(sys, w->state, cam.focus, shift ? -1 : 1)); break;
            case kHome: focus(0); break;
            case kM:
                w->abandonMission("the scale changed");
                view.scale.mode = view.scale.mode == ScaleMode::True ? ScaleMode::Compressed : ScaleMode::True;
                renderer.layout(w->state, view.scale);
                cam.distance = renderer.defaultDistance(cam.focus);
                break;
            case kLeftBracket:
            case kRightBracket: {
                w->abandonMission("the bodies' size changed");
                int e = nearestIndex(kEnlargements, 5, view.scale.enlarge);
                e = std::clamp(e + (scancode == kRightBracket ? 1 : -1), 0, 4);
                view.scale.enlarge = kEnlargements[e];
                break;
            }
            case kO: view.orbits = !view.orbits; break;
            case kL: view.labels = !view.labels; break;
            case kB: view.smallBodies = !view.smallBodies; break;
            case kX: view.axes = !view.axes; break;
            case kH: view.help = !view.help; break;
            case kF:
                if (!cam.freeFly) {
                    cam.eye = renderer.eyeOf(cam);
                    const Vec3 fwd = renderer.forwardOf(cam);
                    cam.yawDeg = std::atan2(fwd.y, fwd.x) / kDegToRad;
                    cam.pitchDeg = std::asin(std::clamp(fwd.z, -1.0, 1.0)) / kDegToRad;
                    cam.freeFly = true;
                } else {
                    cam.freeFly = false;
                    cam.yawDeg += 180;
                    cam.pitchDeg = -cam.pitchDeg;
                    cam.distance = length(cam.eye - renderer.drawn()[static_cast<size_t>(cam.focus)]);
                }
                break;
            case kPageUp: cam.distance /= 1.5; break;
            case kPageDown: cam.distance *= 1.5; break;
#if !defined(__EMSCRIPTEN__)
            case kP: {
                char name[64];
                std::snprintf(name, sizeof name, "orrery-%03d.png", shot++);
                w->engine->saveScreenshotPng(name);
                std::printf("wrote %s\n", name);
                break;
            }
            case kU: writeUsda("orrery.usda", sys, w->state); std::puts("wrote orrery.usda"); break;
#endif
            default:
                if (scancode >= k1 && scancode <= k0) {
                    focus(scancode == k0 ? 0 : scancode - k1 + 1);  // bodies.csv: Sun, Mercury..Neptune, Pluto
                }
        }
    }

    void event(const StrataEvent& ev) {
        if (ev.type == STRATA_EVENT_QUIT) {
            running = false;
        } else if (ev.type == STRATA_EVENT_RESIZED) {
            w->width = std::max(ev.width, 1);
            w->height = std::max(ev.height, 1);
        } else if (ev.type == STRATA_EVENT_TEXT_INPUT && entering) {
            for (const char* p = ev.text; *p; ++p) {
                if (std::strchr("0123456789-:T ", *p) && entry.size() < 19) entry += *p;
            }
        } else if (ev.type == STRATA_EVENT_KEY_UP && ev.scancode >= 0 && ev.scancode < 512) {
            keys[ev.scancode] = false;
        } else if (ev.type == STRATA_EVENT_KEY_DOWN) {
            if (ev.scancode >= 0 && ev.scancode < 512) keys[ev.scancode] = true;
            key(ev.scancode, (ev.modifiers & kShift) != 0);
        }
#if !defined(__EMSCRIPTEN__)
        // In a browser the page handles pointers (mouse, touch, pinch) and
        // calls orrery_orbit/orrery_zoom, so SDL's mouse events are not used.
        else if (ev.type == STRATA_EVENT_MOUSE_BUTTON_DOWN) {
            dragging = true;
        } else if (ev.type == STRATA_EVENT_MOUSE_BUTTON_UP) {
            dragging = false;
        } else if (ev.type == STRATA_EVENT_MOUSE_MOTION && dragging) {
            orbit(ev.dx, ev.dy);
        } else if (ev.type == STRATA_EVENT_MOUSE_WHEEL && w->cam.ship) {
            if (ev.y != 0) drive(ev.y > 0 ? 1 : -1);
        } else if (ev.type == STRATA_EVENT_MOUSE_WHEEL) {
            zoom(std::pow(0.85, ev.y));
        }
#endif
    }

    void step() {
        StrataEvent ev;
        while (w->engine->pollEvent(ev)) event(ev);
#if defined(__EMSCRIPTEN__)
        // The canvas follows the page; draw at its size in device pixels.
        int32_t dw = 0, dh = 0;
        w->engine->drawableSize(dw, dh);
        if (dw > 0 && dh > 0) {
            w->width = dw;
            w->height = dh;
        }
#endif
        // The engine's frame clock (measured, or a test harness's fixed step), capped.
        const double dt = std::min(w->engine->frameSeconds(), 0.1);
        const double rate = kRates[rateIndex];
        CameraState& cam = w->cam;
        Renderer& renderer = *w->renderer;
        if (!paused) w->jd = clampJd(w->jd + rate * dt);
        if (cam.ship && !entering) {
            FlightControls& c = w->controls;
            const double turn = 1.0 * dt, roll = 1.6 * dt;
            c.throttleRate = (keys[kW] ? 0.6 : 0.0) - (keys[kS] ? 0.6 : 0.0);
            c.pitch += (keys[kUp] ? turn : 0.0) - (keys[kDown] ? turn : 0.0);
            c.yaw += (keys[kRight] || keys[kD] ? turn : 0.0) - (keys[kLeft] || keys[kA] ? turn : 0.0);
            c.roll += (keys[kE] ? roll : 0.0) - (keys[kQ] ? roll : 0.0);
            // The stick turns at up to the keys' rate, gently near its centre.
            c.yaw += stickX * std::fabs(stickX) * turn;
            c.pitch += stickY * std::fabs(stickY) * turn;
        }
        w->shipDt = dt;
        if (cam.freeFly) {
            // Speed scales with the distance to the nearest body's surface.
            double nearest = 1e300;
            for (size_t i = 0; i < renderer.drawn().size(); ++i) {
                nearest = std::min(nearest, length(renderer.drawn()[i] - cam.eye) - renderer.drawnRadius(static_cast<int>(i)));
            }
            const double speed = std::max(nearest, 100.0) * 0.8 * dt;
            const Vec3 fwd = renderer.forwardOf(cam);
            const Vec3 right = normalize(cross(fwd, Vec3{0, 0, 1}));
            const Vec3 up = cross(right, fwd);
            if (keys[kW]) cam.eye += fwd * speed;
            if (keys[kS]) cam.eye -= fwd * speed;
            if (keys[kD]) cam.eye += right * speed;
            if (keys[kA]) cam.eye -= right * speed;
            if (keys[kE]) cam.eye += up * speed;
            if (keys[kQ]) cam.eye -= up * speed;
        }
        w->view.prompt = entering ? "Jump to date (UTC, YYYY-MM-DD[THH:MM]): " + entry + "_" : "";
        report = w->frame(rateText(rate, paused));
        w->controls = FlightControls{};
        w->engine->present();
        ++frames;
    }

    const char* statusJson() {
        const Body& b = w->sys->bodies()[static_cast<size_t>(w->cam.focus)];
        const Ship& ship = w->ship;
        const MissionRun& run = w->mission;
        char buf[2048];
        std::snprintf(buf, sizeof buf,
                      "{\"frames\":%ld,\"focus\":%d,\"focusName\":\"%s\",\"freeFly\":%s,\"paused\":%s,"
                      "\"rate\":\"%s\",\"date\":\"%s\",\"mode\":\"%s\",\"width\":%d,\"height\":%d,"
                      "\"spheres\":%d,\"points\":%d,\"lineVertices\":%d,\"labelCount\":%d,"
                      "\"labels\":%s,\"orbits\":%s,\"help\":%s,\"firstDate\":\"%s\",\"lastDate\":\"%s\","
                      "\"ship\":%s,\"drive\":\"%s\",\"throttle\":%.3f,\"speed\":%.6g,\"autopilot\":%s,"
                      "\"cockpit\":%s,\"shipTriangles\":%d,\"stickX\":%.3f,\"stickY\":%.3f,"
                      "\"landing\":%s,\"landed\":%d,\"touchdown\":\"%s\",\"impact\":%.6g,\"heat\":%.3f,\"air\":%d,\"fuel\":%.4f,"
                      "\"missionList\":%s,\"mission\":\"%s\",\"missionDone\":%d,\"missionGoals\":%d,"
                      "\"missionSeconds\":%.3f,\"missionComplete\":%s,\"newBest\":%s,\"recordsVersion\":%d}",
                      frames, w->cam.focus, b.name.c_str(), w->cam.freeFly ? "true" : "false",
                      paused ? "true" : "false", rateText(kRates[rateIndex], paused).c_str(),
                      formatIsoDate(calendarDate(tdbToUtc(w->jd))).c_str(),
                      w->view.scale.mode == ScaleMode::True ? "true" : "compressed", w->width, w->height,
                      report.spheres, report.points, report.lineVertices, report.labels,
                      w->view.labels ? "true" : "false", w->view.orbits ? "true" : "false",
                      w->view.help ? "true" : "false",
                      formatIsoDate(calendarDate(tdbToUtc(w->sys->firstJd()))).c_str(),
                      formatIsoDate(calendarDate(tdbToUtc(w->sys->lastJd()))).c_str(), w->cam.ship ? "true" : "false",
                      kDriveTiers[ship.tier()].name, ship.throttle(), ship.speed, ship.autopilot() ? "true" : "false",
                      w->view.cockpit ? "true" : "false", report.shipTriangles, stickX, stickY,
                      ship.landing() ? "true" : "false", ship.landed(), touchdownName(ship.touchdown().grade),
                      ship.touchdown().impactKmPerS, ship.heat(), ship.air(), ship.fuel(), w->panel ? "true" : "false",
                      run.mission() ? run.mission()->id.c_str() : "", run.mission() ? run.goalsDone() : 0,
                      run.mission() ? run.goalCount() : 0, run.seconds(), run.complete() ? "true" : "false",
                      w->newBest ? "true" : "false", w->recordsVersion);
        status = buf;
        return status.c_str();
    }
};

#if defined(__EMSCRIPTEN__)
Interactive* gLive = nullptr;
#endif

}  // namespace

#if defined(__EMSCRIPTEN__)
// Called by the page (web/shell.html): pointer and touch input, the toolbar,
// and the smoke test's status read. All are no-ops until the session exists.
extern "C" {
EMSCRIPTEN_KEEPALIVE void orrery_resize(int cssWidth, int cssHeight) {
    if (gLive && cssWidth > 0 && cssHeight > 0) gLive->w->engine->resize(cssWidth, cssHeight);
}
// Text scale: the page passes about devicePixelRatio, so the HUD and labels keep
// their size on a phone's dense screen.
EMSCRIPTEN_KEEPALIVE void orrery_ui_scale(double scale) {
    if (gLive && std::isfinite(scale)) gLive->w->view.uiScale = static_cast<float>(std::clamp(scale, 1.0, 4.0));
}
// The HUD's keyboard hint line; the page turns it off on touch screens.
EMSCRIPTEN_KEEPALIVE void orrery_key_hints(int on) {
    if (gLive) gLive->w->view.keyHints = on != 0;
}
EMSCRIPTEN_KEEPALIVE void orrery_orbit(double dx, double dy) {
    if (gLive) gLive->orbit(dx, dy);
}
EMSCRIPTEN_KEEPALIVE void orrery_zoom(double factor) {
    if (gLive) gLive->zoom(factor);
}
EMSCRIPTEN_KEEPALIVE void orrery_key(int scancode, int shift) {
    if (gLive) gLive->key(scancode, shift != 0);
}
// Holds a key down or lets it go, as a held keyboard key: the ship's touch pad
// (throttle and roll) uses it.
EMSCRIPTEN_KEEPALIVE void orrery_hold_key(int scancode, int down) {
    if (gLive && scancode >= 0 && scancode < 512) gLive->keys[scancode] = down != 0;
}
// The ship's thumb stick, -1..1 each way (x right, y up): it steers for as
// long as it is held off centre; the page sends 0, 0 when it is let go.
EMSCRIPTEN_KEEPALIVE void orrery_stick(double x, double y) {
    if (!gLive || !std::isfinite(x) || !std::isfinite(y)) return;
    gLive->stickX = std::clamp(x, -1.0, 1.0);
    gLive->stickY = std::clamp(y, -1.0, 1.0);
}
// The ship's throttle lever: sets the throttle (-0.25..1) and takes control
// back from the autopilot.
EMSCRIPTEN_KEEPALIVE void orrery_throttle(double throttle) {
    if (!gLive || !std::isfinite(throttle)) return;
    gLive->w->ship.engageAutopilot(false);
    gLive->w->ship.setThrottle(throttle);
}
EMSCRIPTEN_KEEPALIVE void orrery_focus(int body) {
    if (gLive) gLive->focus(body);
}
EMSCRIPTEN_KEEPALIVE int orrery_set_date(const char* iso) {
    CalendarDate c;
    if (!gLive || !iso || !parseIsoDate(iso, c)) return 0;
    gLive->w->jd = gLive->clampJd(utcToTdb(julianDate(c)));
    gLive->w->abandonMission("the date changed");
    return 1;
}
// Missions: the list for the page's mission sheet, starting and abandoning
// one, and the best times, which the page keeps in localStorage.
EMSCRIPTEN_KEEPALIVE int orrery_mission_count() { return static_cast<int>(missions().size()); }
EMSCRIPTEN_KEEPALIVE const char* orrery_mission_json(int index) {
    static std::string json;
    if (!gLive || index < 0 || index >= orrery_mission_count()) return "{}";
    const Mission& m = missions()[static_cast<size_t>(index)];
    const auto it = gLive->w->records.find(m.id);
    char buf[64];
    std::snprintf(buf, sizeof buf, ",\"par\":%.3f,\"best\":%.3f}", m.parSeconds,
                  it == gLive->w->records.end() ? 0.0 : it->second);
    json = "{\"id\":" + jsonText(m.id) + ",\"kind\":" + jsonText(m.kind) + ",\"title\":" + jsonText(m.title) +
           ",\"brief\":" + jsonText(m.brief) + buf;
    return json.c_str();
}
EMSCRIPTEN_KEEPALIVE int orrery_start_mission(int index) { return gLive && gLive->startMission(index) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void orrery_abandon_mission() {
    if (gLive) gLive->w->abandonMission("");
}
EMSCRIPTEN_KEEPALIVE const char* orrery_records() {
    static std::string text;
    text = gLive ? formatRecords(gLive->w->records) : "";
    return text.c_str();
}
EMSCRIPTEN_KEEPALIVE void orrery_load_records(const char* text) {
    if (gLive && text) gLive->w->records = parseRecords(text);
}
EMSCRIPTEN_KEEPALIVE int orrery_body_count() { return gLive ? static_cast<int>(gLive->w->sys->bodies().size()) : 0; }
EMSCRIPTEN_KEEPALIVE const char* orrery_body_name(int body) {
    if (!gLive || body < 0 || body >= orrery_body_count()) return "";
    return gLive->w->sys->bodies()[static_cast<size_t>(body)].name.c_str();
}
EMSCRIPTEN_KEEPALIVE const char* orrery_body_kind(int body) {
    if (!gLive || body < 0 || body >= orrery_body_count()) return "";
    return kindName(gLive->w->sys->bodies()[static_cast<size_t>(body)].kind);
}
EMSCRIPTEN_KEEPALIVE const char* orrery_status() { return gLive ? gLive->statusJson() : "{}"; }
// Stops and restarts the frame loop (the smoke test holds a frame still for its screenshots).
EMSCRIPTEN_KEEPALIVE void orrery_hold(int hold) {
    if (hold) emscripten_pause_main_loop();
    else emscripten_resume_main_loop();
}
}
#endif

int main(int argc, char** argv) {
    auto world = std::make_unique<World>();
    Args& args = world->args;
    try {
        if (!parse(argc, argv, args)) return usage();
    } catch (const std::invalid_argument&) {
        usage();
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return usage();
    }

    try {
        world->sys = std::make_unique<SolarSystem>(args.data);
        const SolarSystem& sys = *world->sys;
        std::printf("orrery: %zu bodies, %zu small bodies; DE440s covers JD %.1f..%.1f\n", sys.bodies().size(),
                    sys.smallBodies().size(), sys.firstJd(), sys.lastJd());

        double& jd = world->jd;
        jd = utcToTdb(nowUtc());
        if (!args.date.empty()) {
            CalendarDate c;
            if (!parseIsoDate(args.date, c)) {
                std::fprintf(stderr, "bad --date '%s' (YYYY-MM-DD[THH:MM[:SS]], UTC)\n", args.date.c_str());
                return 2;
            }
            jd = utcToTdb(julianDate(c));
        }
        jd = std::clamp(jd, sys.firstJd(), sys.lastJd());

        SystemState& state = world->state;
        sys.evaluate(jd, state);

        if (!args.usda.empty()) {
            writeUsda(args.usda, sys, state);
            std::printf("wrote %s\n", args.usda.c_str());
            if (args.screenshot.empty() && args.framesDir.empty() && !args.headless) return 0;
        }

        strata::EngineDesc desc;
        desc.surface = args.headless ? STRATA_SURFACE_HEADLESS : STRATA_SURFACE_WINDOW;
        desc.width = args.width;
        desc.height = args.height;
        desc.title = "orrery";
        desc.flags = args.headless ? 0u : static_cast<uint32_t>(STRATA_FLAG_VSYNC);
        world->engine = strata::Engine::create(desc);
        auto& engine = world->engine;
        std::printf("GPU: %s\n", engine->gpuDescription().c_str());

        world->renderer = std::make_unique<Renderer>(*engine, sys, args.data + "/textures");
        CameraState& cam = world->cam;
        ViewOptions& view = world->view;
        view.scale.mode = args.mode;
        view.scale.enlarge = args.enlarge;
        view.labels = args.labels;
        view.orbits = args.orbits;
        view.smallBodies = args.smallBodies;
        view.hud = args.hud;
        view.axes = args.axes;
        cam.focus = sys.find(args.focus);
        if (cam.focus < 0) {
            std::fprintf(stderr, "no body named '%s'\n", args.focus.c_str());
            return 2;
        }
        if (!args.target.empty() && sys.find(args.target) < 0) {
            std::fprintf(stderr, "no body named '%s'\n", args.target.c_str());
            return 2;
        }
        cam.yawDeg = args.yaw;
        cam.pitchDeg = args.pitch;

        world->width = args.width;
        world->height = args.height;
        world->boardPending = args.ship && args.mission.empty();
        world->panel = args.missionsPanel;
        if (!args.mission.empty()) {
            world->missionPending = findMission(args.mission);
            if (world->missionPending < 0) {
                std::fprintf(stderr, "no mission '%s'; the missions are:", args.mission.c_str());
                for (const Mission& m : missions()) std::fprintf(stderr, " %s", m.id.c_str());
                std::fputc('\n', stderr);
                return 2;
            }
        }
        view.cockpit = args.cockpit;
        auto frame = [&](const std::string& rate) { return world->frame(rate); };

        // ---- smoke test: four views (the last from the ship), each must be visibly drawn
        if (!args.smokeDir.empty()) {
            std::filesystem::create_directories(args.smokeDir);
            const Smoke views[] = {
                {"overview_true", "2026-10-18", "Sun", ScaleMode::True, 500, false, 0},
                {"saturn_true", "2026-10-18", "Saturn", ScaleMode::True, 1, false, 0},
                {"overview_compressed", "2026-10-18", "Sun", ScaleMode::Compressed, 1, false, 0},
                {"ship_saturn", "2026-10-18", "Saturn", ScaleMode::True, 1, true, 0.004},
            };
            int failures = 0;
            for (const Smoke& s : views) {
                CalendarDate c;
                parseIsoDate(s.date, c);
                jd = utcToTdb(julianDate(c));
                cam.focus = sys.find(s.focus);
                if (cam.ship) world->disembark();
                cam.distance = s.distanceAu * kAuKm;
                view.scale.mode = s.mode;
                view.scale.enlarge = s.enlarge;
                world->boardPending = s.ship;
                const FrameReport r = frame("paused");
                std::vector<uint8_t> rgba;
                int32_t w = 0, h = 0;
                engine->readPixels(rgba, w, h);
                const double lit = litFraction(rgba);
                const std::string out = args.smokeDir + "/" + s.name + ".png";
                engine->saveScreenshotPng(out);
                const bool ok = lit > 0.002 && r.spheres >= 38 && r.points > 1000 && r.lineVertices > 1000 &&
                                r.labels >= 2 && (!s.ship || r.shipTriangles > 0);
                std::printf("%s: %s  lit %.4f, %d spheres, %d points, %d line vertices, %d labels, %d ship triangles -> %s\n",
                            s.name, ok ? "ok" : "FAIL", lit, r.spheres, r.points, r.lineVertices, r.labels,
                            r.shipTriangles, out.c_str());
                if (!ok) ++failures;
            }
            return failures == 0 ? 0 : 1;
        }

        if (args.headless) {
            if (!args.framesDir.empty()) {
                std::filesystem::create_directories(args.framesDir);
                const int count = std::max(args.count, 1);
                double hottest = 0;
                FrameReport last;
                for (int i = 0; i < count; ++i) {
                    world->shipDt = i == 0 ? 0.0 : 1.0 / 30;
                    char rate[64];
                    std::snprintf(rate, sizeof rate, "%g d per frame", args.step);
                    last = frame(rate);
                    hottest = std::max(hottest, world->ship.heat());
                    char name[64];
                    std::snprintf(name, sizeof name, "/frame_%04d.png", i);
                    engine->saveScreenshotPng(args.framesDir + name);
                    jd = std::clamp(jd + args.step, sys.firstJd(), sys.lastJd());
                    cam.yawDeg += args.yawStep;
                }
                std::printf("wrote %d frames to %s\n", count, args.framesDir.c_str());
                if (cam.ship) {
                    const Touchdown& td = world->ship.touchdown();
                    std::printf("ship: touchdown %s%s%s, hottest %.0f%%, fuel %.0f%%, %d labels in the last frame\n",
                                touchdownName(td.grade), td.body >= 0 ? " on " : "",
                                td.body >= 0 ? sys.bodies()[static_cast<size_t>(td.body)].name.c_str() : "", hottest * 100,
                                world->ship.fuel() * 100, last.labels);
                }
            } else {
                frame("paused");
                const std::string out = args.screenshot.empty() ? "orrery.png" : args.screenshot;
                engine->saveScreenshotPng(out);
                std::printf("wrote %s\n", out.c_str());
            }
            return 0;
        }

        // ---- interactive
#if !defined(__EMSCRIPTEN__)
        // Headless runs never read or write records, and neither does a run a
        // test harness drives (Strata's STRATA_DRIVE): its bests are not the player's.
        if (world->engine->driven()) {
            std::puts("orrery: driven by a test harness; mission records are not read or written");
        } else {
            world->recordsFile = recordsPath();
            world->loadRecords();
        }
#endif
        auto* live = new Interactive(std::move(world), rateIndexFor(args.rate));
        if (live->w->missionPending >= 0) {
            live->rateIndex = kRealTimeRate;
            live->paused = false;
        }
#if defined(__EMSCRIPTEN__)
        // The browser runs the frames (requestAnimationFrame); main returns and
        // the session lives on (it is never freed: the page owns its lifetime).
        gLive = live;
        std::puts("orrery: running in the browser; the toolbar and H list the controls.");
        emscripten_set_main_loop_arg([](void* p) { static_cast<Interactive*>(p)->step(); }, live, 0, false);
#else
        std::puts("H for help in the window. Esc quits.");
        while (live->running) live->step();
        delete live;
#endif
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "orrery: %s\n", e.what());
        return 1;
    }
}
