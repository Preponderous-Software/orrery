// Draws one frame of the solar system with Strata.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "orrery/scale.hpp"
#include "orrery/system.hpp"
#include "strata/strata.hpp"

namespace orrery {

struct CameraState {
    int focus = 0;          // body index the camera orbits (ignored in free flight)
    bool freeFly = false;
    double yawDeg = -60;    // around ecliptic north
    double pitchDeg = 25;   // above the ecliptic
    double distance = 0;    // drawn km from the focus; 0 = pick a default on the next frame
    Vec3 eye;               // free flight: drawn position
    double fovYDeg = 50;
};

struct ViewOptions {
    Scale scale;
    bool orbits = true;
    bool labels = true;
    bool smallBodies = true;
    bool axes = false;
    bool hud = true;
    bool help = false;
    std::string timeLine;   // set by the app: date and rate
    std::string prompt;     // set by the app: date entry, or empty
};

// What the frame drew, for tests and the HUD.
struct FrameReport {
    int spheres = 0, points = 0, lineVertices = 0, labels = 0;
};

class Renderer {
public:
    Renderer(strata::Engine& engine, const SolarSystem& system);

    // Drawn position of every body and small body for the current state.
    void layout(const SystemState& state, const Scale& scale);
    const std::vector<Vec3>& drawn() const { return drawn_; }
    double drawnRadius(int body) const { return drawnRadius_[static_cast<size_t>(body)]; }
    // A distance that frames the body (and its moons) nicely.
    double defaultDistance(int body) const;
    // The camera's eye in drawn space.
    Vec3 eyeOf(const CameraState& cam) const;
    Vec3 forwardOf(const CameraState& cam) const;

    FrameReport draw(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width,
                     int height);

private:
    void drawOrbit(int body, const SystemState& state, const Scale& scale, const Vec3& origin);
    void addLine(const Vec3& a, const Vec3& b, const float color[4], const Vec3& origin);
    void addRings(int planet, const SystemState& state, const Scale& scale, const Vec3& origin);
    void text(float x, float y, float size, const float color[4], const std::string& s);
    void hud(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width, int height);

    strata::Engine& engine_;
    const SolarSystem& sys_;
    std::vector<Vec3> drawn_;
    std::vector<double> drawnRadius_;
    std::vector<Vec3> drawnSmall_;

    std::vector<StrataRenderItem> items_;
    std::vector<StrataPoint> points_;
    std::vector<StrataLineVertex> lines_;
    std::vector<StrataText> texts_;
    std::deque<std::string> textStorage_;  // deque: c_str() stays valid as it grows
    std::vector<float> meshPositions_, meshNormals_, meshColors_;
    std::vector<uint32_t> meshIndices_;
};

}  // namespace orrery
