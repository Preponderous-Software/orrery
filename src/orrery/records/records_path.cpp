#include "orrery/records/records_path.hpp"

#include <string>

namespace orrery {

std::filesystem::path recordsPath(const Environment& env, bool windows) {
    namespace fs = std::filesystem;
    if (const std::string p = env.get("ORRERY_RECORDS"); !p.empty()) return p;
    if (windows) {
        if (const std::string a = env.get("APPDATA"); !a.empty()) return fs::path(a) / "orrery" / "records.txt";
    } else {
        if (const std::string x = env.get("XDG_DATA_HOME"); !x.empty()) return fs::path(x) / "orrery" / "records.txt";
        if (const std::string h = env.get("HOME"); !h.empty()) return fs::path(h) / ".local" / "share" / "orrery" / "records.txt";
    }
    return {};
}

}  // namespace orrery
