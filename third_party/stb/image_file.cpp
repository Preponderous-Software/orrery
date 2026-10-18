// The one translation unit holding stb_image, built without the project's
// warning flags. STB_IMAGE_STATIC keeps every stb symbol local to this file,
// so it cannot collide with another copy linked in (Strata has one).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

#include "image_file.hpp"

namespace orrery {

bool loadRgba8(const std::string& path, std::vector<uint8_t>& rgba, int& width, int& height, std::string* error) {
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        if (error) *error = stbi_failure_reason();
        return false;
    }
    rgba.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(data);
    return true;
}

}  // namespace orrery
