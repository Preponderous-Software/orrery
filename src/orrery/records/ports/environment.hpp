// Port: the process's environment variables.
#pragma once

#include <string>

namespace orrery {

// Contract: get(name) returns the variable's value, or "" when it is unset or
// set to nothing (the two mean the same to every caller). It never fails and
// does not block.
class Environment {
public:
    virtual ~Environment() = default;
    virtual std::string get(const std::string& name) const = 0;
};

}  // namespace orrery
