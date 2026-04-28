#include "bundle.h"

#include "common.h"

#include <fstream>
#include <map>
#include <stdexcept>

namespace dosbundle {

namespace {

void copy_tree(const fs::path& source, const fs::path& destination)
{
	fs::copy(source,
	         destination,
	         fs::copy_options::recursive | fs::copy_options::copy_symlinks);
}

void write_summary(const ResolvedManifest& manifest, const fs::path& destination)
{
	std::ofstream output(destination);
	if (!output) {
		throw std::runtime_error("unable to write summary file: " + destination.string());
	}

	output << "name=" << manifest.raw.name << '\n';
	output << "version=" << manifest.raw.version << '\n';
	output << "target_platform=" << manifest.raw.target_platform << '\n';
	output << "manifest=" << manifest.manifest_path << '\n';
	output << "c_drive_dir=" << manifest.c_drive_dir << '\n';
	output << "startup=" << manifest.startup_path << '\n';
	output << "startup_relative_path=" << manifest.raw.payload_startup << '\n';
	output << "dosbox_config=" << manifest.dosbox_config << '\n';
	output << "output_path=" << manifest.output_path << '\n';
}

void write_launch_metadata(const ResolvedManifest& manifest, const fs::path& destination)
{
	std::ofstream output(destination);
	if (!output) {
		throw std::runtime_error("unable to write launch metadata: " + destination.string());
	}

	output << "startup_relative_path=" << manifest.raw.payload_startup << '\n';
}

std::map<std::string, std::string> read_key_value_file(const fs::path& path)
{
	std::ifstream input(path);
	if (!input) {
		throw std::runtime_error("unable to open metadata file: " + path.string());
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
			throw std::runtime_error("invalid metadata line " + std::to_string(line_number) +
			                         " in " + path.string());
		}

		const auto key = trim(stripped.substr(0, equals));
		const auto value = trim(stripped.substr(equals + 1));
		if (key.empty()) {
			throw std::runtime_error("empty metadata key in " + path.string());
		}
		values[key] = value;
	}

	return values;
}

} // namespace

void stage_bundle(const ResolvedManifest& manifest, const bool force_overwrite)
{
	if (fs::exists(manifest.output_path)) {
		if (!force_overwrite) {
			throw std::runtime_error("output path already exists: " +
			                         manifest.output_path.string());
		}
		fs::remove_all(manifest.output_path);
	}

	fs::create_directories(manifest.output_path / "payload");
	fs::create_directories(manifest.output_path / "dosbox");
	fs::create_directories(manifest.output_path / "manifest");
	fs::create_directories(manifest.output_path / "startup");

	copy_tree(manifest.c_drive_dir, manifest.output_path / "payload" / "c");
	fs::copy_file(manifest.dosbox_config,
	              manifest.output_path / "dosbox" / "dosbox.conf");
	fs::copy_file(manifest.manifest_path,
	              manifest.output_path / "manifest" / "bundle.toml");
	fs::copy_file(manifest.startup_path,
	              manifest.output_path / "startup" / manifest.startup_path.filename());
	write_launch_metadata(manifest, manifest.output_path / "manifest" / "launch.properties");
	write_summary(manifest, manifest.output_path / "manifest" / "summary.txt");
}

BundleMetadata load_bundle_metadata(const fs::path& bundle_path)
{
	const auto absolute_bundle = fs::absolute(bundle_path).lexically_normal();
	const auto launch_metadata_path = absolute_bundle / "manifest" / "launch.properties";
	const auto values = read_key_value_file(launch_metadata_path);

	const auto it = values.find("startup_relative_path");
	if (it == values.end() || it->second.empty()) {
		throw std::runtime_error("bundle launch metadata is missing startup_relative_path");
	}

	const auto payload_root = absolute_bundle / "payload" / "c";
	const auto config_path = absolute_bundle / "dosbox" / "dosbox.conf";
	const auto startup_path = (payload_root / fs::path(it->second)).lexically_normal();
	const auto manifest_path = absolute_bundle / "manifest" / "bundle.toml";

	if (!fs::exists(payload_root) || !fs::is_directory(payload_root)) {
		throw std::runtime_error("bundle payload directory is missing: " + payload_root.string());
	}
	if (!fs::exists(config_path) || !fs::is_regular_file(config_path)) {
		throw std::runtime_error("bundle config is missing: " + config_path.string());
	}
	if (!fs::exists(startup_path) || !fs::is_regular_file(startup_path)) {
		throw std::runtime_error("bundle startup path is missing: " + startup_path.string());
	}
	if (!fs::exists(manifest_path) || !fs::is_regular_file(manifest_path)) {
		throw std::runtime_error("bundle manifest is missing: " + manifest_path.string());
	}

	return BundleMetadata{
	        .bundle_path           = absolute_bundle,
	        .config_path           = config_path,
	        .payload_root          = payload_root,
	        .startup_path          = startup_path,
	        .manifest_path         = manifest_path,
	        .startup_relative_path = it->second,
	};
}

} // namespace dosbundle
