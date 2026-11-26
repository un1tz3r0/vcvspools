#pragma once
#include <string>

inline std::string fixture(const std::string& name) { return std::string(SPOOLS_FIXTURES) + "/" + name; }
