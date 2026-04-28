#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dosbundle {

namespace fs = std::filesystem;

std::string trim(const std::string& value);
bool starts_with(const std::string& value, std::string_view prefix);
std::vector<std::string> split(const std::string& value, char separator);
fs::path resolve_executable_path(const char* argv0);

} // namespace dosbundle
