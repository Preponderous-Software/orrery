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
// --no-small-bodies, --no-hud, --axes.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "orrery/system.hpp"
#include "orrery/time.hpp"
#include "renderer.hpp"
#include "strata/strata.hpp"
#include "usda.hpp"

#ifndef ORRERY_SOURCE_DIR
#define ORRERY_SOURCE_DIR "."
#endif

using namespace orrery;

namespace {

// SDL3 scancodes (USB HID usage ids), as Strata reports them.
enum Key {
    kA = 4, kB = 5, kD = 7, kE = 8, kF = 9, kH = 11, kJ = 13, kL = 15, kM = 16, kN = 17, kO = 18, kP = 19, kQ = 20,
    kS = 22, kU = 24, kW = 26, kX = 27, k1 = 30, k0 = 39, kReturn = 40, kEscape = 41, kBackspace = 42, kTab = 43,
    kSpace = 44, kLeftBracket = 47, kRightBracket = 48, kComma = 54, kPeriod = 55, kHome = 74, kPageUp = 75,
    kPageDown = 78,
};
constexpr int kShift = 0x0001 | 0x0002;

// Time rates in days per second of wall-clock time; 1/86400 is real time.
const double kRates[] = {-365.25, -30, -7, -1, -1.0 / 24, -1.0 / 1440, -1.0 / 86400, 1.0 / 86400, 1.0 / 1440,
                         1.0 / 24, 1, 7, 30, 365.25};
constexpr int kRealTimeRate = 7;
const double kEnlargements[] = {1, 10, 100, 500, 2000};

struct Args {
    std::string data = std::string(ORRERY_SOURCE_DIR) + "/data";
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
};

int usage() {
    std::fputs(
        "usage: orrery [--data DIR] [--date YYYY-MM-DD[THH:MM[:SS]]] [--focus NAME] [--mode true|compressed]\n"
        "              [--enlarge K] [--rate DAYS_PER_SECOND] [--width W] [--height H]\n"
        "              [--yaw DEG] [--pitch DEG] [--distance-au AU] [--no-labels] [--no-orbits]\n"
        "              [--no-small-bodies] [--no-hud] [--axes]\n"
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
        else return false;
    }
    return a.width > 0 && a.height > 0 && a.enlarge >= 1;
}

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

}  // namespace

