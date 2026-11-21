#include "orrery/records/adapters/file_records_store.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "orrery/missions.hpp"

namespace orrery {

std::map<std::string, double> FileRecordsStore::load() {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return {};  // none yet
    std::ostringstream text;
    text << in.rdbuf();
    return parseRecords(text.str());
}

bool FileRecordsStore::save(const std::map<std::string, double>& records) {
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    out << formatRecords(records);
    if (!out) std::fprintf(stderr, "could not save the mission records to %s\n", path_.string().c_str());
    return static_cast<bool>(out);
}

}  // namespace orrery
