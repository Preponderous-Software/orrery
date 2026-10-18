// Decodes a JPEG or PNG file to tightly packed RGBA8, top row first.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orrery {

// False (with a reason in *error) when the file cannot be read or decoded.
bool loadRgba8(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height, std::string* error);

}  // namespace orrery
