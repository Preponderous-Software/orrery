#include "orrery/records/adapters/process_environment.hpp"

#include <cstdlib>

namespace orrery {

std::string ProcessEnvironment::get(const std::string& name) const {
    const char* value = std::getenv(name.c_str());
    return value ? value : "";
}

}  // namespace orrery
