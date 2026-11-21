#include "orrery/records/record_book.hpp"

#include "orrery/missions.hpp"

namespace orrery {

RecordBook::RecordBook(std::unique_ptr<RecordsStore> store) : store_(std::move(store)), records_(store_->load()) {}

bool RecordBook::finish(const std::string& id, double seconds) {
    if (!recordTime(records_, id, seconds)) return false;
    ++version_;
    store_->save(records_);
    return true;
}

}  // namespace orrery
