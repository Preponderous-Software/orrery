// Port: where this device keeps its best mission times between runs.
#pragma once

#include <map>
#include <string>

namespace orrery {

// Best times, seconds by mission id (the records format in orrery/missions.hpp).
//
// Contract:
//  - load() returns the records kept so far; none yet, or none kept at all, is
//    an empty map, never an error. Lines a damaged store holds are skipped.
//  - save() replaces what is kept with `records` and returns true, or returns
//    false when they could not be kept (the adapter says why on stderr). A
//    store that keeps nothing returns true: there is nothing to fail.
//  - Both may block on the file system; call them from the game's thread only.
class RecordsStore {
public:
    virtual ~RecordsStore() = default;
    virtual std::map<std::string, double> load() = 0;
    virtual bool save(const std::map<std::string, double>& records) = 0;
};

}  // namespace orrery
