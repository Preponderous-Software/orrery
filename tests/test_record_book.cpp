// The mission records' ports and core: the RecordsStore contract (a fake, and
// the desktop's records.txt on a temporary directory), the RecordBook's policy
// (load at start; save only a new best, and say so with a new version), and
// where the desktop keeps the file (the README's precedence, both platforms).
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>

#include "check.hpp"
#include "orrery/records/adapters/file_records_store.hpp"
#include "orrery/records/adapters/no_records_store.hpp"
#include "orrery/records/adapters/process_environment.hpp"
#include "orrery/records/ports/environment.hpp"
#include "orrery/records/ports/records_store.hpp"
#include "orrery/records/record_book.hpp"
#include "orrery/records/records_path.hpp"

using namespace orrery;
namespace fs = std::filesystem;
using Records = std::map<std::string, double>;

namespace {

class FakeRecordsStore final : public RecordsStore {
public:
    Records kept;
    int saves = 0;
    bool fail = false;
    Records load() override { return kept; }
    bool save(const Records& records) override {
        ++saves;
        if (fail) return false;
        kept = records;
        return true;
    }
};

class FakeEnvironment final : public Environment {
public:
    std::map<std::string, std::string> vars;
    std::string get(const std::string& name) const override {
        const auto it = vars.find(name);
        return it == vars.end() ? "" : it->second;
    }
};

// What every store that keeps records must do.
void keepingContract(RecordsStore& store, const char* name) {
    std::printf("  contract: %s\n", name);
    CHECK_MSG(store.load().empty(), name);
    const Records r{{"moon-run", 25.057}, {"hot-entry", 32.539}};
    CHECK_MSG(store.save(r), name);
    CHECK_MSG(store.load() == r, name);
    const Records faster{{"moon-run", 24.5}};
    CHECK_MSG(store.save(faster), name);
    CHECK_MSG(store.load() == faster, name);
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    const fs::path tmp = fs::temp_directory_path() / ("orrery-test-record-book-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(tmp);

    SECTION("RecordsStore contract: the fake, and records.txt (its directory made on the first save)");
    {
        FakeRecordsStore fake;
        keepingContract(fake, "FakeRecordsStore");
        FileRecordsStore file(tmp / "nested" / "orrery" / "records.txt");
        keepingContract(file, "FileRecordsStore");
        CHECK(readAll(tmp / "nested" / "orrery" / "records.txt") == "moon-run 24.500\n");
    }

    SECTION("records.txt: damaged lines are skipped; a file that cannot be written is not saved");
    {
        fs::create_directories(tmp);
        std::ofstream(tmp / "damaged.txt", std::ios::binary) << "moon-run 25.057\nnot a record\nhalley -3\n";
        FileRecordsStore damaged(tmp / "damaged.txt");
        CHECK((damaged.load() == Records{{"moon-run", 25.057}}));
        fs::create_directories(tmp / "a-directory");
        FileRecordsStore unwritable(tmp / "a-directory");  // a directory is not a file
        CHECK(!unwritable.save(Records{{"moon-run", 1}}));
    }

    SECTION("NoRecordsStore keeps nothing and never fails");
    {
        NoRecordsStore none;
        CHECK(none.load().empty());
        CHECK(none.save(Records{{"moon-run", 25}}));
        CHECK(none.load().empty());
    }

    SECTION("RecordBook: loads at start; only a new best is kept, saved and versioned");
    {
        auto store = std::make_unique<FakeRecordsStore>();
        store->kept = {{"moon-run", 25.057}};
        FakeRecordsStore* fake = store.get();
        RecordBook book(std::move(store));
        CHECK((book.records() == Records{{"moon-run", 25.057}}));
        CHECK(book.version() == 0);
        CHECK(!book.finish("moon-run", 30) && fake->saves == 0 && book.version() == 0);
        CHECK(!book.finish("moon-run", 25.057) && fake->saves == 0);  // a tie is not a record
        CHECK(book.finish("moon-run", 24.5) && fake->saves == 1 && book.version() == 1);
        CHECK(fake->kept.at("moon-run") == 24.5);
        CHECK(book.finish("halley", 75) && fake->saves == 2 && book.version() == 2 && fake->kept.size() == 2);
    }

    SECTION("RecordBook: a failed save still counts the new best (the browser page saves on the version)");
    {
        auto store = std::make_unique<FakeRecordsStore>();
        store->fail = true;
        RecordBook book(std::move(store));
        CHECK(book.finish("moon-run", 25) && book.version() == 1 && book.records().at("moon-run") == 25);
    }

    SECTION("RecordBook: the page's records replace these, without a save or a new version");
    {
        auto store = std::make_unique<FakeRecordsStore>();
        FakeRecordsStore* fake = store.get();
        RecordBook book(std::move(store));
        book.replace(Records{{"galilean", 600}});
        CHECK((book.records() == Records{{"galilean", 600}}) && fake->saves == 0 && book.version() == 0);
    }

    SECTION("recordsPath: $ORRERY_RECORDS, else the data directory; an empty variable is unset");
    {
        FakeEnvironment env;
        CHECK(recordsPath(env, false).empty());
        CHECK(recordsPath(env, true).empty());
        env.vars = {{"HOME", "/home/p"}};
        CHECK(recordsPath(env, false) == fs::path("/home/p") / ".local" / "share" / "orrery" / "records.txt");
        CHECK(recordsPath(env, true).empty());  // Windows does not fall back to HOME
        env.vars["XDG_DATA_HOME"] = "/x";
        CHECK(recordsPath(env, false) == fs::path("/x") / "orrery" / "records.txt");
        env.vars["XDG_DATA_HOME"] = "";
        CHECK(recordsPath(env, false) == fs::path("/home/p") / ".local" / "share" / "orrery" / "records.txt");
        env.vars["APPDATA"] = "C:/Users/p/AppData/Roaming";
        CHECK(recordsPath(env, true) == fs::path("C:/Users/p/AppData/Roaming") / "orrery" / "records.txt");
        CHECK(recordsPath(env, false) == fs::path("/home/p") / ".local" / "share" / "orrery" / "records.txt");
        env.vars["ORRERY_RECORDS"] = "/r/best.txt";
        CHECK(recordsPath(env, false) == fs::path("/r/best.txt"));
        CHECK(recordsPath(env, true) == fs::path("/r/best.txt"));
    }

    SECTION("ProcessEnvironment reads the real environment; unset is empty");
    {
        ProcessEnvironment env;
        CHECK(env.get("ORRERY_TEST_SURELY_UNSET_VARIABLE").empty());
        CHECK(!env.get("PATH").empty());
    }

    fs::remove_all(tmp);
    return check::finish();
}
