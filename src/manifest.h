#pragma once

#include <filesystem>
#include <string>

namespace dosbundle {

namespace fs = std::filesystem;

struct Manifest {
	std::string name = {};
	std::string version = {};
	std::string target_platform = {};
	std::string payload_c_drive_dir = {};
	std::string payload_startup = {};
	std::string dosbox_config = {};
	std::string output_path = {};
};

struct ResolvedManifest {
	Manifest raw = {};
	fs::path manifest_path = {};
	fs::path manifest_dir = {};
	fs::path c_drive_dir = {};
	fs::path startup_path = {};
	fs::path dosbox_config = {};
	fs::path output_path = {};
};

Manifest parse_manifest_file(const fs::path& manifest_path);
ResolvedManifest resolve_manifest(const fs::path& manifest_path);

} // namespace dosbundle
