// The catalogue's CSV dialect: '#' comment lines, one header line, comma
// separated, no quoting (text fields never contain commas).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace orrery {

struct CsvTable {
    std::string path;
    std::vector<std::string> header;
    std::map<std::string, size_t> column;
    std::vector<std::vector<std::string>> rows;

    // Throws std::runtime_error naming the file and column.
    const std::string& text(const std::vector<std::string>& row, const std::string& name) const;
    double number(const std::vector<std::string>& row, const std::string& name) const;
    // NaN for an empty cell.
    double numberOr(const std::vector<std::string>& row, const std::string& name, double fallback) const;
};

CsvTable readCsv(const std::string& path);

}  // namespace orrery
