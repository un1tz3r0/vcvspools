#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace spools {

// UTF-8 paths on every platform.
bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string* error = nullptr);
bool writeFile(const std::string& path, const uint8_t* data, size_t size, std::string* error = nullptr);
std::string fileExtension(const std::string& path);  // lowercase, without the dot

}
