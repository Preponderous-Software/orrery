// The catalogue's CSV dialect (src/orrery/csv.hpp).
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "check.hpp"
#include "orrery/csv.hpp"

using namespace orrery;

namespace {

// Writes `text` to `path` (in the test's working directory) and returns the path.
std::string writeFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return path;
}

// The message of the std::runtime_error `f` throws, or "" if it throws none.
template <class F>
std::string errorOf(F f) {
    try {
        f();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

int main() {
    SECTION("comments and blank lines are skipped; the first other line is the header");
    const std::string good = writeFile("test_csv_good.csv",
                                       "# a comment\n"
                                       "\n"
                                       "name,radius_km,gm\r\n"
                                       "# another comment\n"
                                       "Earth,6371.0084,398600.435507\r\n"
                                       "Ceres,469.7,\n"
                                       ",,\n");
    const CsvTable t = readCsv(good);
    CHECK(t.path == good);
    CHECK(t.header.size() == 3 && t.header[0] == "name" && t.header[2] == "gm");  // '\r' stripped
    CHECK(t.column.at("name") == 0 && t.column.at("radius_km") == 1 && t.column.at("gm") == 2);
    CHECK_MSG(t.rows.size() == 3, std::to_string(t.rows.size()));

    SECTION("a trailing comma is an empty last cell");
    CHECK(t.rows[1].size() == 3 && t.rows[1][2].empty());
    CHECK(t.rows[2].size() == 3 && t.rows[2][0].empty() && t.rows[2][2].empty());

    SECTION("text, number and numberOr read cells by column name");
    CHECK(t.text(t.rows[0], "name") == "Earth");
    CHECK(t.number(t.rows[0], "radius_km") == 6371.0084);
    CHECK(t.numberOr(t.rows[0], "gm", -1) == 398600.435507);
    CHECK(t.numberOr(t.rows[1], "gm", -1) == -1);  // empty cell -> the fallback
    CHECK(t.numberOr(t.rows[1], "gm", 0) == 0);

    SECTION("bad cells and unknown columns throw, naming the file");
    std::string err = errorOf([&] { t.number(t.rows[1], "gm"); });
    CHECK_MSG(contains(err, good) && contains(err, "is not a number"), err);
    err = errorOf([&] { t.number(t.rows[0], "name"); });
    CHECK_MSG(contains(err, "'Earth' in column name is not a number"), err);
    err = errorOf([&] { t.text(t.rows[0], "mass"); });
    CHECK_MSG(contains(err, good) && contains(err, "no column 'mass'"), err);
    err = errorOf([&] { t.numberOr(t.rows[0], "mass", 0); });
    CHECK_MSG(contains(err, "no column 'mass'"), err);
    const std::string trailing = writeFile("test_csv_trailing.csv", "x\n12abc\n");
    const CsvTable tt = readCsv(trailing);
    err = errorOf([&] { tt.number(tt.rows[0], "x"); });
    CHECK_MSG(contains(err, "'12abc' in column x is not a number"), err);

    SECTION("malformed files throw, naming the file");
    const std::string ragged = writeFile("test_csv_ragged.csv", "a,b\n1,2\n1,2,3\n");
    err = errorOf([&] { readCsv(ragged); });
    CHECK_MSG(contains(err, ragged) && contains(err, "row has 3 cells, header has 2"), err);
    const std::string empty = writeFile("test_csv_empty.csv", "# only a comment\n\n");
    err = errorOf([&] { readCsv(empty); });
    CHECK_MSG(contains(err, empty) && contains(err, "no header"), err);
    err = errorOf([&] { readCsv("test_csv_does_not_exist.csv"); });
    CHECK_MSG(err == "cannot open test_csv_does_not_exist.csv", err);

    for (const std::string& path : {good, trailing, ragged, empty}) std::remove(path.c_str());
    return check::finish();
}
