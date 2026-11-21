// Adapter: the desktop's records.txt.
#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "orrery/records/ports/records_store.hpp"

namespace orrery {

// Keeps the records in one text file, creating its directory on the first
// save. A missing file loads as no records.
class FileRecordsStore final : public RecordsStore {
public:
    explicit FileRecordsStore(std::filesystem::path path) : path_(std::move(path)) {}
    std::map<std::string, double> load() override;
    bool save(const std::map<std::string, double>& records) override;

private:
    std::filesystem::path path_;
};

}  // namespace orrery
