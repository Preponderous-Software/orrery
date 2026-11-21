#include "orrery/records/service.hpp"

#include <memory>

#include "orrery/records/adapters/file_records_store.hpp"
#include "orrery/records/adapters/no_records_store.hpp"
#include "orrery/records/adapters/process_environment.hpp"
#include "orrery/records/records_path.hpp"

namespace orrery {

namespace {
#if defined(_WIN32)
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif
}  // namespace

RecordBook openDesktopRecords() {
    const auto path = recordsPath(ProcessEnvironment{}, kWindows);
    if (path.empty()) return unkeptRecords();
    return RecordBook(std::make_unique<FileRecordsStore>(path));
}

RecordBook unkeptRecords() { return RecordBook(std::make_unique<NoRecordsStore>()); }

}  // namespace orrery
