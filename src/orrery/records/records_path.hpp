// Where the desktop build keeps the mission records (core: no I/O).
#pragma once

#include <filesystem>

#include "orrery/records/ports/environment.hpp"

namespace orrery {

// $ORRERY_RECORDS, else the user's data directory: %APPDATA%\orrery on
// Windows, $XDG_DATA_HOME/orrery or ~/.local/share/orrery elsewhere, each
// /records.txt. Empty when none of them is set: the records are not kept.
std::filesystem::path recordsPath(const Environment& env, bool windows);

}  // namespace orrery