int main(int argc, char** argv) {
    Args args;
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
        const SolarSystem sys(args.data);
        std::printf("orrery: %zu bodies, %zu small bodies; DE440s covers JD %.1f..%.1f\n", sys.bodies().size(),
                    sys.smallBodies().size(), sys.firstJd(), sys.lastJd());

        double jd = utcToTdb(nowUtc());
        if (!args.date.empty()) {
            CalendarDate c;
            if (!parseIsoDate(args.date, c)) {
                std::fprintf(stderr, "bad --date '%s' (YYYY-MM-DD[THH:MM[:SS]], UTC)\n", args.date.c_str());
                return 2;
            }
            jd = utcToTdb(julianDate(c));
        }
        jd = std::clamp(jd, sys.firstJd(), sys.lastJd());

        SystemState state;
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
        auto engine = strata::Engine::create(desc);
        std::printf("GPU: %s\n", engine->gpuDescription().c_str());

        Renderer renderer(*engine, sys, args.data + "/textures");
        CameraState cam;
        ViewOptions view;
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
        cam.yawDeg = args.yaw;
        cam.pitchDeg = args.pitch;

        int width = args.width, height = args.height;
        auto frame = [&](const std::string& rate) {
            sys.evaluate(jd, state, view.smallBodies);
            renderer.layout(state, view.scale);
            if (cam.distance <= 0) {
                cam.distance = args.distanceAu > 0 ? args.distanceAu * kAuKm : renderer.defaultDistance(cam.focus);
            }
            view.timeLine = timeLine(jd, rate);
            return renderer.draw(state, cam, view, width, height);
        };

        // ---- smoke test: three views, each must be visibly drawn
        if (!args.smokeDir.empty()) {
            std::filesystem::create_directories(args.smokeDir);
            const Smoke views[] = {
                {"overview_true", "2026-10-18", "Sun", ScaleMode::True, 500},
                {"saturn_true", "2026-10-18", "Saturn", ScaleMode::True, 1},
                {"overview_compressed", "2026-10-18", "Sun", ScaleMode::Compressed, 1},
            };
            int failures = 0;
            for (const Smoke& s : views) {
                CalendarDate c;
                parseIsoDate(s.date, c);
                jd = utcToTdb(julianDate(c));
                cam.focus = sys.find(s.focus);
                cam.distance = 0;
                view.scale.mode = s.mode;
                view.scale.enlarge = s.enlarge;
                const FrameReport r = frame("paused");
                std::vector<uint8_t> rgba;
                int32_t w = 0, h = 0;
                engine->readPixels(rgba, w, h);
                const double lit = litFraction(rgba);
                const std::string out = args.smokeDir + "/" + s.name + ".png";
                engine->saveScreenshotPng(out);
                const bool ok = lit > 0.002 && r.spheres >= 38 && r.points > 1000 && r.lineVertices > 1000 && r.labels >= 2;
                std::printf("%s: %s  lit %.4f, %d spheres, %d points, %d line vertices, %d labels -> %s\n", s.name,
                            ok ? "ok" : "FAIL", lit, r.spheres, r.points, r.lineVertices, r.labels, out.c_str());
                if (!ok) ++failures;
            }
            return failures == 0 ? 0 : 1;
        }

        if (args.headless) {
            if (!args.framesDir.empty()) {
                std::filesystem::create_directories(args.framesDir);
                const int count = std::max(args.count, 1);
                for (int i = 0; i < count; ++i) {
                    char rate[64];
                    std::snprintf(rate, sizeof rate, "%g d per frame", args.step);
                    frame(rate);
                    char name[64];
                    std::snprintf(name, sizeof name, "/frame_%04d.png", i);
                    engine->saveScreenshotPng(args.framesDir + name);
                    jd = std::clamp(jd + args.step, sys.firstJd(), sys.lastJd());
                    cam.yawDeg += args.yawStep;
                }
                std::printf("wrote %d frames to %s\n", count, args.framesDir.c_str());
            } else {
                frame("paused");
                const std::string out = args.screenshot.empty() ? "orrery.png" : args.screenshot;
                engine->saveScreenshotPng(out);
                std::printf("wrote %s\n", out.c_str());
            }
            return 0;
        }

        // ---- interactive
        int rateIndex = nearestIndex(kRates, static_cast<int>(sizeof kRates / sizeof kRates[0]), args.rate);
        bool paused = false, running = true, dragging = false, entering = false;
        std::string entry;
        bool keys[512] = {};
        auto last = std::chrono::steady_clock::now();
        int shot = 0;
        std::puts("H for help in the window. Esc quits.");
        while (running) {
            StrataEvent ev;
            while (engine->pollEvent(ev)) {
                if (ev.type == STRATA_EVENT_QUIT) running = false;
                else if (ev.type == STRATA_EVENT_RESIZED) {
                    width = std::max(ev.width, 1);
                    height = std::max(ev.height, 1);
                } else if (ev.type == STRATA_EVENT_TEXT_INPUT && entering) {
                    for (const char* p = ev.text; *p; ++p) {
                        if (std::strchr("0123456789-:T ", *p) && entry.size() < 19) entry += *p;
                    }
                } else if (ev.type == STRATA_EVENT_KEY_UP && ev.scancode >= 0 && ev.scancode < 512) {
                    keys[ev.scancode] = false;
                } else if (ev.type == STRATA_EVENT_KEY_DOWN) {
                    if (ev.scancode >= 0 && ev.scancode < 512) keys[ev.scancode] = true;
                    const bool shift = (ev.modifiers & kShift) != 0;
                    if (entering) {
                        if (ev.scancode == kReturn) {
                            CalendarDate c;
                            if (parseIsoDate(entry, c)) jd = std::clamp(utcToTdb(julianDate(c)), sys.firstJd(), sys.lastJd());
                            entering = false;
                        } else if (ev.scancode == kEscape) {
                            entering = false;
                        } else if (ev.scancode == kBackspace && !entry.empty()) {
                            entry.pop_back();
                        }
                        continue;
                    }
                    switch (ev.scancode) {
                        case kEscape: running = false; break;
                        case kSpace: paused = !paused; break;
                        case kComma: rateIndex = std::max(rateIndex - 1, 0); break;
                        case kPeriod:
                            rateIndex = std::min(rateIndex + 1, static_cast<int>(sizeof kRates / sizeof kRates[0]) - 1);
                            break;
                        case kN:
                            jd = std::clamp(utcToTdb(nowUtc()), sys.firstJd(), sys.lastJd());
                            rateIndex = kRealTimeRate;
                            paused = false;
                            break;
                        case kJ: entering = true; entry.clear(); break;
                        case kTab:
                            cam.focus = cycle(sys, state, cam.focus, shift ? -1 : 1);
                            cam.freeFly = false;
                            cam.distance = renderer.defaultDistance(cam.focus);
                            break;
                        case kHome: cam.focus = 0; cam.freeFly = false; cam.distance = renderer.defaultDistance(0); break;
                        case kM:
                            view.scale.mode = view.scale.mode == ScaleMode::True ? ScaleMode::Compressed : ScaleMode::True;
                            renderer.layout(state, view.scale);
                            cam.distance = renderer.defaultDistance(cam.focus);
                            break;
                        case kLeftBracket:
                        case kRightBracket: {
                            int e = nearestIndex(kEnlargements, 5, view.scale.enlarge);
                            e = std::clamp(e + (ev.scancode == kRightBracket ? 1 : -1), 0, 4);
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
                        case kP: {
                            char name[64];
                            std::snprintf(name, sizeof name, "orrery-%03d.png", shot++);
                            engine->saveScreenshotPng(name);
                            std::printf("wrote %s\n", name);
                            break;
                        }
                        case kU: writeUsda("orrery.usda", sys, state); std::puts("wrote orrery.usda"); break;
                        default:
                            if (ev.scancode >= k1 && ev.scancode <= k0) {
                                const int digit = ev.scancode == k0 ? 0 : ev.scancode - k1 + 1;
                                if (digit < static_cast<int>(sys.bodies().size())) {
                                    cam.focus = digit;  // bodies.csv: Sun, Mercury..Neptune, Pluto
                                    cam.freeFly = false;
                                    cam.distance = renderer.defaultDistance(digit);
                                }
                            }
                    }
                } else if (ev.type == STRATA_EVENT_MOUSE_BUTTON_DOWN) {
                    dragging = true;
                } else if (ev.type == STRATA_EVENT_MOUSE_BUTTON_UP) {
                    dragging = false;
                } else if (ev.type == STRATA_EVENT_MOUSE_MOTION && dragging) {
                    const double sign = cam.freeFly ? -1 : 1;
                    cam.yawDeg -= ev.dx * 0.3 * sign;
                    cam.pitchDeg = std::clamp(cam.pitchDeg + ev.dy * 0.3 * sign, -89.0, 89.0);
                } else if (ev.type == STRATA_EVENT_MOUSE_WHEEL) {
                    cam.distance *= std::pow(0.85, ev.y);
                }
            }
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::min(std::chrono::duration<double>(now - last).count(), 0.1);
            last = now;
            const double rate = kRates[rateIndex];
            if (!paused) jd = std::clamp(jd + rate * dt, sys.firstJd(), sys.lastJd());
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
            view.prompt = entering ? "Jump to date (UTC, YYYY-MM-DD[THH:MM]): " + entry + "_" : "";
            frame(rateText(rate, paused));
            engine->present();
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "orrery: %s\n", e.what());
        return 1;
    }
}
