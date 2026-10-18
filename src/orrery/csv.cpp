#include "orrery/csv.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace orrery {
namespace {

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, ',')) out.push_back(cell);
    if (!line.empty() && line.back() == ',') out.emplace_back();
    return out;
}

}  // namespace

CsvTable readCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    CsvTable t;
    t.path = path;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (t.header.empty()) {
            t.header = split(line);
            for (size_t i = 0; i < t.header.size(); ++i) t.column[t.header[i]] = i;
            continue;
        }
        auto row = split(line);
        if (row.size() != t.header.size()) {
            throw std::runtime_error(path + ": row has " + std::to_string(row.size()) + " cells, header has " +
                                     std::to_string(t.header.size()));
        }
        t.rows.push_back(std::move(row));
    }
    if (t.header.empty()) throw std::runtime_error(path + ": no header");
    return t;
}

const std::string& CsvTable::text(const std::vector<std::string>& row, const std::string& name) const {
    const auto it = column.find(name);
    if (it == column.end()) throw std::runtime_error(path + ": no column '" + name + "'");
    return row[it->second];
}

double CsvTable::number(const std::vector<std::string>& row, const std::string& name) const {
    const std::string& s = text(row, name);
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (s.empty() || *end != '\0') throw std::runtime_error(path + ": '" + s + "' in column " + name + " is not a number");
    return v;
}

double CsvTable::numberOr(const std::vector<std::string>& row, const std::string& name, double fallback) const {
    const std::string& s = text(row, name);
    return s.empty() ? fallback : number(row, name);
}

}  // namespace orrery
