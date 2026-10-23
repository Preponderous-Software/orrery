// Draws one frame of the solar system with Strata.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "orrery/flight.hpp"
#include "orrery/scale.hpp"
#include "orrery/system.hpp"
#include "strata/strata.hpp"

namespace orrery {

struct CameraState {
    int focus = 0;          // body index the camera orbits (ignored in free flight; the target in the ship)
    bool freeFly = false;
    bool ship = false;      // ship mode: eye, look and up are the ship camera's, set by the app
    double yawDeg = -60;    // around ecliptic north
    double pitchDeg = 25;   // above the ecliptic
    double distance = 0;    // drawn km from the focus; 0 = pick a default on the next frame
    Vec3 eye;               // free flight and ship: drawn position
    Vec3 look{1, 0, 0}, up{0, 0, 1};  // ship: unit view direction and camera up
    double nearLimit = 0;   // > 0: the near plane is at most this far (the ship in front of the camera)
    double fovYDeg = 50;
};

// A line of HUD text the app sets, and its colour.
enum class HudTone { Plain, Dim, Go, Warn };
struct HudLine {
    std::string text;
    bool small = false;
    HudTone tone = HudTone::Plain;
};

struct ViewOptions {
    Scale scale;
    bool orbits = true;
    bool labels = true;
    bool smallBodies = true;
    bool axes = false;
    bool hud = true;
    bool help = false;
    float uiScale = 1;      // text size multiplier (the browser build: about devicePixelRatio)
    bool keyHints = true;   // the HUD's bottom line of keys (off on touch screens)
    std::string timeLine;   // set by the app: date and rate
    std::string prompt;     // set by the app: date entry, or empty
    const Ship* ship = nullptr;  // ship mode: drawn and reported on
    bool cockpit = false;        // ship mode: from the cockpit, so the ship itself is not drawn
    // Missions, set by the app: the ship HUD's mission line and the goal (or
    // a note) under it, and the mission list while it is open.
    std::string missionLine, missionGoal;
    HudTone missionTone = HudTone::Plain;
    std::vector<HudLine> panel;
};

// What the frame drew, for tests and the HUD.
struct FrameReport {
    int spheres = 0, points = 0, lineVertices = 0, labels = 0, shipTriangles = 0;
};

class Renderer {
public:
    // textureDir holds the planet maps (data/textures, DATA_SOURCES.md); a
    // missing map, or a Strata older than ABI 1.7, leaves that body a plain
    // coloured sphere.
    Renderer(strata::Engine& engine, const SolarSystem& system, const std::string& textureDir);
    int texturedBodies() const;

    // Drawn position of every body and small body for the current state.
    void layout(const SystemState& state, const Scale& scale);
    const std::vector<Vec3>& drawn() const { return drawn_; }
    double drawnRadius(int body) const { return drawnRadius_[static_cast<size_t>(body)]; }
    // A distance that frames the body (and its moons) nicely.
    double defaultDistance(int body) const;
    // The camera's eye in drawn space.
    Vec3 eyeOf(const CameraState& cam) const;
    Vec3 forwardOf(const CameraState& cam) const;
    // Every body as the ship sees it: drawn centre, surface and influence.
    std::vector<FlightBody> flightBodies(const SystemState& state) const;

    FrameReport draw(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width,
                     int height);

private:
    void drawOrbit(int body, const SystemState& state, const Scale& scale, const Vec3& origin);
    void addLine(const Vec3& a, const Vec3& b, const float color[4], const Vec3& origin);
    void addRings(int planet, const SystemState& state, const Scale& scale, const Vec3& origin);
    // Places (or hides) a textured body's sphere; false when the body has none.
    bool placeTextured(size_t body, const SystemState& state, const Vec3& origin);
    void addShip(const Ship& ship, const Vec3& origin);
    void addDust(const Ship& ship, const Vec3& origin);
    void addTargetMarker(int body, const CameraState& cam, const Vec3& origin, int width, int height);
    // Pixel position of a camera-relative point; false behind the camera.
    bool project(const Vec3& rel, int width, int height, float& sx, float& sy, float& clipW) const;
    void text(float x, float y, float size, const float color[4], const std::string& s);
    void flightHud(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width, int height);
    void hud(const SystemState& state, const CameraState& cam, const ViewOptions& view, int width, int height);

    strata::Engine& engine_;
    const SolarSystem& sys_;
    std::vector<Vec3> drawn_;
    std::vector<double> drawnRadius_;
    std::vector<Vec3> drawnSmall_;

    StrataCamera camera_{};
    std::vector<StrataRenderItem> items_;
    std::vector<StrataPoint> points_;
    std::vector<StrataLineVertex> lines_;
    std::vector<StrataText> texts_;
    std::deque<std::string> textStorage_;  // deque: c_str() stays valid as it grows
    std::vector<float> meshPositions_, meshNormals_, meshColors_;
    std::vector<uint32_t> meshIndices_;
    float textScale_ = 1;    // text(): multiplies position and size (the HUD's uiScale)
    uint32_t ringMesh_ = 0;  // this frame's ring mesh, replaced every frame

    // Textured spheres (ABI 1.7): one mesh per mapped body, built once around
    // the origin in the body's own frame and turned by a model matrix each frame.
    struct TexturedBody {
        uint32_t texture = 0;
        uint32_t mesh = 0;  // 0 while hidden
        bool unlit = false;
    };
    std::vector<TexturedBody> textured_;  // indexed like sys_.bodies(); texture 0 = none
    std::vector<float> spherePositions_, sphereUvs_;
    std::vector<uint32_t> sphereIndices_;
};

}  // namespace orrery
