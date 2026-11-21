// Adapter: records that are not read or written (headless and driven runs,
// no data directory, and the browser, whose page keeps them in localStorage).
#pragma once

#include <map>
#include <string>

#include "orrery/records/ports/records_store.hpp"

namespace orrery {

class NoRecordsStore final : public RecordsStore {
public:
    std::map<std::string, double> load() override { return {}; }
    bool save(const std::map<std::string, double>&) override { return true; }
};

}  // namespace orrery
