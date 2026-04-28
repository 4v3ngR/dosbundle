#include "config.h"

#include "common.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace dosbundle {

namespace {

std::optional<fs::path> resolve_optional_path(const std::string& value)
{
	if (value.empty()) {
		return std::nullopt;
	}

	const fs::path parsed = value;
	if (parsed.is_absolute()) {
		return parsed.lexically_normal();
	}

	const auto home = std::getenv("HOME");
	if (!home || !*home) {
		return fs::absolute(parsed).lexically_normal();
	}

	return (fs::path(home) / parsed).lexically_normal();
}

std::map<std::string, std::string> read_config_entries(const fs::path& path)
{
	std::ifstream input(path);
	if (!input) {
		return {};
	}

	std::map<std::string, std::string> values = {};
	std::string line = {};
	size_t line_number = 0;

	while (std::getline(input, line)) {
		++line_number;
		const auto stripped = trim(line);
		if (stripped.empty() || starts_with(stripped, "#")) {
			continue;
		}

		const auto equals = stripped.find('=');
		if (equals == std::string::npos) {
			throw std::runtime_error("invalid config line " + std::to_string(line_number) +
			                         " in " + path.string());
		}

		const auto key = trim(stripped.substr(0, equals));
		const auto value = trim(stripped.substr(equals + 1));
		if (key.empty()) {
			throw std::runtime_error("empty config key in " + path.string());
		}
		values[key] = value;
	}

	return values;
}

AppConfig read_app_config()
{
	const auto path = app_config_path();
	if (!fs::exists(path)) {
		return {};
	}

	const auto values = read_config_entries(path);

	AppConfig config = {};
	if (const auto it = values.find("dosbox_path"); it != values.end()) {
		config.dosbox_path = resolve_optional_path(it->second);
	}
	if (const auto it = values.find("tar_path"); it != values.end()) {
		config.tar_path = resolve_optional_path(it->second);
	}
	if (const auto it = values.find("extract_root"); it != values.end()) {
		config.extract_root = resolve_optional_path(it->second);
	}
	if (const auto it = values.find("temp_directory"); it != values.end() &&
	                                  !config.extract_root) {
		config.extract_root = resolve_optional_path(it->second);
	}

	return config;
}

} // namespace

fs::path app_config_path()
{
	if (const auto* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
	    xdg_config_home && *xdg_config_home) {
		return (fs::path(xdg_config_home) / "dosbundle.conf").lexically_normal();
	}

	if (const auto* home = std::getenv("HOME"); home && *home) {
		return (fs::path(home) / ".config" / "dosbundle.conf").lexically_normal();
	}

	return fs::path(".dosbundle.conf");
}

const AppConfig& load_app_config()
{
	static const auto config = read_app_config();
	return config;
}

} // namespace dosbundle
