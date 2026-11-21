// Composition root for the mission records: builds a RecordBook on the
// production adapters.
#pragma once

#include "orrery/records/record_book.hpp"

namespace orrery {

// The desktop's records: records.txt at recordsPath() of the real
// environment, or none kept when no data directory can be found.
RecordBook openDesktopRecords();
// Records that are not read or written: headless and driven runs, and the
// browser, whose page loads and saves them itself.
RecordBook unkeptRecords();

}  // namespace orrery
