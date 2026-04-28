#pragma once

#include <filesystem>
#include <optional>

namespace dosbundle {

namespace fs = std::filesystem;

struct AppConfig {
	std::optional<fs::path> dosbox_path = std::nullopt;
	std::optional<fs::path> tar_path = std::nullopt;
	std::optional<fs::path> extract_root = std::nullopt;
};

const AppConfig& load_app_config();
fs::path app_config_path();

} // namespace dosbundle
