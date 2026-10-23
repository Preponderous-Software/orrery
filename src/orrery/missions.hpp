// Missions: short goals for the ship, each timed, with the best time kept per
// device. A mission names its bodies by catalogue name and sets its own date,
// so times on one mission compare. No GPU here; the app resolves the names,
// places the ship and saves the records.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "orrery/flight.hpp"

namespace orrery {

struct MissionGoal {
    enum Kind { Land, Pass };
    Kind kind = Land;
    std::string body;  // catalogue name
    std::string text;  // what the HUD asks for: "land on Io"
    // Land: the touchdown point's latitude in the body's frame (+Z = north
    // pole) must be at least this.
    double minLatitudeDeg = -90;
    // Land: when set, the touchdown point must face away from this body
    // (more than 120 degrees from the point under it): a far side.
    std::string awayFrom;
    // Pass: come within this many km of the body's surface (of its centre
    // when it has no radius, as comets and spacecraft do not).
    double withinKm = 0;
};

struct Mission {
    std::string id;     // stable: the records are keyed on it
    std::string kind;   // "tour", "race", "precision" or "sightseeing"
    std::string title;
    std::string brief;  // one line for the mission list
    std::string date;   // UTC, YYYY-MM-DD: the mission flies at this date
    std::string start;  // the ship starts at the autopilot standoff from this body, facing the first goal
    std::vector<MissionGoal> goals;  // all of them, in any order
    double parSeconds = 0;           // races: the time to beat; 0 = none
};

// The missions, in the order the mission list shows them (keys 1-9, then 0).
const std::vector<Mission>& missions();
int findMission(const std::string& id);  // -1 if none

// Where a mission puts the ship: at the standoff from `start`, on the side
// facing `toward`, nose toward it.
void missionStartPose(const FlightBody& start, const FlightBody& toward, Vec3& position, Vec3& forward);

// One run of a mission: call update() once with dt = 0 when the ship is in
// place (flybys are measured from there), then after every ship step.
class MissionRun {
public:
    // Starts `m`. `find` maps a catalogue name to a body index, -1 if there is
    // none; false (and nothing started) if any name the mission uses is unknown.
    bool start(const Mission& m, const std::function<int(const std::string&)>& find);
    void abandon() { mission_ = nullptr; }
    // dt: the seconds the ship just flew.
    void update(double dt, const Ship& ship, const std::vector<FlightBody>& bodies);

    const Mission* mission() const { return mission_; }
    bool running() const { return mission_ && !complete_; }
    bool complete() const { return mission_ && complete_; }
    double seconds() const { return seconds_; }
    int goalCount() const { return static_cast<int>(done_.size()); }
    int goalsDone() const;
    bool goalDone(int goal) const { return done_[static_cast<size_t>(goal)]; }
    int goalBody(int goal) const { return goalBody_[static_cast<size_t>(goal)]; }
    int nextGoal() const;  // the first goal not yet done, or -1
    // Set while the ship sits on a goal body somewhere that does not count:
    // what was wrong, for the HUD. Empty otherwise.
    const std::string& miss() const { return miss_; }

private:
    const Mission* mission_ = nullptr;
    bool complete_ = false;
    double seconds_ = 0;
    std::vector<bool> done_;
    std::vector<int> goalBody_, awayBody_;
    std::vector<Vec3> lastRel_;   // Pass goals: the ship from the body at the last update
    std::vector<bool> haveLast_;
    std::string miss_;
};

// Best times: one "id seconds" line per mission. Lines that do not parse are
// skipped, so a damaged file loses only those records.
std::map<std::string, double> parseRecords(const std::string& text);
std::string formatRecords(const std::map<std::string, double>& records);
// Keeps a finished time if it beats the record; true when it does.
bool recordTime(std::map<std::string, double>& records, const std::string& id, double seconds);
// "0:31.2", "12:05.0", "1:02:03".
std::string clockText(double seconds);

}  // namespace orrery
