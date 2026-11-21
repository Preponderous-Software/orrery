// Adapter: the real environment, through std::getenv.
#pragma once

#include <string>

#include "orrery/records/ports/environment.hpp"

namespace orrery {

class ProcessEnvironment final : public Environment {
public:
    std::string get(const std::string& name) const override;
};

}  // namespace orrery
