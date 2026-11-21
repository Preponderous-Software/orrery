// The best mission times kept on this device (core: talks to a RecordsStore
// port, never to files).
#pragma once

#include <map>
#include <memory>
#include <string>

#include "orrery/records/ports/records_store.hpp"

namespace orrery {

class RecordBook {
public:
    // Loads what the store keeps.
    explicit RecordBook(std::unique_ptr<RecordsStore> store);

    const std::map<std::string, double>& records() const { return records_; }
    // Bumped with each new best (the browser page saves the records then).
    int version() const { return version_; }

    // A finished mission: keeps the time if it beats the record, and then
    // saves the records. True when it was a new best.
    bool finish(const std::string& id, double seconds);
    // The records the browser page kept (its localStorage), in place of these.
    void replace(std::map<std::string, double> records) { records_ = std::move(records); }

private:
    std::unique_ptr<RecordsStore> store_;
    std::map<std::string, double> records_;
    int version_ = 0;
};

}  // namespace orrery
